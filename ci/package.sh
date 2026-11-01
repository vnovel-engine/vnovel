#!/bin/sh
# ci/package.sh — assemble a release bundle for one platform.
#
# Usage: sh ci/package.sh <platform>
#
# Produces dist/vnovel-<version>-<platform>.tar.gz (and .zip for Windows).
#
# Two layouts:
#
#   flat      the core, the core-info, vnovel.rdb, install.sh and vnovel/ side
#             by side — what install.sh expects, and what the Linux and macOS
#             bundles ship.
#
#   drop-in   Windows only: cores/ and database/rdb/ at the archive root, so
#             extracting the archive into a portable RetroArch directory puts
#             every file where the frontend reads it. install.bat is still
#             shipped, for a cores directory somewhere else.
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

stage="$out/vnovel-$version-$platform"
rm -rf "$stage"
mkdir -p "$stage"

# Where the core and its data go inside the bundle, and where the database
# goes. The flat layout keeps everything at the root; the Windows drop-in
# layout mirrors the frontend's own directory tree.
case "$platform" in
	windows-*)
		core_dir="$stage/cores"
		db_dir="$stage/database/rdb"
		data_dir="$stage/system/vnovel"
		required="$core vnovel_libretro.info vnovel.rdb install.bat"
		;;
	*)
		core_dir="$stage"
		db_dir="$stage"
		data_dir="$stage/vnovel"
		required="$core vnovel_libretro.info vnovel.rdb install.sh"
		;;
esac
mkdir -p "$core_dir" "$db_dir" "$data_dir"

for f in $required; do
	[ -f "$f" ] || { echo "package: missing required file: $f" >&2; exit 1; }
	case "$f" in
		vnovel.rdb)             cp "$f" "$db_dir/" ;;
		install.sh|install.bat) cp "$f" "$stage/" ;;
		*)                      cp "$f" "$core_dir/" ;;
	esac
done

# The KiriKiri engine's non-compiled data, under vnovel/ in the frontend's
# system directory (system/vnovel/ in the Windows drop-in archive, vnovel/ at
# the root of the flat bundles).
#
# Not optional, and not cosmetic: without system_polyfill/ the engine has no
# scenario parser and a .xp3 game cannot boot at all, and without overrides.txt
# it treats the game's own Windows plugins as loadable and aborts startup. The
# core finds this directory from the libretro core path (the directory it was
# loaded from), so a bundle that omits it produces a core that starts and then
# fails on every KiriKiri game — which is exactly what earlier bundles did.
#
# The installers copy the whole tree into the frontend's system directory, so
# the layout here is the layout at that directory.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engdata_src="$repo_dir/deps/krkrsdl2/data"
if [ -d "$engdata_src/system_polyfill" ]; then
	mkdir -p "$data_dir/system_polyfill"
	cp "$engdata_src/overrides.txt" "$engdata_src/mona.ttf" "$data_dir/"
	cp "$engdata_src"/system_polyfill/*.tjs "$data_dir/system_polyfill/"
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
	cp "$repo_dir/resources/fonts/$f" "$data_dir/"
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
				cp "$d/$dll" "$core_dir/"
				found=1
				break
			done
			[ -n "$found" ] ||
				{ echo "package: ERROR: the core imports $dll and it was not found" >&2; exit 1; }
		done
		;;
esac

# A short readme so an unpacked bundle explains itself.
case "$platform" in
	windows-*)
		hint='Extract this archive into your RetroArch directory (the one with
retroarch.exe): the cores/, system/ and database/ trees land where the frontend
reads them, so nothing else is needed. To install into a cores directory
somewhere else, run:

    install.bat --dir "C:\path\to\cores"'
		;;
	*)
		hint='Install:
    ./install.sh                 # detect the cores directory
    ./install.sh --dir ~/.config/retroarch/cores'
		;;
esac

cat > "$stage/README.txt" <<EOF
VNovel $version ($platform)

A libretro core for Japanese visual novels: ONScripter-EN, PONScripter and
KiriKiri (KAG) in one core.

$hint

The core is a single self-contained $core (the runtime and both engines are
linked in). The installer also places vnovel.rdb, which lets RetroArch show a
game's name instead of its script filename when you scan a folder.

Artwork is in resources/ (logo.png).

Load a game by selecting its nscript.dat, pscript.dat or data.xp3.
Documentation: https://docs.libretro.com/library/vnovel/
EOF

if [ -f "$stage/install.sh" ]; then
	chmod 0755 "$stage/install.sh"
fi

# Archives. Windows users expect a .zip; everyone else gets a tar.gz.
tar -C "$out" -czf "$out/vnovel-$version-$platform.tar.gz" \
	"vnovel-$version-$platform"
if command -v zip >/dev/null 2>&1; then
	# zip updates an existing archive rather than replacing it, so a rebuild
	# would keep members from the previous one.
	rm -f "$out/vnovel-$version-$platform.zip"
	case "$platform" in
		windows-*)
			# No wrapper directory: extracting the zip into a portable
			# RetroArch directory is the install.
			(cd "$stage" && zip -qr "../vnovel-$version-$platform.zip" .)
			;;
		*)
			(cd "$out" && zip -qr "vnovel-$version-$platform.zip" "vnovel-$version-$platform")
			;;
	esac
else
	echo "package: note: zip not available, skipping the .zip" >&2
fi

rm -rf "$stage"
echo "package: wrote:"
ls -l "$out"
