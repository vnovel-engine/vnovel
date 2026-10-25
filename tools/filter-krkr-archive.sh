#!/bin/sh
# filter-krkr-archive.sh — make a linkable krkrsdl2 archive for the single-file core.
#
# Usage: filter-krkr-archive.sh <in.a> <out.a>
#
# Why
# krkrsdl2 vendors its own libogg (external/krkrz/external/libogg/src/*.c) and our
# runtime vendors another. Linked into one core that is a duplicate definition of
# every ogg_* symbol: 71 of them, the only C collisions between the KiriKiri
# engine and ours (the C++ sides are entirely disjoint — KiriKiri's namespaces
# share nothing with ONScripter's, and those two were already separated by
# ons/pons-symbol-prefix.h).
#
# One copy is enough. The two liboggs are different revisions — 2010 vs 2018
# internals, 256 lines apart in framing.c — but the public API and every ABI
# struct (ogg_packet, ogg_page, ogg_stream_state, ogg_sync_state) are identical,
# and the exported symbol set matches exactly (35 names, both sides). Keeping
# ours is therefore a straight win: it is the newer one, and the runtime already
# links it.
#
# What this does
# Copies the archive with krkrsdl2's ogg members removed. Members are matched by
# name (bitwise.c.o, framing.c.o), not by path, because the archive is flat — the
# build system already flattened the directory structure when it created it.
#
# The exit status distinguishes "filtered" from "nothing to do", so a build can
# report which happened rather than silently doing either.

set -eu

in_a=${1:?usage: filter-krkr-archive.sh <in.a> <out.a>}
out_a=${2:?usage: filter-krkr-archive.sh <in.a> <out.a>}

# Resolve to absolute paths before changing directory: the work happens inside
# a temporary directory, and a relative argument would otherwise be resolved
# against that instead of the caller's cwd.
case "$in_a" in
	/*) ;;
	*) in_a="$(pwd)/$in_a" ;;
esac
case "$out_a" in
	/*) ;;
	*) out_a="$(pwd)/$out_a" ;;
esac

[ -f "$in_a" ] || { printf '%s: no such archive: %s\n' "$(basename "$0")" "$in_a" >&2; exit 1; }

# The ogg members to drop, by basename. Both come from libogg/src/ in the tree;
# listing basenames keeps this working whatever prefix the archive used.
OGG_MEMBERS="bitwise.c.o framing.c.o"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Object members are named *.o on ELF and *.obj on PE/COFF (mingw). Discover the
# suffix from the archive instead of assuming: with `*.o` hard-coded, a mingw
# archive extracts fine and then `ar rcs out.a *.o` matches nothing, failing with
# "*.o: No such file or directory" and an empty output archive.
member_suffix=
for m in $(ar t "$in_a"); do
	case "$m" in
	*.obj) member_suffix=obj ;;
	*.o)   member_suffix=o ;;
	esac
	[ -n "$member_suffix" ] && break
done
[ -n "$member_suffix" ] || {
	printf '%s: no object members found in %s\n' "$(basename "$0")" "$in_a" >&2
	exit 1
}

(
	cd "$tmp"
	ar x "$in_a"  # absolute or relative, both fine
	dropped=0
	for m in $OGG_MEMBERS; do
		if [ -f "$m" ]; then
			rm -f "$m"
			dropped=$((dropped + 1))
		fi
	done
	# A mingw archive names its members *.obj, so the ogg members to drop carry
	# that suffix there. Word-split the list into separate names before
	# substituting: sed applies its end-of-line anchor per line, so feeding both
	# names on one line leaves all but the last unmatched and the member
	# survives, which then collides at link time.
	if [ "$member_suffix" = obj ]; then
		for m in $OGG_MEMBERS; do
			obj=${m%.o}.obj
			if [ -f "$obj" ]; then
				rm -f "$obj"
				dropped=$((dropped + 1))
			fi
		done
	fi

	# `ar rcs` creates the archive if absent and builds an index. An empty
	# file created by a shell redirect is NOT a valid archive and ar rejects
	# it ("file format not recognized"), so let ar make the file.
	rm -f "$out_a"
	ar rcs "$out_a" *."$member_suffix"
	printf '%s: dropped %s ogg member(s), %s members remain\n' \
		"$(basename "$out_a")" "$dropped" \
		"$(ls *."$member_suffix" | wc -l)" >&2
)

# Refuse to produce an archive with no ogg removed: that means the names changed
# upstream, and the link would fail later with a confusing duplicate definition
# instead of here with a clear one.
if ! ar t "$in_a" | grep -qxE 'bitwise\.c\.(o|obj)' && ! ar t "$in_a" | grep -qxE 'framing\.c\.(o|obj)'; then
	printf '%s: WARNING: no ogg members found in %s — the upstream layout may have changed\n' \
		"$(basename "$0")" "$in_a" >&2
fi
