#!/usr/bin/env bash
#
# build-mingw-vorbis.sh — build the static libraries the mingw cross build
# links, and install them into a prefix the krkrsdl2 mingw build consumes.
#
# Builds three things:
#   - libogg + libvorbis (BGM decoding), from deps-overlay vendored sources.
#   - libfreetype (font rendering), from the vendored tree the krkrsdl2 fork
#     ships at deps/krkrsdl2/external/krkrz/external/freetype.
# All are static archives so the Windows DLL stays self-contained: it imports
# no libvorbis/libfreetype/libogg DLL. The mingw sysroot on Debian/Ubuntu has
# none of these (Arch's does, which is why this is only needed off Arch), so
# they are built here from the same vendored sources the core uses.
#
# Without this script the Windows build silently has no .ogg decoder and no
# fonts: the engine's configure reports "vorbis/freetype not found", the build
# proceeds, and every KiriKiri BGM track is silent and every glyph is missing.
#
# Usage:
#   tools/build-mingw-vorbis.sh [PREFIX]
# PREFIX defaults to build/mingw-deps under the tree. It used to default
# to /tmp/mingw-vorbis, which a reboot wipes — and a wiped prefix does not fail
# loudly: the krkrsdl2 configure simply reports "vorbis not found", the Windows
# build proceeds without an .ogg decoder, and every KiriKiri BGM track is
# silently absent. Keeping it under build/ (gitignored) makes it survive and
# makes a rebuild cheap.
# The krkrsdl2 build reads vorbis from KRKRSDL2_MINGW_VORBIS_PREFIX
# (see deps/krkrsdl2/CMakeLists.txt) and freetype from PKG_CONFIG_PATH_CUSTOM
# (see the Makefile's mingw block): the cross pkg-config wrapper honours that
# env var and prepends it to its search path, which is the hook for a freetype
# we build ourselves.
#
set -eu

CORE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${1:-$CORE_DIR/build/mingw-deps}"
DEPBUILD="$CORE_DIR/build/mingw-deps"
TOOLCHAIN="${MINGW_TOOLCHAIN:-$DEPBUILD/mingw-toolchain.cmake}"
OGG_SRC="$CORE_DIR/deps-overlay/deps/libogg"
VORBIS_SRC="$CORE_DIR/deps-overlay/deps/libvorbis"
FT_SRC="$CORE_DIR/deps/krkrsdl2/external/krkrz/external/freetype"

[ -d "$OGG_SRC/src" ]        || { echo "missing $OGG_SRC" >&2; exit 1; }
[ -d "$VORBIS_SRC/lib" ]     || { echo "missing $VORBIS_SRC" >&2; exit 1; }
[ -d "$FT_SRC/include" ]     || { echo "missing $FT_SRC" >&2; exit 1; }
command -v x86_64-w64-mingw32-gcc >/dev/null || { echo "mingw-w64 not installed" >&2; exit 1; }

# The throwaway toolchain the core's mingw engine build uses. Regenerate it if
# it is gone so this script stands alone. CMAKE_SYSTEM_PROCESSOR must be
# "amd64": the project's CMake gates SSE2/AVX2 and the DirectShow quartz link on
# that value, not on "x86_64".
mkdir -p "$DEPBUILD"
if [ ! -f "$TOOLCHAIN" ]; then
  cat > "$TOOLCHAIN" <<'EOF'
set(CMAKE_SYSTEM_NAME Windows)
# Must be "amd64", not "x86_64": the engine's CMake gates its SSE2/AVX2 and
# DirectShow paths on this exact value.
set(CMAKE_SYSTEM_PROCESSOR amd64)
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set(CMAKE_AR           x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB       x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP        x86_64-w64-mingw32-strip)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(PKG_CONFIG_EXECUTABLE /usr/bin/x86_64-w64-mingw32-pkg-config)
EOF
  echo "wrote $TOOLCHAIN"
fi

# ---------------------------------------------------------------------------
# libogg + libvorbis
# ---------------------------------------------------------------------------
VORBIS_PREFIX="$PREFIX/vorbis"
mkdir -p "$VORBIS_PREFIX"
OGG_BUILD="${OGG_BUILD:-$DEPBUILD/bo-ogg}"
VORBIS_BUILD="${VORBIS_BUILD:-$DEPBUILD/bo-vorbis}"
rm -rf "$OGG_BUILD" "$VORBIS_BUILD"

