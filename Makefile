# vnovel Libretro Core — Makefile
#
# Build targets:
#   make                    Linux x86_64 (.so)
#   make platform=aarch64   Cross-compile for aarch64/RPi5 (.so)
#   make platform=mingw     Cross-compile for Windows x86_64 (.dll)
#   make DEBUG=1            Debug build with -O0 -g
#
# Based on the standard libretro Makefile template used by bsnes, picodrive,
# and other libretro cores. Compatible with the libretro-super buildbot.

# Quiet mode
ifneq ($(V),1)
   Q := @
endif

# Platform detection
ifeq ($(platform),)
   platform = unix
   ifeq ($(shell uname -s 2>/dev/null),)
      platform = win
   else ifneq ($(findstring MINGW,$(shell uname -s)),)
      platform = mingw
   else ifneq ($(findstring Darwin,$(shell uname -s)),)
      platform = osx
   else ifneq ($(findstring win,$(shell uname -s)),)
      platform = win
   endif
endif

# Core identity
# Pin the default goal: an earlier rule would otherwise become it, and the
# KiriKiri archive rule is defined above `all`.
.DEFAULT_GOAL := all
CORE_DIR     := .
# C/C++ sources. The Makefiles and generated manifests (sdl-vendored.h)
# stay at $(CORE_DIR) so `make` runs from a pristine clone; only the sources
# move here.
SRC_DIR      := $(CORE_DIR)/src

# Auto-create symlinks to vendored deps from deps-overlay
$(shell for d in bzip2 libmad libogg libvorbis sdl sdl-mixer libretro-common; do \
	[ -e $(CORE_DIR)/deps/$$d ] || ln -s ../deps-overlay/deps/$$d $(CORE_DIR)/deps/$$d 2>/dev/null; \
done)
TARGET_NAME  := vnovel
LIBM         := -lm

# Defaults (overridden by platform blocks below)
CC            ?= cc
CXX           ?= c++
fpic          :=
SHARED        :=
TARGET_EXT    := .so
PLATFLAGS     :=
SDL_SYSTIMER  ?= unix/SDL_systimer.c

# Platform configuration
# Link-time extras for ELF/POSIX targets. dlopen is still needed: the KiriKiri
# engine's overrides table can name a plugin to load, and the frontend path is
# resolved through it. -z muldefs is gone: it existed only because the
# symbol-rename machinery deliberately produced duplicate symbols.
LINK_EXTRA_unix :=
LINK_LIBS_unix   := -ldl -lpthread

# Shared-module extension and the flags that name one. PE has no SONAME; the
# loader finds a DLL by name in the module's own directory.
MOD_EXT       := .so
MOD_RPATH     := -Wl,-rpath,'$$ORIGIN'

# Everything is in one .so now, so the engines' and the core's symbols share a
# link namespace. The rename machinery (sdl-vendored.h, ons-/pons-symbol-prefix.h)
# is what makes that possible, and it is also what makes the whole core safe to
# compile with -fvisibility=hidden: the only symbols that must stay visible are
# the retro_* entry points (RETRO_API in libretro.h already marks them) and the
# few the engines resolve by name, which VNOVEL_EXPORT marks.
#
# This replaces link.T. A version script does the same job on ELF but does not
# exist on PE/COFF, so Windows silently exported everything; a source-level
# attribute is one mechanism for every platform.
VISIBILITY     := -fvisibility=hidden
SINGLE_LDFLAGS = $(LDFLAGS) $(KRKR_SYS_LIBS)

# Locate nothing beside the core: there are no sibling modules any more. The
# rpath is kept for the vendored font directory and any plugin the overrides
# table names, both of which are resolved relative to the core by the frontend.
RPATH_ORIGIN := -Wl,-rpath,'$$ORIGIN'

# pkg-config(1) to query. Overridable so a cross build can point at the target's
# pkg-config (buildroot exports PKG_CONFIG): the host's would answer with host
# include paths, which the cross compiler rejects as unsafe, and with host
# library paths, which silently link the host's freetype.
PKG_CONFIG ?= pkg-config

ifeq ($(platform), unix)
   TARGET_EXT := .so
   fpic    := -fPIC
   CXXFLAGS += $(shell $(PKG_CONFIG) --cflags freetype2 2>/dev/null)
   CFLAGS   += $(shell $(PKG_CONFIG) --cflags freetype2 2>/dev/null)
   SHARED  := -shared -Wl,--no-undefined
   LINK_EXTRA := $(LINK_EXTRA_unix)
   LDFLAGS += $(LINK_LIBS_unix)

else ifeq ($(platform), aarch64)
   CROSS   := aarch64-linux-gnu-
   TARGET_EXT := .so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined
   LINK_EXTRA := $(LINK_EXTRA_unix)
   LDFLAGS += $(LINK_LIBS_unix)
   override CC := $(CROSS)gcc
   override CXX := $(CROSS)g++ -std=c++11
   AR      := $(CROSS)ar
   OBJCOPY := $(CROSS)objcopy
   NM      := $(CROSS)nm
   # pkg-config must answer for the TARGET, not the host. A bare pkg-config
   # reports the host's paths, and the host's /usr/include puts its
   # bits/wordsize.h ahead of the target's, making intptr_t a 32-bit type on a
   # 64-bit target: the engine then rejects its own pointer casts with
   #   cast from 'void*' to 'tjs_intptr_t' {aka 'int'} loses precision
   # PKG_CONFIG_SYSROOT_DIR prefixes every reported path with the sysroot and
   # PKG_CONFIG_LIBDIR replaces the search path, so both the include and the
   # library flags name the target. CMake's FindPkgConfig and the $(shell)
   # calls below inherit both from the environment.
   #
   # Two layouts exist, and they need different treatment:
   #
   #  * A self-contained cross toolchain (buildroot and friends) keeps its .pc
   #    files inside the sysroot with paths relative to it. PKG_CONFIG_SYSROOT_DIR
   #    is what prefixes every reported path so it names the target.
   #  * A Debian/Ubuntu multiarch host keeps the arm64 .pc files outside the
   #    sysroot at /usr/lib/<triplet>/pkgconfig, with already-absolute paths that
   #    the cross compiler resolves correctly (it searches /usr/include/<triplet>
   #    ahead of the shared /usr/include). A sysroot prefix there would double
   #    every path, and /usr/lib/pkgconfig must not be searched: it holds the
   #    host-arch .pc files whose paths are the problem in the first place.
   #
   # The sysroot's own pkgconfig directory decides which. Nothing is set when
   # neither layout is present — a bare pkg-config is left to its defaults rather
   # than pointed at a directory that cannot hold the packages. A pkg-config
   # given by path is a caller's own and is never touched (buildroot passes one
   # that already knows its sysroot, and its recipe documents that overriding
   # these variables breaks it).
   empty :=
   space := $(empty) $(empty)
   AARCH64_TRIPLET := $(patsubst %-,%,$(CROSS))
   AARCH64_SYSROOT ?= $(shell $(CROSS)gcc -print-sysroot)
   AARCH64_SYSROOT_PC := $(wildcard $(AARCH64_SYSROOT)/usr/lib/pkgconfig $(AARCH64_SYSROOT)/usr/share/pkgconfig)
   AARCH64_MULTIARCH_PC := $(wildcard /usr/lib/$(AARCH64_TRIPLET)/pkgconfig)
   AARCH64_PKG_DIRS := $(strip $(AARCH64_SYSROOT_PC) $(AARCH64_MULTIARCH_PC) $(if $(or $(AARCH64_SYSROOT_PC),$(AARCH64_MULTIARCH_PC)),/usr/share/pkgconfig))
   AARCH64_PKG_LIBDIR := $(subst $(space),:,$(AARCH64_PKG_DIRS))
   AARCH64_PKG_ENV ?= $(if $(findstring /,$(PKG_CONFIG)),,$(if $(AARCH64_PKG_LIBDIR),$(if $(AARCH64_SYSROOT_PC),PKG_CONFIG_SYSROOT_DIR=$(AARCH64_SYSROOT) )PKG_CONFIG_LIBDIR=$(AARCH64_PKG_LIBDIR)))
   KRKR_CMAKE_ENV := $(AARCH64_PKG_ENV)
   # freetype must be the target's, not the host's: a bare -lfreetype links
   # whatever the linker finds first, which on a multiarch host is the amd64
   # library, and once the sysroot only carries arm64 the link fails with
   # "cannot find -lfreetype".
   AARCH64_FT_CFLAGS := $(shell $(AARCH64_PKG_ENV) $(PKG_CONFIG) --cflags freetype2 2>/dev/null)
   AARCH64_FT_LIBS   := $(shell $(AARCH64_PKG_ENV) $(PKG_CONFIG) --libs freetype2 2>/dev/null)
   LDFLAGS  += $(if $(AARCH64_FT_LIBS),$(AARCH64_FT_LIBS),-lfreetype)
   CXXFLAGS += $(AARCH64_FT_CFLAGS)
   CFLAGS   += $(AARCH64_FT_CFLAGS)
