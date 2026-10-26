#!/bin/sh
# ci/deb.sh — assemble a Debian/Ubuntu package for the Linux core.
#
# Usage: sh ci/deb.sh <arch> <version> [core-file]
#
# <arch> is a Debian architecture (amd64, arm64). <version> is the release
# version without a leading "v" (0.6.0, or 0.6.0-1 for a packaging revision).
# The core defaults to ./vnovel_libretro.so.
#
# The paths are the ones Debian's retroarch package configures:
#
#   libretro_directory = "/usr/lib/<triplet>/libretro/"
#   libretro_info_path = "/usr/share/libretro/info/"
#
# so a .deb drops the core into the tripleted directory and the .info beside
# the other core descriptions, where a frontend finds both on its own
# (verified against retroarch_1.20.0+dfsg-2 from Debian trixie).
#
# vnovel.rdb is NOT installed: it is the game-name database, and RetroArch
# reads it from the user's own <config>/retroarch/database/rdb/ (or from
# /usr/share/libretro/database/rdb for a system install, which no distribution
# package owns). A package that dropped it into the frontend's config
# directory would be writing to the user's home from a maintainer script, so
# the README points at install.sh for that instead.
#
# No dependencies are declared: the core links only libc, libstdc++, libm,
# libgcc, freetype, SDL2, fontconfig and vorbis, and deriving the runtime
# package names for the target distribution from a build inside a container is
# guesswork. An empty Depends is legal and honest; a wrong one is neither.

set -eu

arch=${1:?usage: sh ci/deb.sh <arch> <version> [core-file]}
version=${2:?usage: sh ci/deb.sh <arch> <version> [core-file]}
core=${3:-vnovel_libretro.so}

case "$arch" in
	amd64)  triplet=x86_64-linux-gnu ;;
	arm64)  triplet=aarch64-linux-gnu ;;
	i386)   triplet=i386-linux-gnu ;;
	*)      echo "deb: unmapped architecture: $arch" >&2; exit 1 ;;
esac

# Debian versions and git descriptions differ in what they allow. The control
# field wants a version that starts with a digit ("dev" is rejected outright by
# dpkg-deb: "version number does not start with digit"), and a git-describe
# fallback like "0.6.0-3-gabc1234-dirty" is not a version Debian will accept as
# written either. A release build from a tag is exact; anything else is made
# installable by stripping the git/dirty suffix, and a version that still does
# not start with a digit (the "dev" of a build with no git, or no tags in a
# shallow clone) is prefixed so the package can be built at all. Failing the job
# there would refuse to package a core that is already built, over a string.
deb_version=$(printf '%s' "$version" | sed -e 's/^v//' -e 's/-dirty$//' -e 's/-[0-9]*-g[0-9a-f]*$//')
[ -n "$deb_version" ] || deb_version=dev
case "$deb_version" in
	[0-9]*) ;;
	*) deb_version="0.0.0+$deb_version" ;;
esac

out=dist
pkg="vnovel-$deb_version-$arch"
stage="$out/$pkg"

[ -f "$core" ] || { echo "deb: core not found: $core" >&2; exit 1; }
[ -f vnovel_libretro.info ] || { echo "deb: vnovel_libretro.info not found" >&2; exit 1; }

# Debian's own name for the architecture, which is what the filename and the
# control file must agree on. dpkg wants exactly one of these.
case "$arch" in
	amd64)  triplet=x86_64-linux-gnu ;;
	arm64)  triplet=aarch64-linux-gnu ;;
	i386)   triplet=i386-linux-gnu ;;
	*)      echo "deb: unmapped architecture: $arch" >&2; exit 1 ;;
esac

rm -rf "$stage"
mkdir -p "$stage/DEBIAN" \
         "$stage/usr/lib/$triplet/libretro" \
         "$stage/usr/share/libretro/info" \
         "$stage/usr/share/doc/$pkg"

cp "$core" "$stage/usr/lib/$triplet/libretro/vnovel_libretro.so"
chmod 0755 "$stage/usr/lib/$triplet/libretro/vnovel_libretro.so"
cp vnovel_libretro.info "$stage/usr/share/libretro/info/"
chmod 0644 "$stage/usr/share/libretro/info/vnovel_libretro.info"
cp LICENSE "$stage/usr/share/doc/$pkg/copyright"

# The KiriKiri engine's non-compiled data, as vnovel/ beside the core in the
# libretro cores directory. The core finds it from its own path, so it belongs
# here and nowhere else: without system_polyfill/ no .xp3 game can boot, and
# without overrides.txt the engine tries to load the game's own Windows plugins
# and aborts startup.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engdata="$repo_dir/deps/krkrsdl2/data"
[ -d "$engdata/system_polyfill" ] ||
	{ echo "deb: no engine data at $engdata" >&2; exit 1; }
mkdir -p "$stage/usr/lib/$triplet/libretro/vnovel/system_polyfill"
cp "$engdata/overrides.txt" "$engdata/mona.ttf" \
	"$stage/usr/lib/$triplet/libretro/vnovel/"
cp "$engdata"/system_polyfill/*.tjs \
	"$stage/usr/lib/$triplet/libretro/vnovel/system_polyfill/"
for f in ComicNeue-Regular.ttf OpenDyslexic-Regular.otf \
	AtkinsonHyperlegible-Regular.ttf EBGaramond-Variable.ttf \
	BIZUDPGothic-Regular.ttf
do
	[ -f "$repo_dir/resources/fonts/$f" ] || continue
	cp "$repo_dir/resources/fonts/$f" \
		"$stage/usr/lib/$triplet/libretro/vnovel/"
done
chmod 0644 "$stage/usr/lib/$triplet/libretro/vnovel"/* \
	"$stage/usr/lib/$triplet/libretro/vnovel/system_polyfill"/*.tjs

installed_size=$(du -sk "$stage/usr" | cut -f1)

cat > "$stage/DEBIAN/control" <<EOF
Package: vnovel
Version: $deb_version
Architecture: $arch
Maintainer: Matcha & Nova <noreply@vnovel-engine.invalid>
Installed-Size: $installed_size
Section: games
Priority: optional
Homepage: https://github.com/vnovel-engine/vnovel
Description: Multi-engine libretro core for Japanese visual novels
 ONScripter-EN, PONScripter and KiriKiri (KAG) in one core, with the engine
 chosen from the game's script or data file (nscript.dat, pscript.dat,
 data.xp3).
 .
 The core is self-contained: the runtime and all three engines are linked in,
 so nothing accompanies it.
EOF

# Reproducible-enough: a fixed mtime for every member, and no timestamp on the
# archive itself. A release built twice from the same objects then differs only
# where it should.
find "$stage" -exec touch -h -d '2026-01-01 00:00:00 UTC' {} +
dpkg-deb --root-owner-group --build "$stage" "$out/$pkg.deb" >/dev/null
rm -rf "$stage"

echo "deb: wrote $out/$pkg.deb"
dpkg-deb -I "$out/$pkg.deb" | sed -n '2,8p'
dpkg-deb -c "$out/$pkg.deb" | awk '{print "  " $NF}'
