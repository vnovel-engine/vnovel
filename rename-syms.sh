#!/bin/sh
# Rename symbols in .o files using objcopy --redefine-sym
# Usage: rename-syms.sh <rename_file> <objcopy> <file.o> [...]
# rename_file format: old=new (one per line)
rename_file="$1"
objcopy="$2"
shift 2

for f in "$@"; do
	# Each line -> two args: --redefine-sym and old=new
	# Null-delimit so xargs -0 passes them as separate args without shell expansion
	sed 's/^/--redefine-sym\n/' "$rename_file" | tr '\n' '\0' | xargs -0 "$objcopy" "$f" 2>/dev/null || true
done