else ifeq ($(platform), mingw)
   # Overridable so the same block builds x86_64 and i686 (win32):
   #   make platform=mingw CROSS=i686-w64-mingw32-
   CROSS    ?= x86_64-w64-mingw32-
   TARGET_EXT := .dll
   MOD_EXT   := .dll
   MOD_RPATH :=
   # No --version-script: PE/COFF does not support it. It is not needed either —
   # RETRO_API expands to __declspec(dllexport) on Windows, so exactly the
   # retro_* entry points are exported and everything else stays private.
   # -fvisibility=hidden is likewise ignored by PE; the export attribute is what
   # governs there, which is why VISIBILITY is applied on ELF only.
   SHARED    := -shared -static-libgcc -static-libstdc++ -s -Wl,--no-undefined
   override CC  := $(CROSS)gcc
   override CXX := $(CROSS)g++ -std=c++11
   AR        := $(CROSS)ar
   PLATFLAGS := -DWIN32 -DFAKE_RECURSIVE_MUTEX=0 -Wno-narrowing
   SDL_SYSTIMER := win32/SDL_systimer.c
   # The engine's CMake build must be told to cross-compile: without a toolchain
   # file it configures for the HOST and produces host-arch archives, which
   # later fail the link with a wall of architecture errors.
   #
   # Generated by tools/build-mingw-vorbis.sh into build/ (gitignored) rather
   # than living in /tmp, which a reboot wipes. CMAKE_SYSTEM_PROCESSOR must be
   # "amd64": the engine's CMake gates SSE2/AVX2 and the DirectShow path on that
   # exact value.
   KRKR_MINGW_DEPS   := $(CURDIR)/build/mingw-deps
   KRKR_CMAKE_TOOLCHAIN := $(KRKR_MINGW_DEPS)/mingw-toolchain.cmake
   # The static vorbis/ogg the engine links on Windows; built by
   # tools/build-mingw-vorbis.sh, consumed by the engine's own CMake.
   KRKR_CMAKE_VORBIS := -DKRKRSDL2_MINGW_VORBIS_PREFIX=$(KRKR_MINGW_DEPS)/vorbis
   # The engine resolves FreeType with pkg_check_modules(), and CMake otherwise
   # runs the HOST pkg-config — which reports /usr/include and /usr/lib, so the
   # cross build silently mixes glibc headers into the mingw compile and dies on
   # conflicting stdlib declarations (putenv, at_quick_exit). Point CMake at the
   # cross pkg-config so only target paths are reported.
   # Resolved to an absolute path first: CMake stores a bare name by joining it
   # to the build directory, producing a path that does not exist, and then
   # FreeType detection silently comes up empty (ft2build.h not found).
   KRKR_CMAKE_PKGCONFIG := -DPKG_CONFIG_EXECUTABLE=$(shell which $(CROSS)pkg-config)
   # What the engine needs on top of the core's own objects. On Linux these are
   # shared libraries from the sysroot; on Windows they must be the engine's own
   # static SDL2, the statically built vorbis/ogg, and the Windows system
   # libraries the engine references. This list mirrors the engine's own
   # CMakeFiles/krkrsdl2.dir/linkLibs.rsp — the authoritative closure — and
   # deliberately has no fontconfig: the engine's mingw build does not use it.
   #
   # SDL2 is named by path, not -lSDL2: krkrsdl2 vendors its SDL2 and builds it
   # as a static archive under its own build directory, and there is no SDL2
   # import library in the mingw sysroot.
   # Lazy (=, not :=): KRKR_BUILD_DIR is defined further down this file, so an
   # immediate expansion here captures an empty value and the linker is handed
   # "/external/SDL/libSDL2.a".
   KRKR_MINGW_SDL2 = $(KRKR_BUILD_DIR)/external/SDL/libSDL2.a
   KRKR_SYS_LIBS = $(KRKR_MINGW_SDL2) \
                    -L$(KRKR_MINGW_DEPS)/vorbis/lib -lvorbisfile -lvorbis -logg \
                    -lwinpthread -lz \
                    $(MINGW_TRIPLET_STATIC_LIBS) \
                    -luuid -lmpr -ldmoguids -lmfplat -lstrmiids -lmf \
                    -lpropsys -lmfuuid -lamstrmid -ldxguid -lwindowscodecs \
                    -lquartz \
                    -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 \
                    -lversion -ladvapi32 -lsetupapi -lshell32 -lm
   LDFLAGS   += -lwinmm -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
   # The development socket (src/devsock.c) uses Winsock on Windows, and winsock
   # is not in the default mingw link set.
   LDFLAGS   += -lws2_32
   # Everything is linked dynamically on Windows: a mingw-w64 distribution ships
   # these as import libraries rather than static archives, so a static link
   # cannot be relied on to resolve. Only what is not already compiled into the
   # core is taken from the toolchain — bzip2, ogg, vorbis, libmad and SMPEG are
   # built from source, so linking the system -lbz2 as well would collide
   # (multiple definition of BZ2_bzRead*).
   SINGLE_LDFLAGS += -lwinpthread -lz -lbrotlidec -lbrotlicommon
   # freetype comes from the mingw cross package, so ask the *cross* pkg-config:
   # the native one reports host paths and the include never resolves.
   MINGW_PKGCONFIG ?= $(CROSS)pkg-config
   FT_CFLAGS := $(shell $(MINGW_PKGCONFIG) --cflags freetype2 2>/dev/null)
   # --static, and the archive named by path rather than -lfreetype: the plain
   # form names libfreetype.dll.a, so the DLL would import libfreetype-6.dll and
   # the package would have to ship it. libfreetype.a is present in the same
   # sysroot, so naming it explicitly keeps the DLL self-contained, which is the
   # whole point of this build. -Wl,-Bstatic around -lfreetype is not enough on
   # its own: the engine's SDL2/vorbis section later re-enters -Bdynamic and ld
   # resolves left to right, so the first, static binding is what must win.
   # $(CROSS) carries a trailing dash ("x86_64-w64-mingw32-"), so it cannot be
   # concatenated into a path: /usr/$(CROSS)lib becomes ...mingw32-lib.
   MINGW_TRIPLET := $(patsubst %-,%,$(CROSS))
   MINGW_SYSROOT := /usr/$(MINGW_TRIPLET)/lib
   # Libraries selected by absolute path rather than -lNAME.
   #
   # The sysroot carries both an import library (libNAME.dll.a) and a static
   # archive (libNAME.a) for several of these, and -lNAME prefers the import
   # library — so the DLL would import libbrotlidec.dll / libfreetype-6.dll and
   # the package would have to ship them, defeating the self-contained build.
   # Naming the archive is unambiguous and is the only form that works without
   # wrapping the whole link line in -Bstatic (which is impossible here: the
   # compiler runtime must stay dynamic, and ld matches left to right).
   MINGW_STATIC_LIB = $(if $(wildcard $(MINGW_SYSROOT)/lib$(1).a),$(MINGW_SYSROOT)/lib$(1).a,-l$(1))
   MINGW_TRIPLET_STATIC_LIBS := $(call MINGW_STATIC_LIB,bz2) \
                                $(call MINGW_STATIC_LIB,brotlidec) \
                                $(call MINGW_STATIC_LIB,brotlicommon)
   MINGW_FT_STATIC := $(call MINGW_STATIC_LIB,freetype)
   # pkg-config repeats -lbz2/-lbrotli*, which would re-select the import
   # libraries after the static archives above. Keep only what is not already
   # named by path (-lz and anything else it may add).
   FT_LIBS   := $(MINGW_FT_STATIC) \
                $(shell PKG_CONFIG_LIBDIR=/usr/$(MINGW_SYSROOT)/pkgconfig \
                          $(MINGW_PKGCONFIG) --libs --static freetype2 2>/dev/null \
                          | tr ' ' '\n' \
                          | grep -vE -- '-lfreetype|-lbz2|-lbrotlidec|-lbrotlicommon' \
                          | tr '\n' ' ')
   CXXFLAGS += $(FT_CFLAGS)
   CFLAGS   += $(FT_CFLAGS)
   LDFLAGS  += $(FT_LIBS)

