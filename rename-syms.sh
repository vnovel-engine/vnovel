#!/bin/sh
# Rename symbols in .o files using objcopy --redefine-sym
# Usage: rename-syms.sh <rename_file> <objcopy> <file.o> [...]
# rename_file format: old=new (one per line)
rename_file="$1"
objcopy="$2"
shift 2

# Build a set of old symbol names being renamed (for COMDAT matching)
old_syms=$(sed 's/=.*//' "$rename_file")

for f in "$@"; do
	# objcopy --redefine-sym renames symbols but NOT COMDAT group
	# signatures. Both engines include headers with the same class
	# names, so inline constructors share COMDAT group signatures
	# (e.g. _ZN13ScriptHandler12VariableDataC5Ev).
	# The linker deduplicates by signature, silently dropping one
	# engine's code.
	#
	# Fix: for each COMDAT group whose signature matches a renamed
	# symbol, mark that section group as non-unique by removing the
	# GROUP flag from the group section. This prevents the linker
	# from deduplicating it, while keeping within-library COMDATs
	# (like std::piecewise_construct) intact.
	#
	# We use objcopy --set-section-flags .group=alloc,readonly to
	# strip the GROUP flag from all .group sections, but only if
	# the file contains renamed COMDAT signatures.
	needs_group_strip=0
	groups=$(readelf -g "$f" 2>/dev/null | sed -n "s/.*\.group.* \[\([^]]*\)\] contains.*/\1/p")
	for sig in $groups; do
		for old in $old_syms; do
			if [ "$sig" = "$old" ]; then
				needs_group_strip=1
				break 2
			fi
		done
	done
	if [ "$needs_group_strip" = "1" ]; then
		tmp=$(mktemp "${f}.XXXXXX")
		"$objcopy" --set-section-flags .group=contents,readonly "$f" "$tmp" 2>/dev/null && mv "$tmp" "$f" || rm -f "$tmp"
	fi
	# Use --redefine-sym=old=new (single arg per symbol) so xargs
	# never splits a --redefine-sym flag from its value.
	sed 's/^/--redefine-sym=/' "$rename_file" | tr '\n' '\0' | xargs -0 "$objcopy" "$f"
done
