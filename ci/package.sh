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

# Version: the tag when building from one, else the git description. One
# implementation, in ci/version.sh, because the Debian package and the release
# notes need the same answer.
version=$(sh "$(dirname "$0")/version.sh")

# The core carries the platform's own extension, and the bundle ships exactly
# one of them: a .so for Linux, a .dll for Windows, a .dylib for macOS. Found
# by name rather than derived from $platform, so a mislabelled platform string
# cannot make this pick the wrong file or no file.
core=""
for ext in so dll dylib; do
	if [ -f "vnovel_libretro.$ext" ]; then
		core="vnovel_libretro.$ext"
		break
	fi
done
[ -n "$core" ] || {
	echo "package: no vnovel_libretro.{so,dll,dylib} in $(pwd)" >&2
	echo "package: build the core first (make / make platform=mingw)" >&2
	exit 1
}

# Files that MUST be in every bundle. The core is self-contained (runtime and
# both engines are linked in).
required="$core vnovel_libretro.info vnovel.rdb install.sh"

stage="$out/vnovel-$version-$platform"
rm -rf "$stage"
mkdir -p "$stage"

for f in $required; do
	[ -f "$f" ] || { echo "package: missing required file: $f" >&2; exit 1; }
	cp "$f" "$stage/"
done

# The KiriKiri engine's non-compiled data, under vnovel/ beside the core.
#
# Not optional, and not cosmetic: without system_polyfill/ the engine has no
# scenario parser and a .xp3 game cannot boot at all, and without overrides.txt
# it treats the game's own Windows plugins as loadable and aborts startup. The
# core finds this directory from the libretro core path (the directory it was
# loaded from), so a bundle that omits it produces a core that starts and then
# fails on every KiriKiri game — which is exactly what earlier bundles did.
#
# install.sh copies the whole tree next to the core, so the layout here is the
# layout at the frontend's cores directory.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engdata_src="$repo_dir/deps/krkrsdl2/data"
if [ -d "$engdata_src/system_polyfill" ]; then
	mkdir -p "$stage/vnovel/system_polyfill"
	cp "$engdata_src/overrides.txt" "$engdata_src/mona.ttf" "$stage/vnovel/"
	cp "$engdata_src"/system_polyfill/*.tjs "$stage/vnovel/system_polyfill/"
else
	echo "package: ERROR: $engdata_src/system_polyfill missing" >&2
	echo "package: the engine cannot run a KiriKiri game without it" >&2
	exit 1
fi

# The core option's selectable faces. Their absence costs the reader the choice
# of that face and nothing else, so unlike the polyfill these are best-effort —
# but they are the feature the option exists for, so a bundle is expected to
# have them.
for f in ComicNeue-Regular.ttf OpenDyslexic-Regular.otf \
	AtkinsonHyperlegible-Regular.ttf EBGaramond-Variable.ttf \
	BIZUDPGothic-Regular.ttf
do
	[ -f "$repo_dir/resources/fonts/$f" ] || continue
	cp "$repo_dir/resources/fonts/$f" "$stage/vnovel/"
done

# The logo, for the README the bundle ships and for anything that wants artwork.
# Computed from the script's own location, so it works from any CWD; not fatal,
# since a bundle without artwork is still a working bundle.
if [ -f "$repo_dir/resources/logo.png" ]; then
	mkdir -p "$stage/resources"
	cp "$repo_dir/resources/logo.png" "$stage/resources/"
else
	echo "package: note: resources/logo.png not found, skipping artwork" >&2
fi

# Windows: whatever the core imports from outside Windows travels with it. The
# list is read from the DLL's own import table rather than guessed at, for two
# reasons: the mingw build links freetype, bzip2 and brotli statically (see the
# mingw block in the top-level Makefile), so copying them unconditionally would
# ship several megabytes nothing loads; and a dependency that is neither a
# Windows system library nor present in the toolchain is fatal, because the core
# would not start — which the old per-name warning hid.
case "$platform" in
	windows-*)
		imports=$(x86_64-w64-mingw32-objdump -p "$core" 2>/dev/null |
			sed -n 's/^[[:space:]]*DLL Name: //p' | sort -u)
		for dll in libfreetype-6.dll zlib1.dll libbrotlidec.dll \
			libbrotlicommon.dll libwinpthread-1.dll
		do
			printf '%s\n' "$imports" | grep -qix "$dll" || continue
			found=""
			for d in \
				"${MINGW_PREFIX:-/nonexistent}/bin" \
				/usr/x86_64-w64-mingw32/sys-root/mingw/bin \
				/usr/x86_64-w64-mingw32/bin \
				/usr/i686-w64-mingw32/sys-root/mingw/bin
			do
				[ -f "$d/$dll" ] || continue
				cp "$d/$dll" "$stage/"
				found=1
				break
			done
			[ -n "$found" ] ||
				{ echo "package: ERROR: the core imports $dll and it was not found" >&2; exit 1; }
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

The core is a single self-contained $core (the runtime and both engines are
linked in). install.sh copies it and also installs vnovel.rdb, which lets
RetroArch show a game's name instead of its script filename when you scan a
folder.

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