else ifeq ($(platform), win)
   # Native Windows (MSVC not supported; use mingw platform for cross-compile)
   TARGET_EXT := .dll
   SHARED    := -shared -static-libgcc -static-libstdc++ -s -Wl,--no-undefined
   PLATFLAGS := -DWIN32 -DFAKE_RECURSIVE_MUTEX=0 -Wno-narrowing
   SDL_SYSTIMER := win32/SDL_systimer.c
   LDFLAGS   += -lwinmm -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
   # The development socket (src/devsock.c) uses Winsock on Windows.
   LDFLAGS   += -lws2_32

else ifneq (,$(findstring osx,$(platform)))
   TARGET_EXT := .dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib

else ifeq ($(platform), emscripten)
   TARGET_EXT := .bc
   TARGET_STEM_EXTRA := _emscripten
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined

else
   # Fallback: treat as unix-like
   TARGET_EXT := .so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined
endif

# Optimization / debug
ifeq ($(DEBUG), 1)
   OPTFLAGS := -O0 -g3 -DDEBUG
else
   OPTFLAGS := -O3
endif

# Global defines
DEFINES := -D__LIBRETRO__ -DUSE_OGG_VORBIS -D_FILE_OFFSET_BITS=64 -DLIBRETRO_FRAME_SYNC

# Build stamp: the deployed .so carries this string, so `strings` identifies the
# exact build (see VNOVEL_LIBRETRO_VERSION in libretro.cpp). A git
# abbreviation is not fixed-width and "-dirty" adds six chars, so truncate hard
# to the 15 the [16] buffer allows.
#
# Only trust `git describe` when the toplevel IS this tree. Buildroot copies the
# sources to an out-of-tree build dir with no .git of its own, where a plain
# `git -C` would walk up into whatever repo encloses it (the Recalbox tree) and
# stamp a foreign hash — worse than no stamp, because it looks valid. Fall back
# to "dev" there; the package recipe passes the real hash via BUILD_VERSION.
#
# NB: test _TOP for emptiness explicitly. `$(abspath )` of an empty value is the
# current directory, which would compare equal to $(abspath $(CORE_DIR)) for an
# out-of-tree build and wrongly select the git branch.
_TOP := $(shell git -C $(CORE_DIR) rev-parse --show-toplevel 2>/dev/null)
ifneq ($(_TOP),)
ifeq ($(abspath $(_TOP)),$(abspath $(CORE_DIR)))
_TREE_VERSION := $(shell git -C $(CORE_DIR) describe --always --dirty 2>/dev/null)
endif
endif
BUILD_VERSION ?= $(if $(_TREE_VERSION),$(_TREE_VERSION),dev)
BUILD_VERSION := $(shell printf '%s' '$(BUILD_VERSION)' | cut -c1-15)
DEFINES += -DBUILD_VERSION='"$(BUILD_VERSION)"'

# The development wire protocol (the text stream out, navigation intents in).
# Always compiled in: it is what every harness, client and test drives the core
# through.
DEFINES += -DVNOVEL_DEVWIRE

# There is one layout: every engine is linked into the core, so the KiriKiri
# ABI wrapper resolves the engine's symbols by direct reference rather than
# through a module handle. See krkr_sym in engine_kirikiri_cabi.cpp.
DEFINES += -DKRKR_STATIC_ENGINE

# KiriKiri is compiled in unconditionally: the engine is statically linked into
# the core (no runtime dlopen; see engine_kirikiri_cabi.cpp), so a core without it
# cannot run a .xp3 game.
DEFINES += -DVNOVEL_KIRIKIRI