# -DCMAKE_POLICY_VERSION_MINIMUM=3.5: these are autotools-era CMakeLists that
# declare cmake_minimum_required(2.8.12), which CMake >= 3.27 refuses.
cmake -S "$OGG_SRC" -B "$OGG_BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$VORBIS_PREFIX" \
  -DBUILD_SHARED_LIBS=OFF
ninja -C "$OGG_BUILD" -j"$(nproc)" install

cmake -S "$VORBIS_SRC" -B "$VORBIS_BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$VORBIS_PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DOGG_INCLUDE_DIR="$VORBIS_PREFIX/include" \
  -DOGG_LIBRARY="$VORBIS_PREFIX/lib/libogg.a"
ninja -C "$VORBIS_BUILD" -j"$(nproc)" install

echo
echo "installed static libogg/libvorbis into $VORBIS_PREFIX"

# ---------------------------------------------------------------------------
# libfreetype
# ---------------------------------------------------------------------------
# The vendored freetype CMakeLists has no install() target, so the install is
# done by hand below (archive, headers, .pc file). The .pc is what the
# engine's pkg_check_modules(FREETYPE2) and the core's FT_CFLAGS/FT_LIBS both
# consume; it is reached through PKG_CONFIG_PATH (Debian's wrapper) and
# PKG_CONFIG_PATH_CUSTOM (Arch's wrapper) — see the Makefile's mingw block.
FT_PREFIX="$PREFIX/freetype"
FT_BUILD="${FT_BUILD:-$DEPBUILD/bo-ft}"
rm -rf "$FT_BUILD" "$FT_PREFIX"
mkdir -p "$FT_PREFIX/lib/pkgconfig" "$FT_PREFIX/include/freetype2"

# -DFT2_BUILD_LIBRARY=1 is mandatory: without it the freetype sources expect
# FT_INTERNAL_OBJECTS_H and every file dies with "expects FILENAME" errors.
# -DBUILD_SHARED_LIBS=OFF for a static archive (self-contained DLL).
# -DCMAKE_POLICY_VERSION_MINIMUM=3.5: the CMakeLists declares 3.4.1, which
# CMake >= 3.27 refuses.
cmake -S "$FT_SRC" -B "$FT_BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$FT_PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-DFT2_BUILD_LIBRARY=1"
ninja -C "$FT_BUILD" -j"$(nproc)"

# The vendored CMakeLists has no install() target, so the install is done by
# hand: the archive is built into the build root, and the headers are the
# source tree's own include/. The .pc file is written here, modelled on
# /usr/x86_64-w64-mingw32/lib/pkgconfig/freetype2.pc on an Arch host.
# The built freetype uses its internal gzip code (FT_CONFIG_OPTION_SYSTEM_ZLIB
# is off in ftoption.h), so it has no zlib/bzip2/brotli dependency and the .pc
# lists none — unlike Arch's system freetype, which drags in all three.
cp "$FT_BUILD/libfreetype.a" "$FT_PREFIX/lib/libfreetype.a"
# Headers: ft2build.h and the freetype/ subtree both live under include/, and
# the .pc Cflags is -I${includedir}/freetype2 so callers #include <ft2build.h>.
cp "$FT_SRC/include/ft2build.h" "$FT_PREFIX/include/freetype2/"
cp -R "$FT_SRC/include/freetype" "$FT_PREFIX/include/freetype2/"

cat > "$FT_PREFIX/lib/pkgconfig/freetype2.pc" <<EOF
prefix=$FT_PREFIX
includedir=\${prefix}/include
libdir=\${prefix}/lib

Name: FreeType 2
Description: A free, high-quality, and portable font engine.
URL: https://freetype.org
Version: 2.6.3
Libs: -L\${libdir} -lfreetype
Cflags: -I\${includedir}/freetype2
EOF

echo
echo "installed static libfreetype into $FT_PREFIX"
ls -1 "$PREFIX"/{vorbis,freetype}/lib/*.a
