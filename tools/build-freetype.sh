#!/bin/sh
# build-freetype.sh — fetch and build a pinned FreeType for one target.
#
# Usage: tools/build-freetype.sh [host|mingw|aarch64|osx]
#
# Downloads the pinned tarball once into build/freetype-src/, builds a static
# library with FreeType's own gzip code (no zlib/bzip2/brotli/png/harfbuzz),
# and installs it into build/freetype/<target>. The Makefile consumes that
# prefix through FREETYPE_PREFIX.
#
# Why this exists: the mingw build used to compile the krkrz fork's FreeType
# 2.6.3, while Linux and macOS linked whatever the host had. That pinned an
# old engine on one platform and made the same face render differently across
# them (the bundled EB Garamond is a variable font, which 2.6.3 predates).
# One pinned, current FreeType for every target removes both problems.
#
# The build is static and self-contained: --without-zlib keeps FreeType's
# internal gzip, so the Windows DLL imports no libfreetype/libz/libbz2 DLL and
# still passes ci/verify-bundle.sh's "only Windows system libraries" check.

set -eu

VERSION=${FREETYPE_VERSION:-2.15.0}
TARGET=${1:-host}
CORE_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC_ROOT="$CORE_DIR/build/freetype-src"
SRC="$SRC_ROOT/freetype-$VERSION"
PREFIX="$CORE_DIR/build/freetype/$TARGET"
TARBALL="$SRC_ROOT/freetype-$VERSION.tar.xz"
URL="https://download.savannah.gnu.org/releases/freetype/freetype-$VERSION.tar.xz"

case "$TARGET" in
	host)    HOST= ;;
	mingw)   HOST=x86_64-w64-mingw32 ;;
	aarch64) HOST=aarch64-linux-gnu ;;
	osx)     HOST=arm-apple-darwin ;;
	*) echo "build-freetype: unknown target: $TARGET" >&2; exit 1 ;;
esac

mkdir -p "$SRC_ROOT"
[ -f "$TARBALL" ] || curl -fL --retry 3 -o "$TARBALL" "$URL"
[ -d "$SRC" ] || tar -C "$SRC_ROOT" -xf "$TARBALL"

BUILD="$CORE_DIR/build/freetype-build/$TARGET"
rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD" "$PREFIX"

cd "$BUILD"
# -fPIC: the host/aarch64/macOS builds link this archive into a shared object.
# PE does not need it, but the flag is harmless there.
CFLAGS="-fPIC" "$SRC/configure" ${HOST:+--host=$HOST} \
	--prefix="$PREFIX" \
	--enable-static --disable-shared \
	--without-zlib --without-bzip2 --without-brotli --without-png \
	--without-harfbuzz --disable-freetype-config
make -j"$(nproc)"
make install

echo "build-freetype: installed $VERSION for $TARGET into $PREFIX"