# Frame synchronization: remove -DLIBRETRO_FRAME_SYNC to disable 1:1 frame sync.

# Build directory (per-platform, per-build-type).
#
# Debug and release must not share a directory: cmake caches its configuration
# from the first `cmake -S/-B` and ignores a later flag change, so `make debug`
# then `make` would leave the debug objects in place — -O0, and asserts live. A
# stale assert in vendored FAudio aborted a routine call (a sound during a save)
# in a build believed to be release.
ifeq ($(KRKR_DEBUG),1)
   BUILD_TAG := -debug
else
   BUILD_TAG :=
endif
BUILD_DIR := build/$(platform)$(BUILD_TAG)

# Tool defaults (overridden by platform blocks above)
OBJCOPY ?= objcopy
NM      ?= nm

CXXFLAGS += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) $(VISIBILITY)
CFLAGS   += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall $(fpic) $(VISIBILITY)

# The output name
# Composed after every platform block, because those blocks set the extension:
# a platform block that assigns `TARGET` wholesale discards this suffix.
TARGET := $(TARGET_NAME)_libretro$(TARGET_STEM_EXTRA)$(TARGET_EXT)

TARGET_BUILD := $(BUILD_DIR)/$(TARGET)

# Source files and includes
include Makefile.common

# The KiriKiri engine
#
# The engine comes from deps/krkrsdl2 and is a CMake project, not part of this
# Makefile's source lists. Its two static archives are what the core links:
#
#   libkrkrsdl2common.a   the engine proper
#   libFAudio.a           its audio layer, a separate CMake library
#
# plus SDL2/fontconfig/vorbisfile, which the engine's own link.txt names.
#
# The archives are BUILT here rather than assumed. Assuming them was a silent
# failure: a tree without them still linked, just without KiriKiri, and the only
# sign was a game reporting no engine — which is indistinguishable from a game
# whose engine genuinely is missing. Building them costs a no-op whenever they
# are already current.
KRKR_DIR := $(DEPS_DIR)/krkrsdl2
# The engine's host-facing presentation contract (visual/Present.h) is a pure-C
# header with no engine types in it, which is why the core can include it
# directly while declaring every other engine symbol by hand. Added here rather
# than in Makefile.common because KRKR_DIR is not defined until this point.
CXXFLAGS += -I$(KRKR_DIR)/src/core/visual
# Per-platform, like BUILD_DIR: a shared CMake tree would hand the linker the
# previous platform's objects ("file in wrong format"), and cmake caches the
# compiler from its first configure, so the platforms cannot share one.
#
# Tagged by build type as well, for the same reason as BUILD_DIR above: one
# shared tree means `make debug` and `make` overwrite each other's cmake cache,
# and the second build silently keeps the first's flags.
KRKR_BUILD_DIR := $(KRKR_DIR)/build-$(platform)$(BUILD_TAG)
KRKR_ARCHIVE := $(BUILD_DIR)/krkrsdl2common.filtered.a

# Where the two archives come from. Normally this Makefile builds them (below).
#
# A buildroot build sets this instead: there the engine is its own package with
# its own per-package sysroot, and configuring its CMake from here reads an
# sdl2.pc whose absolute paths were stamped for that other package — cmake then
# rejects the imported SDL2 target and the build dies in a way that looks like a
# broken install. The engine package therefore builds the archives and tells us
# where they are, which is also the ordinary buildroot split: one package builds
# a thing, another consumes it.
KRKR_PREBUILT_DIR ?=
ifeq ($(KRKR_PREBUILT_DIR),)
   KRKR_SRC_ARCHIVE := $(KRKR_BUILD_DIR)/libkrkrsdl2common.a
   KRKR_FAUDIO := $(KRKR_BUILD_DIR)/external/FAudio/libFAudio.a
else
   KRKR_SRC_ARCHIVE := $(KRKR_PREBUILT_DIR)/libkrkrsdl2common.a
   KRKR_FAUDIO := $(KRKR_PREBUILT_DIR)/libFAudio.a
endif

# The CMake options the engine needs to be a libretro engine rather than a
# standalone app: the builtin-shim registry, and KRKRSDL2_LIBRETRO (which turns
# on the extern "C" control entry points the core calls). Same set the buildroot
# package passes.
# A cross build must configure the engine for the TARGET, or the archives come
# out host-arch and the link fails with a wall of architecture errors. Callers
# that have a CMake toolchain file pass it here (buildroot does: it is what its
# own cmake-package uses); a native build leaves it empty.
KRKR_CMAKE_TOOLCHAIN ?=
# Compilers are named only when no toolchain file provides them: $(CXX) can be
# "g++ -std=c++11" here, which is not a compiler path, and cmake rejects it.
KRKR_CMAKE_COMPILERS := $(if $(KRKR_CMAKE_TOOLCHAIN),,$(if $(CROSS),-DCMAKE_C_COMPILER=$(CROSS)gcc -DCMAKE_CXX_COMPILER=$(CROSS)g++))
# Environment for the engine's configure and build, empty unless a platform
# block sets it. A cross build puts PKG_CONFIG_SYSROOT_DIR/PKG_CONFIG_LIBDIR
# here so CMake's FindPkgConfig reports target paths (see the aarch64 block).
KRKR_CMAKE_ENV ?=
# -fvisibility=hidden keeps the core's own symbols out of the dynamic table (see
# the long note at the CMAKE flags below), and takes every vendored codec's with
# it. `-fvisibility-inlines-hidden` is separate and is what makes small in-header
# accessors invisible: it hides an inline function even when -fno-inline kept a
# real out-of-line copy of it, so nm reports nothing and a gdb call into one is
# impossible.
#
# That is the one flag a debug build must drop. -fno-inline alone is not enough:
# a method declared in a header is emitted as an inline symbol, and this option
# then strips it from the table regardless of whether the compiler inlined the
# call. Both engine declarations that bit us are of that shape
# (tTVPBaseBitmap::GetWidth/GetHeight live in BitmapIntf.h while the class is
# declared in LayerBitmapIntf.h), so their symbols are absent in an ordinary
# build and present when this is off.
#
# Overridable rather than conditional so the choice is visible at the call site:
# the debug target passes a base without it (see KRKR_DEBUG_FLAGS below).
KRKR_CXX_VIS ?= -fvisibility=hidden -fvisibility-inlines-hidden
KRKR_CXX_BASE  := $(KRKR_CXX_VIS)

