#!/usr/bin/env bash
#
# build-mingw-vorbis.sh — build static libogg/libvorbis for the mingw-w64
# cross toolchain and install them into a prefix the krkrsdl2 mingw build
# consumes.
#
# Why this exists: krkrsdl2's Windows DLL has to be self-contained, so it links
# vorbis (BGM decoding) and freetype statically rather than importing
# libvorbis/libfreetype DLLs. The mingw sysroot has no static vorbis, so it is
# built here from the same vendored sources the core uses.
# (deps-overlay/deps/libogg, deps-overlay/deps/libvorbis). Without this the
# Windows build silently has no .ogg decoder and all KiriKiri BGM is silent.
#
# Usage:
#   tools/build-mingw-vorbis.sh [PREFIX]
# PREFIX defaults to build/mingw-deps/vorbis under the tree. It used to default
# to /tmp/mingw-vorbis, which a reboot wipes — and a wiped prefix does not fail
# loudly: the krkrsdl2 configure simply reports "vorbis not found", the Windows
# build proceeds without an .ogg decoder, and every KiriKiri BGM track is
# silently absent. Keeping it under build/ (gitignored) makes it survive and
# makes a rebuild cheap.
# The krkrsdl2 build reads it from KRKRSDL2_MINGW_VORBIS_PREFIX
# (see deps/krkrsdl2/CMakeLists.txt).
#
set -eu

CORE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${1:-$CORE_DIR/build/mingw-deps/vorbis}"
DEPBUILD="$CORE_DIR/build/mingw-deps"
TOOLCHAIN="${MINGW_TOOLCHAIN:-$DEPBUILD/mingw-toolchain.cmake}"
OGG_SRC="$CORE_DIR/deps-overlay/deps/libogg"
VORBIS_SRC="$CORE_DIR/deps-overlay/deps/libvorbis"

[ -d "$OGG_SRC/src" ]        || { echo "missing $OGG_SRC" >&2; exit 1; }
[ -d "$VORBIS_SRC/lib" ]     || { echo "missing $VORBIS_SRC" >&2; exit 1; }
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

mkdir -p "$PREFIX"
OGG_BUILD="${OGG_BUILD:-$DEPBUILD/bo-ogg}"
VORBIS_BUILD="${VORBIS_BUILD:-$DEPBUILD/bo-vorbis}"
rm -rf "$OGG_BUILD" "$VORBIS_BUILD"

# -DCMAKE_POLICY_VERSION_MINIMUM=3.5: these are autotools-era CMakeLists that
# declare cmake_minimum_required(2.8.12), which CMake >= 3.27 refuses.
cmake -S "$OGG_SRC" -B "$OGG_BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF
ninja -C "$OGG_BUILD" -j"$(nproc)" install

cmake -S "$VORBIS_SRC" -B "$VORBIS_BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DOGG_INCLUDE_DIR="$PREFIX/include" \
  -DOGG_LIBRARY="$PREFIX/lib/libogg.a"
ninja -C "$VORBIS_BUILD" -j"$(nproc)" install

echo
echo "installed static libogg/libvorbis into $PREFIX"
ls -1 "$PREFIX/lib"/*.a
