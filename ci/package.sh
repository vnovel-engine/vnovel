#!/bin/sh
# ci/package.sh — assemble a release bundle for one platform.
#
# Usage: sh ci/package.sh <platform>
#
# Produces dist/vnovel-<version>-<platform>.tar.gz (and .zip for Windows)
# containing the single self-contained core, the core-info file, the game
# database and install.sh — the exact set install.sh expects.
#
# The version is the tag when built from one, else the short commit.

set -eu

platform=${1:?usage: package.sh <platform>}
out=dist

# Version: prefer an exact tag, else the short SHA. CI_COMMIT_TAG is set only
# for tag pipelines; a local run falls back to git describe.
version=""
if [ -n "${CI_COMMIT_TAG:-}" ]; then
	version=$CI_COMMIT_TAG
else
	version=$(git describe --tags --always --dirty 2>/dev/null || echo dev)
fi

# Files that MUST be in every bundle. The core is self-contained (runtime and
# both engines are linked in).
required="vnovel_libretro.so vnovel_libretro.info vnovel.rdb install.sh"

stage="$out/vnovel-$version-$platform"
rm -rf "$stage"
mkdir -p "$stage"

for f in $required; do
	[ -f "$f" ] || { echo "package: missing required file: $f" >&2; exit 1; }
	cp "$f" "$stage/"
done

# The logo, for the README the bundle ships and for anything that wants artwork.
# Computed from the script's own location, so it works from any CWD; not fatal,
# since a bundle without artwork is still a working bundle.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -f "$repo_dir/resources/logo.png" ]; then
	mkdir -p "$stage/resources"
	cp "$repo_dir/resources/logo.png" "$stage/resources/"
else
	echo "package: note: resources/logo.png not found, skipping artwork" >&2
fi

# Windows: the core links its support libraries dynamically (see the mingw
# block in the top-level Makefile), so the DLLs have to travel with it. They
# come from the mingw sysroot the build used ($MINGW_PREFIX/bin, or the usual
# cross locations), and a missing one is fatal — the core would not start.
case "$platform" in
	windows-*)
		for d in \
			"${MINGW_PREFIX:-/nonexistent}/bin" \
			/usr/x86_64-w64-mingw32/sys-root/mingw/bin \
			/usr/x86_64-w64-mingw32/bin \
			/usr/i686-w64-mingw32/sys-root/mingw/bin
		do
			[ -d "$d" ] || continue
			for dll in libfreetype-6.dll zlib1.dll libbrotlidec.dll \
				libbrotlicommon.dll libwinpthread-1.dll; do
				if [ -f "$d/$dll" ]; then
					cp "$d/$dll" "$stage/"
				elif [ ! -f "$stage/$dll" ]; then
					echo "package: WARNING: $dll not found in $d" >&2
				fi
			done
		done
		;;
esac

# A short readme so an unpacked bundle explains itself.
cat > "$stage/README.txt" <<EOF
VNovel $version ($platform)

A libretro core for Japanese visual novels: ONScripter-EN, PONScripter and
KiriKiri (KAG) in one core.

Install:
    ./install.sh                 # detect the frontend's cores directory
    ./install.sh --dir ~/.config/retroarch/cores

The core is a single self-contained vnovel_libretro.so (the runtime and both
engines are linked in). install.sh copies it and also installs vnovel.rdb,
which lets RetroArch show a game's name instead of its script filename when you
scan a folder.

Artwork is in resources/ (logo.png).

Load a game by selecting its nscript.dat, pscript.dat or data.xp3.
Documentation: https://docs.libretro.com/library/vnovel/
EOF

chmod 0755 "$stage/install.sh"

# Archives. Windows users expect a .zip; everyone else gets a tar.gz.
tar -C "$out" -czf "$out/vnovel-$version-$platform.tar.gz" \
	"vnovel-$version-$platform"
if command -v zip >/dev/null 2>&1; then
	(cd "$out" && zip -qr "vnovel-$version-$platform.zip" "vnovel-$version-$platform")
else
	echo "package: note: zip not available, skipping the Windows .zip"
fi

rm -rf "$stage"
echo "package: wrote:"
ls -l "$out"