# Debug flags go in the per-configuration variables, not in CMAKE_CXX_FLAGS.
# CMAKE_<LANG>_FLAGS is expanded *before* CMAKE_<LANG>_FLAGS_<CONFIG>, and the
# engine trees force -DCMAKE_BUILD_TYPE=Release, so "-g -O0 -fno-inline" placed
# in CMAKE_CXX_FLAGS is followed by "-O3 -DNDEBUG" and loses: the built objects
# then carry no .debug_info at all (verified: readelf -S on
# tjsInterCodeGen.cpp.o shows zero debug sections, and gdb resolves nothing).
# The _RELEASE variables are expanded last and therefore win.
#
# These must stay non-empty. They are passed to CMake as
# -DCMAKE_*_FLAGS_RELEASE, and a plain `make` that passes nothing re-uses
# whatever the existing CMake cache holds — so a `make` following a
# `make debug` would silently remain a debug build (-O0, asserts live) while
# appearing to be release. That is exactly how a live FAudio assert on a
# supported flag surfaced as a crash in a build believed to be release.
KRKR_CXX_RELEASE ?= -O3 -DNDEBUG
KRKR_C_RELEASE   ?= -O3 -DNDEBUG

# `make debug` — the build to drive under gdb.
#
# What is needed together, and a plain build has none of it:
#   -g -O0        so gdb can break on and resolve engine symbols at all
#   -fno-inline   so small accessors and one-line methods (SetFace, GetPoint,
#                 rect.get_width(), ...) keep a frame: inlined ones have no
#                 symbol, and a gdb command that calls into one aborts the whole
#                 script with "Cannot evaluate function -- may be inlined",
#                 which silently truncates the run.
#   probes        the always-on diagnostic probes.
#
# The flags go into CMAKE_*_FLAGS_RELEASE, which cmake expands *after*
# CMAKE_*_FLAGS. Putting them in CMAKE_CXX_FLAGS does not work: both engine
# trees force -DCMAKE_BUILD_TYPE=Release, so the trailing -O3 -DNDEBUG wins and
# -O0/-fno-inline are undone (the objects still carry -g, so the failure is
# "symbols resolve but accessors have no frames" - the exact gdb symptom the
# AGENTS.md conventions describe).
#
# This builds krkrsdl2, the engine the core links.
#
# The engine's own compile-time probes are OPTION_DEBUG_PROBES, pinned OFF in
# KRKR_CMAKE_FLAGS below; runtime diagnostics are the debug mask (VNOVEL_DEBUG,
# see src/vnovel_debug.h).
KRKR_DEBUG_FLAGS  ?= -g -O0 -fno-inline
# A debug build drops -fvisibility-inlines-hidden, so in-header accessors keep
# their symbols and can be called from gdb. Keeping only -fvisibility=hidden
# still keeps the core's exported surface to what it declares. -O0 (in the flags
# above) is what stops the compiler folding them into their callers.
KRKR_DEBUG_VIS    ?= -fvisibility=hidden
debug:
	@echo "==> debug build: $(KRKR_DEBUG_FLAGS), visibility '$(KRKR_DEBUG_VIS)'"
	$(Q)$(MAKE) platform=unix KRKR_DEBUG=1 \
		KRKR_CXX_VIS="$(KRKR_DEBUG_VIS)" \
		KRKR_CXX_RELEASE="$(KRKR_DEBUG_FLAGS)" KRKR_C_RELEASE="$(KRKR_DEBUG_FLAGS)" all

.PHONY: debug

# -fvisibility=hidden for the same reason as everything else here: the core must
# export its entry points and nothing else. Without it the engine's own symbols
# (and every vendored codec it carries) land in the dynamic symbol table, which
# is what link.T used to prevent for the rest of the core. The engine's
# deliberate exports carry their own visibility attribute and survive.
# Overridable so a cross build can point at its target sysroot. Extra CMake
# arguments go in KRKR_CMAKE_EXTRA, e.g.
#   make platform=unix KRKR_CMAKE_EXTRA="-DOPTION_DEBUG_PROBES=ON"
# Note it must be a real CMake option: appending a -D to CMAKE_CXX_FLAGS would
# only define a preprocessor symbol, which is not how the engine's options
# (OPTION_DEBUG_PROBES) are wired — those drive a generated header.
# OPTION_DEBUG_PROBES is passed EXPLICITLY, and as OFF, on every configure.
# CMake keeps a cached value otherwise: once a run passed
# KRKR_CMAKE_EXTRA="-DOPTION_DEBUG_PROBES=ON", every later build reused the
# cached ON and compiled the engine's ad-hoc stderr probes into a release
# build, with nothing in the output saying so. A stale cache is a silent
# change to the shipped engine, so the value is pinned here; an explicit
# KRKR_CMAKE_EXTRA still wins because it is passed last.
# VNOVEL_DEVWIRE must reach the ENGINE's own CMake build too, or the shim that
# publishes the records compiles its calls to no-ops and the wire protocol is
# silently half-present (the core would listen and nothing would ever arrive).
# The engines are a separate project with their own compiler invocations, so a -D
# in this Makefile's DEFINES does not reach them.
KRKR_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release -DKRKRSDL2_LIBRETRO=ON \
                    -DOPTION_DEBUG_PROBES=OFF \
                    -DVNOVEL_DEVWIRE=ON \
                    -DKRKRSDL2_BUILTIN_SHIMS=ON -DOPTION_BUILD_MACOS_BUNDLE=OFF \
                    -DCMAKE_C_FLAGS="-fvisibility=hidden" \
                    -DCMAKE_CXX_FLAGS:STRING="$(KRKR_CXX_BASE) $(KRKR_CXX_EXTRA)" \
                    $(if $(KRKR_CXX_RELEASE),-DCMAKE_CXX_FLAGS_RELEASE:STRING="$(KRKR_CXX_RELEASE)",) \
                    $(if $(KRKR_C_RELEASE),-DCMAKE_C_FLAGS_RELEASE:STRING="$(KRKR_C_RELEASE)",) \
                    $(if $(KRKR_CMAKE_TOOLCHAIN),-DCMAKE_TOOLCHAIN_FILE=$(KRKR_CMAKE_TOOLCHAIN),) \
                    $(KRKR_CMAKE_PKGCONFIG) $(KRKR_CMAKE_VORBIS) $(KRKR_CMAKE_EXTRA)

KRKR_CMAKE_TARGET := krkrsdl2common

# Parallelism for the engine's own CMake build. It is a separate knob from
# `make -j`: krkrsdl2 has a few very heavy translation units (LayerIntf,
# DrawDevice, the TJS interpreter) that can each take well over a gigabyte, so
# `--parallel` with no argument — which means "one job per core" — exhausts a
# 16 GB machine and the kernel OOM-kills the build. Overridable for a bigger
# box: `make platform=mingw KRKR_JOBS=16`.
KRKR_JOBS ?= 4

