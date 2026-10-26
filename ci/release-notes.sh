#!/bin/sh
# ci/release-notes.sh — print the CHANGELOG section for one release.
#
# Usage: sh ci/release-notes.sh <version> [changelog]
#
# The version is a tag; a leading "v" is ignored. The section runs from the
# "## <version>" heading to the next "## " heading, which is the shape
# CHANGELOG.md uses. Nothing is printed, and the exit status is 1, when the
# version has no section — a release whose notes are missing should say so
# rather than publish an empty body.

set -eu

version=${1:?usage: sh ci/release-notes.sh <version> [changelog]}
version=${version#v}
file=${2:-CHANGELOG.md}

[ -f "$file" ] || { echo "release-notes: no $file" >&2; exit 1; }

notes=$(awk -v v="$version" '
	# Match the version as a whole heading word, so 0.6.0 does not also
	# match a hypothetical "0.6.0-rc1" section, and so the dot is a literal.
	index($0, "## " v) == 1 && (length($0) == 3 + length(v) || substr($0, 4 + length(v), 1) ~ /[ \t—-]/) { p = 1; next }
	p && index($0, "## ") == 1 { exit }
	p { print }
' "$file")

# Trim leading and trailing blank lines so the notes do not open with an
# empty gap in the release body.
notes=$(printf '%s\n' "$notes" | sed -e '/./,$!d' -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')

[ -n "$notes" ] || { echo "release-notes: $file has no section for $version" >&2; exit 1; }
printf '%s\n' "$notes"
