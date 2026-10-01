#!/usr/bin/env python3
"""decrypt_fate.py - Decrypt and extract Fate/Stay Night XP3 archives.
Uses FateCrypt algorithm: XOR 0x36 + bit flips at offsets 0x13 and 0x2ea29.
"""
import struct, zlib, sys, os

def fate_decrypt(data, file_offset=0):
    result = bytearray(data)
    for i in range(len(result)):
        result[i] ^= 0x36
    if file_offset <= 0x13 and file_offset + len(result) > 0x13:
        result[0x13 - file_offset] ^= 1
    if file_offset <= 0x2ea29 and file_offset + len(result) > 0x2ea29:
        result[0x2ea29 - file_offset] ^= 3
    return bytes(result)

def read_xp3_index(filename):
    f = open(filename, 'rb')
    f.read(11)
    index_offset = struct.unpack('<Q', f.read(8))[0]
    f.seek(index_offset)
    first = f.read(1)[0]
    if first == 0x01:
        comp_size = struct.unpack('<Q', f.read(8))[0]
        f.read(8)
        data = zlib.decompress(f.read(comp_size))
    else:
        f.seek(index_offset)
        data = f.read()
    return data, f

def parse_files(index_data):
    files = []
    pos = 0
    while pos + 12 <= len(index_data):
        chunk_type = index_data[pos:pos+4]
        chunk_size = struct.unpack_from('<Q', index_data, pos+4)[0]
        pos += 12
        if chunk_type == b'File':
            sub_pos = pos
            end = pos + chunk_size
            name = None
            segments = []
            while sub_pos + 12 <= end and sub_pos + 12 <= len(index_data):
                sub_type = index_data[sub_pos:sub_pos+4]
                sub_size = struct.unpack_from('<Q', index_data, sub_pos+4)[0]
                if sub_type == b'info':
                    namelen = struct.unpack_from('<H', index_data, sub_pos+12+20)[0]
                    name = index_data[sub_pos+12+22:sub_pos+12+22+namelen*2].decode('utf-16-le', errors='replace')
                elif sub_type == b'segm':
                    seg_count = sub_size // 28
                    for i in range(seg_count):
                        base = sub_pos + 12 + i * 28
                        seg_flags = struct.unpack_from('<I', index_data, base)[0]
                        offset_val = struct.unpack_from('<Q', index_data, base+4)[0]
                        org_size = struct.unpack_from('<Q', index_data, base+12)[0]
                        arc_size = struct.unpack_from('<Q', index_data, base+20)[0]
                        segments.append((offset_val, org_size, arc_size, (seg_flags & 7) == 1))
                sub_pos += 12 + sub_size
            if name:
                files.append({'name': name, 'segments': segments})
        pos += chunk_size
        if pos >= len(index_data):
            break
    return files

def extract_xp3(xp3_filename, output_dir, decrypt=False):
    index_data, f = read_xp3_index(xp3_filename)
    files = parse_files(index_data)
    print(f"{xp3_filename}: {len(files)} files")
    for file_info in files:
        name = file_info['name'].replace('\\', '/')
        if len(name) > 200:
            print(f"  Skipping: {name[:50]}...")
            continue
        out_path = os.path.join(output_dir, name)
        os.makedirs(os.path.dirname(out_path) if os.path.dirname(out_path) else output_dir, exist_ok=True)
        content = b''
        file_offset = 0
        for offset_val, org_size, arc_size, compressed in file_info['segments']:
            f.seek(offset_val)
            seg_data = f.read(arc_size)
            if compressed:
                try:
                    seg_data = zlib.decompress(seg_data)
                except:
                    pass
            if decrypt:
                seg_data = fate_decrypt(seg_data, file_offset)
            content += seg_data
            file_offset += len(seg_data)
        with open(out_path, 'wb') as out:
            out.write(content)
    f.close()

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: decrypt_fate.py <input.xp3> <output_dir> [--decrypt]", file=sys.stderr)
        sys.exit(1)
    xp3_file = sys.argv[1]
    output_dir = sys.argv[2]
    decrypt = '--decrypt' in sys.argv
    extract_xp3(xp3_file, output_dir, decrypt)