# Rebuild the engine when the tree is newer than the archives. Output is
# redirected: cmake prints hundreds of lines and this runs inside `make`. Not
# declared at all when the archives are supplied from elsewhere, so nothing
# tries to rebuild a file outside this tree.
#
# The rule also depends on the cmake flags, through a stamp file. Without that,
# changing KRKR_CMAKE_EXTRA (a probe define, a different build type) left the
# archives in place: cmake caches its configuration from the first run, so the
# new flag never reached the compiler and the build silently produced an engine
# without the probe the caller asked for. The stamp makes flag changes a real
# dependency, as the source files already are.
KRKR_FLAGS_STAMP := $(KRKR_BUILD_DIR)/.krkr-cmake-flags
KRKR_SIG_STAMP   := $(KRKR_BUILD_DIR)/.krkr-tree-signature
ifeq ($(KRKR_PREBUILT_DIR),)
# Rebuild the engine when the tree changed, detected through a signature file
# rather than by listing the tree as prerequisites. The file list was the
# original arrangement, and it does not survive the tree it is asked about:
# make splits its prerequisite list on whitespace, and krkrsdl2's vendored SDL
# ships "external/SDL/Xcode-iOS/Demos/iOS Launch Screen.storyboard", so the list
# asked make for a target named ".../Demos/iOS" and the build stopped with "No
# rule to make target". Escaping the spaces does not help (make applies escape
# processing before the split only to literal text, not to an $(shell)
# expansion), and filtering the offenders out leaves a rule that silently misses
# any future path containing a space.
#
# A signature is immune to all of that, because no path ever enters make's
# target namespace: find piped through the checksum of (name, size, mtime) is
# one value, and the rule depends on the file holding it. It is also cheaper -
# no thousands-of-entries prerequisite list for make to stat on every run.
#
# Vexing detail: the signature names files, so it is byte-sensitive to the
# `find` output order, which is why the pipeline sorts. A changed mtime alone
# rebuilds; that is the intended conservatism, since a checkout or a touch does
# not change the checksum the engine's own build would produce but does mean the
# objects are stale relative to what the developer just did.
#
# The build directories under the tree are excluded, and that is not an
# optimisation: cmake writes its output *inside* $(KRKR_DIR) (build-unix/ and
# friends), so including them makes the signature describe the build's own
# products. Every build then changes the signature, which triggers a rebuild on
# the next invocation, which changes it again - a self-perpetuating loop that
# never links. Only inputs are hashed.
KRKR_SIG_CMD := find $(KRKR_DIR) \( -name 'build' -o -name 'build-*' \) -prune -o \
	-type f -printf '%P %s %T@\n' 2>/dev/null | LC_ALL=C sort | cksum

$(KRKR_SIG_STAMP): FORCE
	@mkdir -p $(dir $@)
	@$(KRKR_SIG_CMD) > $@.new 2>/dev/null || echo "none" > $@.new
	@if ! cmp -s $@.new $@ 2>/dev/null; then \
		mv $@.new $@; \
		echo "note: KiriKiri tree changed, rebuilding the engine"; \
	else \
		rm -f $@.new; \
	fi

$(KRKR_SRC_ARCHIVE): $(KRKR_SIG_STAMP) $(KRKR_DIR)/CMakeLists.txt $(KRKR_FLAGS_STAMP)
	@$(if $(Q),echo "CMAKE $(KRKR_DIR)",)
	$(Q)$(KRKR_CMAKE_ENV) cmake -S $(KRKR_DIR) -B $(KRKR_BUILD_DIR) $(KRKR_CMAKE_FLAGS) $(KRKR_CMAKE_COMPILERS) >/dev/null
	$(Q)$(KRKR_CMAKE_ENV) cmake --build $(KRKR_BUILD_DIR) --target $(KRKR_CMAKE_TARGET) --parallel $(KRKR_JOBS) >/dev/null

# Rewrite the stamp only when the flags actually change. `cmp` against the
# existing contents keeps an unchanged build from reconfiguring, and touching
# the file only on a real change makes it a genuine dependency for the archive
# rule above. KRKR_CMAKE_ENV is part of the stamp: it decides where CMake's
# FindPkgConfig looks, and a cross build that changed sysroot must reconfigure.
$(KRKR_FLAGS_STAMP): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(KRKR_CMAKE_ENV) $(KRKR_CMAKE_FLAGS) $(KRKR_CMAKE_COMPILERS) $(KRKR_CMAKE_TARGET)' > $@.new
	@if ! cmp -s $@.new $@ 2>/dev/null; then \
		mv $@.new $@; \
		echo "note: KiriKiri cmake flags changed, reconfiguring the engine"; \
	else \
		rm -f $@.new; \
	fi

.PHONY: FORCE
FORCE:

# FAudio is a dependency of krkrsdl2common's configure, so one rule suffices.
$(KRKR_FAUDIO): $(KRKR_SRC_ARCHIVE)
	@:
endif

# Filter krkrsdl2's archive so its vendored libogg does not collide with ours.
# Whole-archived at link time, because the engine reaches its own objects
# through a registry rather than through references the linker can follow, so a
# plain archive member would be dropped.
KRKR_DEPS := $(KRKR_ARCHIVE) $(KRKR_FAUDIO)
ifeq ($(KRKR_FAUDIO),)
KRKR_LINK := -Wl,--whole-archive $(KRKR_ARCHIVE) -Wl,--no-whole-archive
else
KRKR_LINK := -Wl,--whole-archive $(KRKR_ARCHIVE) -Wl,--no-whole-archive $(KRKR_FAUDIO)
endif
# What the engine needs beyond what the core already links. The core links only
# the vendored SDL 1.2 (renamed to PrivateSDL_*) plus freetype; the engine needs
# SDL2, fontconfig, freetype and vorbisfile on top. This mirrors the engine's own
# link.txt, minus freetype which LDFLAGS already carries. Overridable so a cross
# build can point at its target sysroot.
KRKR_SYS_LIBS ?= -lSDL2 -lfontconfig -lvorbisfile -lvorbis

# Object files (all in BUILD_DIR, preserving source subdirectory structure)
# Per-module lists are defined in the "Shared modules" section below, after
# Makefile.common has populated the source lists.

ONS_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.ons.o,$(ONS_CXX))
PONS_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.pons.o,$(PONS_CXX)) \
             $(patsubst %.c,$(BUILD_DIR)/%.pons.o,$(PONS_C))

# Vendored fallback font (stb_truetype has no fontconfig fallback); embedded as
# a C header at build time. The TTF + its licence live in resources/fonts/.
#
# Mona is the CJK fallback here and in the KiriKiri engine, so all three engines
# render Japanese identically. It is a metric-compatible MS PGothic clone
# (Shinonome-derived, effectively public domain — MONA-LICENSE.txt), which suits
# this use better than the previous Latin-only FiraSans: it carries the CJK
# codepoints the game fonts lack, and its fixed-pitch Latin metrics match the
# half-width cells ONScripter lays text out in.
FALLBACK_FONT      := resources/fonts/mona.ttf
FALLBACK_FONT_HDR  := resources/fonts/fallback_font.h
# Secondary fallback, tried when the primary lacks a codepoint. Mona is a CJK
# clone (no Latin-1 accents); FiraSans covers Latin. Between them every codepoint
# these games use resolves.
EXTRA_FALLBACK_FONT     := resources/fonts/FiraSans-Regular.ttf
EXTRA_FALLBACK_FONT_HDR := resources/fonts/extra_fallback_font.h

