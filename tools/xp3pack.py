#!/usr/bin/env python3
"""xp3pack.py — build a minimal KiriKiri XP3 archive from a directory.

Why this exists: the KiriKiri path in the core detects its engine by looking for
`data.xp3` beside the loaded file (src/libretro.cpp), so a test fixture made of
loose files cannot be loaded at all — it is detected as ONScripter and then fails
with "no game script found". Packing the fixture is what makes it runnable.

The format below is not taken from the spec's prose but from the archives that
actually load, and was checked by round-tripping through the reader at
/home/matcha/dev/fate-harness/xp3.py — the parser that reads real commercial
archives. Four details the first version got wrong, each found that way:

  1. the magic is 11 bytes, not 10 (see MAGIC below);
  2. segment offsets are ABSOLUTE file offsets, not body-relative (the reader
     does f.seek(off) from the start of the file);
  3. a directory's contents go to the archive ROOT, not under the directory's
     name — the engine looks a member up by its archive-relative path;
  4. an `adlr` sub-chunk is REQUIRED inside the `info` section; the engine
     refuses the archive without it (XP3Archive.cpp:514 throws
     TVPReadError). It is written here as a 4-byte zero.

Layout written (all little-endian):

    0x00  11 bytes  magic "XP3\\r\\n \\x1a\\x8b\\x67\\x01"
    0x0B  u64       offset of the index
    ...             file data, each segment stored zlib-compressed
    ...             the index blob
    EOF   11 bytes  magic again, then u64 index offset

Index blob, repeated per file:

    "File"  u64 section_size
      "info" u64 section_size
        u32 flags          (0 — no extra fields)
        u64 original size
        u64 stored size
        u16 name length in code units
        name in UTF-16LE
      "segm" u64 section_size
        per segment: u32 seg_flags | u64 offset | u64 original | u64 stored
    ...zero padding...

Usage:
    xp3pack.py OUT.xp3 SRCDIR [SRCDIR ...]
"""
import os
import struct
import sys
import zlib

# The 11-byte magic, taken from the bytes a real archive actually has:
#
#     58 50 33 0d 0a 20 0a 1a 8b 67 01
#     X  P  3 \r \n sp \n sub 8b  g  01
#
# Written as one explicit byte string rather than the documented
# b"XP3\r\n \x1a\x8b\x67\x01": that literal is 10 bytes, because " \x1a" is
# only two bytes where the format has three (space, 0x0a, 0x1a), and a `\x8bg`
# run additionally swallows the following character. Both mistakes produce a
# magic the reader cannot match, which is silent at write time and only shows up
# as "struct.error" when something tries to read the archive back.
MAGIC = bytes([0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20, 0x0A, 0x1A,
               0x8B, 0x67, 0x01])

# Segment flag bits, as the reader tests them: `sf & 7` means compressed. 1 is
# zlib for the data segment; 0 is a raw segment.
SEG_COMPRESSED = 1


def collect(sources):
    """(archive name, local path) for every file, in a stable order."""
    out = []
    for src in sources:
        if os.path.isfile(src):
            out.append((os.path.basename(src), src))
            continue
        # A directory's own contents go to the archive ROOT: packing /g/dir
        # stores "startup.tjs", not "dir/startup.tjs". That is what a KiriKiri
        # archive means - the engine asks for startup.tjs by name at the root -
        # and getting it wrong is silent: the file is simply never found, the
        # engine raises a startup script exception, and on this build dies in
        # the exception path without printing which file was missing.
        base = os.path.abspath(src)
        for root, _dirs, files in os.walk(src):
            for f in sorted(files):
                full = os.path.join(root, f)
                name = os.path.relpath(full, base).replace(os.sep, "/")
                out.append((name, full))
    out.sort(key=lambda t: t[0])
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out_path, sources = sys.argv[1], sys.argv[2:]

    files = collect(sources)
    if not files:
        print("xp3pack: no input files", file=sys.stderr)
        return 1

    body = bytearray()
    entries = []
    # Segment offsets are ABSOLUTE file offsets, not body-relative: the reader
    # does f.seek(off) and reads there. Writing body-relative offsets pointed the
    # first segment into the archive header, which read back as garbage.
    header_len = len(MAGIC) + 8
    for name, path in files:
        raw = open(path, "rb").read()
        comp = zlib.compress(raw, 6)
        # Only keep the compressed form when it actually helps; otherwise store
        # raw, which the reader handles as seg_flags 0.
        if len(comp) < len(raw):
            seg_flags, payload = SEG_COMPRESSED, comp
        else:
            seg_flags, payload = 0, raw
        offset = header_len + len(body)
        body += payload
        entries.append((name, raw, len(payload), offset, seg_flags))
        if len(body) % 4:                    # keep segments aligned
            body += b"\0" * (4 - (len(body) % 4))

    index = bytearray()
    for name, raw, stored, offset, seg_flags in entries:
        name_u16 = name.encode("utf-16-le")
        info = struct.pack("<IQQH", 0, len(raw), stored, len(name_u16) // 2)
        info += name_u16
        segm = struct.pack("<IQQQ", seg_flags, offset, len(raw), stored)
        # adlr: a 4-byte file hash, and REQUIRED. The engine's index reader calls
        # FindChunk(cn_adlr, ...) and throws TVPReadError when it is absent
        # (XP3Archive.cpp:514), which surfaces as "Failed." binding the archive
        # and then a startup script exception. Every real archive carries one;
        # the value is not validated by this engine, so 0 will do.
        adlr = struct.pack("<I", 0)
        section = b"info" + struct.pack("<Q", len(info)) + info
        section += b"segm" + struct.pack("<Q", len(segm)) + segm
        section += b"adlr" + struct.pack("<Q", len(adlr)) + adlr
        index += b"File" + struct.pack("<Q", len(section)) + section
    header_len = len(MAGIC) + 8
    index_offset = header_len + len(body)

    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<Q", index_offset))
        f.write(bytes(body))
        f.write(b"\0")               # hash type 0: uncompressed index follows
        f.write(struct.pack("<Q", len(index)))
        f.write(bytes(index))
        f.write(MAGIC)
        f.write(struct.pack("<Q", index_offset))

    print(f"{out_path}: {len(entries)} file(s), {len(body)} data bytes, "
          f"index at {index_offset}, total {os.path.getsize(out_path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