# One artifact, linked straight into the repo root where the start-* scripts
# and install.sh expect it ($(TARGET) resolves relative to $(CORE_DIR)).
all: $(TARGET)

# Embedded fallback fonts
# Generate a C header from each vendored TTF at build time (the fonts stay
# normal files in resources/fonts/, with their licences alongside).
$(FALLBACK_FONT_HDR): $(FALLBACK_FONT)
	@echo "EMBED $(FALLBACK_FONT)"
	$(Q)python3 tools/embed_font.py $(FALLBACK_FONT) $@

$(EXTRA_FALLBACK_FONT_HDR): $(EXTRA_FALLBACK_FONT)
	@echo "EMBED $(EXTRA_FALLBACK_FONT)"
	$(Q)python3 tools/embed_font.py $(EXTRA_FALLBACK_FONT) $@

# libretro.cpp includes the generated header; CORE_DIR is ".", so its object is
# build/$(platform)/./libretro.o. Make the header before it compiles (the .d
# file already tracks the header's contents after that).
$(BUILD_DIR)/./src/libretro.o: $(FALLBACK_FONT_HDR) $(EXTRA_FALLBACK_FONT_HDR)

# Include compiler-generated header dependencies (-MMD). If a header changes,
# the objects that include it rebuild. Missing .d files (first build) are
# ignored via -include.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

# Dependency symlinks (deps-overlay provides vendored libs)
DEPS_SYMLINKS := deps/bzip2 deps/libmad deps/libogg deps/libvorbis deps/sdl deps/sdl-mixer deps/libretro-common


# libretro database
# Regenerates vnovel.rdb (the database the core's .info names) from the
# clrmamepro DAT. `make rdb` after editing dat/vnovel.dat.
DB_DAT := dat/vnovel.dat
DB_RDB := vnovel.rdb


# The core
#
# One .so: the core, the runtime, both ONScripter engines and the KiriKiri
# engine. This is the shape a distribution wants — libretro's buildbot copies
# exactly one file per core, so a core that needs neighbours beside it cannot be
# shipped that way — and it is the only layout this Makefile builds.
#
# The pieces share one link namespace because the renames already removed what
# collided:
#
#   * ONScripter-EN and PONScripter define the same class names (ScriptHandler,
#     DirectReader, ...), so their internals are renamed apart by
#     ons-/pons-symbol-prefix.h (see tools/gen-engine-symbol-prefix.py).
#   * KiriKiri's C++ namespaces share nothing with ONScripter's: measured 0
#     collisions, so nothing else needs renaming.
#   * Its vendored libogg duplicates ours — 71 symbols, the only C collision —
#     and is dropped from its archive by tools/filter-krkr-archive.sh. The two
#     liboggs have an identical public API and ABI, so one copy serves both.
#
# The KiriKiri engine is linked in, so engine_kirikiri_cabi.cpp resolves its
# symbols by direct reference rather than by dlsym. There is no module to load;
# a tree without the KiriKiri build still produces a working two-engine core,
# and the KiriKiri path reports the engine absent.

RUNTIME_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(RUNTIME_C)) \
                $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(RUNTIME_CXX))
CORE_OBJS    := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CORE_CXX))
ONS_MOD_OBJS := $(ONS_OBJS) $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ONS_EXTRA_CXX))
PONS_MOD_OBJS:= $(PONS_OBJS) $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PONS_EXTRA_CXX))

# The link is in the build directory and the result copied to the repository
# root, which is the path the start-* scripts, tests, install.sh, ci/package.sh
# and buildroot's recipe read. Linking every platform straight to the root copy
# would make the last build of any platform the one that ships, so a stale
# artifact from another platform would look current. The copy runs only after a
# successful link, so a failed build leaves the previous artifact in place.
$(TARGET_BUILD): $(FALLBACK_FONT_HDR) $(EXTRA_FALLBACK_FONT_HDR) $(CORE_OBJS) $(RUNTIME_OBJS) $(ONS_MOD_OBJS) $(PONS_MOD_OBJS) $(KRKR_DEPS)
	@$(if $(Q),echo "LD $@",)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(fpic) $(SHARED) $(LINK_EXTRA) -o $@ \
		$(CORE_OBJS) $(RUNTIME_OBJS) $(ONS_MOD_OBJS) $(PONS_MOD_OBJS) \
		$(KRKR_LINK) $(MOD_RPATH) $(SINGLE_LDFLAGS)

$(TARGET): $(TARGET_BUILD)
	@$(if $(Q),echo "CP $(TARGET)",)
	$(Q)cp $(TARGET_BUILD) $@

# Filter krkrsdl2's archive so its vendored libogg does not collide with ours.
$(KRKR_ARCHIVE): $(KRKR_SRC_ARCHIVE) tools/filter-krkr-archive.sh
	@$(if $(Q),echo "FILTER $(notdir $<)",)
	$(Q)mkdir -p $(dir $@)
	$(Q)sh tools/filter-krkr-archive.sh $< $@

# Generic compile rules (output to BUILD_DIR, preserving subdirs)
# -MMD -MP emits a .d per object listing its included headers; those are
# -included below, so editing any header rebuilds every object that uses it.
DEPFLAGS := -MMD -MP

$(BUILD_DIR)/%.o: %.c
	@$(if $(Q),echo "CC $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.cpp
	@$(if $(Q),echo "CXX $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# ONScripter-EN / PONScripter objects (hidden visibility, separate objects)
#
# Header dependencies: without them, editing an engine header (e.g.
# PonscripterLabel.h) does not rebuild the objects that include it, leaving
# stale objects compiled against the old layout -> ABI mismatch -> corruption
# at runtime. DEPFLAGS (defined above) adds the header list to each object.
#
# Each engine's internal C++ symbols are renamed to a private prefix. The two
# are forks of one codebase and define the same class names, so without this
# they cannot share a symbol table at all; the rename is what lets all three
# engines link into one core instead of living in dlopen'd modules. See
# tools/gen-engine-symbol-prefix.py for how the name list is derived, and
# sdl-vendored.h for the same technique applied to the vendored SDL.
ONS_RENAME  := -DONS_SYMBOL_PREFIX -include $(CORE_DIR)/ons-symbol-prefix.h
PONS_RENAME := -DPON_SYMBOL_PREFIX -include $(CORE_DIR)/pons-symbol-prefix.h

$(BUILD_DIR)/%.ons.o: %.cpp
	@echo "CXX(ons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(ONS_RENAME) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

# PONScripter (hidden visibility, separate objects)
$(BUILD_DIR)/%.pons.o: %.cpp
	@echo "CXX(pons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(PONS_RENAME) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

$(BUILD_DIR)/%.pons.o: %.c
	@echo "CC(pons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(PONS_RENAME) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

# Built on demand, not part of `all`: it is regenerated only when its source
# DAT changes, and it is tracked so a clone ships a usable database.
$(DB_RDB): $(DB_DAT) tools/make-rdb.py
	@echo "RDB $@"
	$(Q)python3 tools/make-rdb.py $(DB_DAT) $@

rdb: $(DB_RDB)

# Wrap unit harness
#
# Feeds a string straight into processBreaks/findNextBreak and prints the break
# offsets. Not part of `all`: it links the engine objects, so it needs a normal
# build first. See tests/wrap/README.md for why it exists.
#
# It must compile with ONS_RENAME (the engine objects are built with it, so
# Fontinfo/ScriptHandler live under ONS_* names) and link with the whole core
# object set, because the engine calls into the runtime and the vendored SDL.
WRAP_HARNESS := tests/wrap/wrap_harness
WRAP_HARNESS_OBJ := $(BUILD_DIR)/tests/wrap/wrap_harness.o

$(WRAP_HARNESS_OBJ): tests/wrap/wrap_harness.cpp
	@echo "CXX(wrap) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(ONS_RENAME) $(DEPFLAGS) -c -o $@ $<

$(WRAP_HARNESS): $(WRAP_HARNESS_OBJ) $(CORE_OBJS) $(RUNTIME_OBJS) $(ONS_MOD_OBJS) $(PONS_MOD_OBJS) $(KRKR_DEPS)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(CXXFLAGS) -o $@ $(WRAP_HARNESS_OBJ) \
		$(CORE_OBJS) $(RUNTIME_OBJS) $(ONS_MOD_OBJS) $(PONS_MOD_OBJS) \
		$(KRKR_LINK) $(SINGLE_LDFLAGS) -lpng

wrap-harness: $(WRAP_HARNESS)

.PHONY: wrap-harness

# Unit harness for the text-plane bridge (tests/textplane/). Links only the
# bridge, so it needs no engine, no SDL and no frontend - which is the point of
# the bridge being the shared contract.
TEXTPLANE_TEST := tests/textplane/textplane_test
TEXTPLANE_TEST_OBJ := $(BUILD_DIR)/tests/textplane/textplane_test.o

$(TEXTPLANE_TEST_OBJ): tests/textplane/textplane_test.cpp
	@echo "CXX(textplane) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TEXTPLANE_TEST): $(TEXTPLANE_TEST_OBJ) $(RUNTIME_OBJS)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(CXXFLAGS) -o $@ $(TEXTPLANE_TEST_OBJ) $(RUNTIME_OBJS)

textplane-test: $(TEXTPLANE_TEST)
	@$(TEXTPLANE_TEST)

.PHONY: textplane-test

# Unit harness for the development socket's outbound backlog (src/devsock.c).
#
# Links the socket and nothing else -- no engine, no SDL, no frontend -- because
# the property under test is the ring's eviction rule, and that is reachable with
# the socket API alone (see the header of tests/devsock/devsock_test.cpp for why
# a live run cannot exercise it). VNOVEL_DEVWIRE comes from the global DEFINES,
# so src/devsock.c compiles its real bodies rather than the inline no-ops in
# devsock.h.
DEVSOCK_TEST := tests/devsock/devsock_test
DEVSOCK_TEST_OBJ := $(BUILD_DIR)/tests/devsock/devsock_test.o
DEVSOCK_SOCK_OBJ := $(BUILD_DIR)/tests/devsock/devsock.o

$(DEVSOCK_TEST_OBJ): tests/devsock/devsock_test.cpp
	@echo "CXX(devsock) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(DEVSOCK_SOCK_OBJ): $(SRC_DIR)/devsock.c
	@echo "CC(devsock) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(DEVSOCK_TEST): $(DEVSOCK_TEST_OBJ) $(DEVSOCK_SOCK_OBJ)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(CXXFLAGS) -o $@ $(DEVSOCK_TEST_OBJ) $(DEVSOCK_SOCK_OBJ) -lpthread

devsock-test: $(DEVSOCK_TEST)
	@$(DEVSOCK_TEST)

.PHONY: devsock-test

clean:
	rm -rf build
	rm -f $(TARGET) $(WRAP_HARNESS)
install:
	@test -n "$(DESTDIR)" || (echo "ERROR: set DESTDIR" && false)
	cp -v $(TARGET) $(DESTDIR)/
	cp -v $(TARGET_NAME)_libretro.info $(DESTDIR)/

# sdl-vendored.h is generated (tools/gen_sdl_vendored.py) but tracked: it is the
# manifest of which symbols we rename, it is reviewable in diffs, and a fresh
# clone needs it without a bootstrap build first.
#
# The generator reads the symbols the vendored objects define, but a normal
# build already applies the renames. Regenerate with the two commands below:
# the bootstrap pass compiles the vendored sources under their plain names into
# a scratch build dir, which the generator then reads.
regen-vendored:
	@echo "REGEN sdl-vendored.h (bootstrap build, plain names)"
	$(Q)rm -rf build/vendored-bootstrap
	$(Q)$(MAKE) platform=$(platform) VENDORED_BOOTSTRAP=1 BUILD_DIR=build/vendored-bootstrap \
		all >/dev/null
	$(Q)python3 tools/gen_sdl_vendored.py build/vendored-bootstrap sdl-vendored.h
	$(Q)rm -rf build/vendored-bootstrap

.PHONY: all clean install regen-vendored rdb

print-%:
	@echo '$*=$($*)'


# C ABI wrappers. Each compiles with only its own engine's include path, because
# both engines have headers of the same names (ScriptHandler.h, and so on) and
# including both paths in one command resolves the wrong one.
$(BUILD_DIR)/engine_ons_cabi.o: $(SRC_DIR)/engine_ons_cabi.cpp
	@$(if $(Q),echo "CXX ons-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(ONS_RENAME) \
		-I$(LIBRETRO_COMM)/include -I$(ONSCRIPTER_EN) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(DEPS_DIR)/libogg/include \
		-I$(DEPS_DIR)/libvorbis/include -I$(DEPS_DIR)/libmad -I$(DEPS_DIR)/bzip2 \
		-I$(CORE_DIR) -I$(SRC_DIR) -I$(SMPEG_DIR) \
		-c -o $@ $<

$(BUILD_DIR)/engine_pons_cabi.o: $(SRC_DIR)/engine_pons_cabi.cpp
	@$(if $(Q),echo "CXX pons-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(PONS_RENAME) \
		-I$(LIBRETRO_COMM)/include -I$(PONSCRIPTER) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(DEPS_DIR)/libogg/include \
		-I$(DEPS_DIR)/libvorbis/include -I$(DEPS_DIR)/libmad -I$(DEPS_DIR)/bzip2 \
		-I$(CORE_DIR) -I$(SRC_DIR) -I$(SMPEG_DIR) \
		-c -o $@ $<

$(BUILD_DIR)/engine_kirikiri_cabi.o: $(SRC_DIR)/engine_kirikiri_cabi.cpp
	@$(if $(Q),echo "CXX kirikiri-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(CORE_DIR) -I$(SRC_DIR) $(VISIBILITY) \
		-c -o $@ $<
