# =============================================================================
# ONScripter-EN Libretro Core — Makefile
#
# Build targets:
#   make                    Linux x86_64 (.so)
#   make platform=aarch64   Cross-compile for aarch64/RPi5 (.so)
#   make platform=mingw     Cross-compile for Windows x86_64 (.dll)
#   make DEBUG=1            Debug build with -O0 -g
#
# Based on the standard libretro Makefile template used by bsnes, picodrive,
# and other libretro cores. Compatible with the libretro-super buildbot.
# =============================================================================

# --- Quiet mode ---
ifneq ($(V),1)
   Q := @
endif

# --- Platform detection ---
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

# --- Core identity ---
CORE_DIR     := .

# Auto-create symlinks to vendored deps from deps-overlay
$(shell for d in bzip2 libmad libogg libvorbis sdl sdl-mixer libretro-common; do \
	[ -e $(CORE_DIR)/deps/$$d ] || ln -s ../deps-overlay/deps/$$d $(CORE_DIR)/deps/$$d 2>/dev/null; \
done)
TARGET_NAME  := onscripter-en
LIBM         := -lm

# --- Defaults (overridden by platform blocks below) ---
CC            ?= cc
CXX           ?= c++
fpic          :=
SHARED        :=
TARGET        := $(TARGET_NAME)_libretro.so
PLATFLAGS     :=
SDL_SYSTIMER  ?= unix/SDL_systimer.c

# --- Platform configuration ---
# Link-time extras for ELF/POSIX targets. -z muldefs is gone: it existed only
# because the symbol-rename machinery deliberately produced duplicate symbols,
# and the engine modules make that unnecessary.
LINK_EXTRA_unix :=
LINK_LIBS_unix   := -ldl -lpthread

# Flags for the engine modules: shared, and every symbol the module needs must
# be resolvable (--no-undefined) or the loader would fault at call time. The
# core keeps the version script (link.T) so only retro_* is exported; the
# runtime deliberately does not, since the engines resolve SDL by name there.
SHARED_NOUNDEF := -shared -Wl,--no-undefined

# Locate the sibling modules next to the core/engine rather than at their build
# path, so the four artifacts can be deployed as a set. $ORIGIN is ELF; PE has
# no equivalent (Windows searches the module's own directory first, which is the
# behaviour we want there anyway).
RPATH_ORIGIN := -Wl,-rpath,'$$ORIGIN'

ifeq ($(platform), unix)
   TARGET   := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   CXXFLAGS += $(shell pkg-config --cflags freetype2 2>/dev/null)
   CFLAGS   += $(shell pkg-config --cflags freetype2 2>/dev/null)
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LINK_EXTRA := $(LINK_EXTRA_unix)
   LDFLAGS += $(LINK_LIBS_unix)

else ifeq ($(platform), aarch64)
   CROSS   := aarch64-linux-gnu-
   TARGET   := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LINK_EXTRA := $(LINK_EXTRA_unix)
   LDFLAGS += $(LINK_LIBS_unix)
   override CC := $(CROSS)gcc
   override CXX := $(CROSS)g++ -std=c++11
   AR      := $(CROSS)ar
   OBJCOPY := $(CROSS)objcopy
   NM      := $(CROSS)nm
   LDFLAGS  += -lfreetype
   CXXFLAGS += $(shell pkg-config --cflags freetype2 2>/dev/null)
   CFLAGS   += $(shell pkg-config --cflags freetype2 2>/dev/null)
else ifeq ($(platform), mingw)
   CROSS    := x86_64-w64-mingw32-
   TARGET    := $(TARGET_NAME)_libretro.dll
   # No --version-script: PE/COFF does not support it. It is not needed either —
   # RETRO_API expands to __declspec(dllexport) on Windows, so exactly the
   # retro_* entry points are exported and everything else stays private.
   SHARED    := -shared -static-libgcc -static-libstdc++ -s -Wl,--no-undefined
   override CC  := $(CROSS)gcc
   override CXX := $(CROSS)g++ -std=c++11
   AR        := $(CROSS)ar
   PLATFLAGS := -DWIN32 -DFAKE_RECURSIVE_MUTEX=0 -Wno-narrowing
   SDL_SYSTIMER := win32/SDL_systimer.c
   LDFLAGS   += -lwinmm -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
   # freetype comes from the mingw cross package, so ask the *cross* pkg-config:
   # the native one reports host paths and the include never resolves.
   MINGW_PKGCONFIG ?= $(CROSS)pkg-config
   FT_CFLAGS := $(shell $(MINGW_PKGCONFIG) --cflags freetype2 2>/dev/null)
   FT_LIBS   := $(shell PKG_CONFIG_LIBDIR=/usr/$(CROSS)lib/pkgconfig \
                          $(MINGW_PKGCONFIG) --libs freetype2 2>/dev/null)
   CXXFLAGS += $(FT_CFLAGS)
   CFLAGS   += $(FT_CFLAGS)
   LDFLAGS  += $(FT_LIBS)

else ifeq ($(platform), win)
   # Native Windows (MSVC not supported; use mingw platform for cross-compile)
   TARGET    := $(TARGET_NAME)_libretro.dll
   SHARED    := -shared -static-libgcc -static-libstdc++ -s \
               -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   PLATFLAGS := -DWIN32 -DFAKE_RECURSIVE_MUTEX=0 -Wno-narrowing
   SDL_SYSTIMER := win32/SDL_systimer.c
   LDFLAGS   += -lwinmm -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic

else ifneq (,$(findstring osx,$(platform)))
   TARGET   := $(TARGET_NAME)_libretro.dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib

else ifeq ($(platform), emscripten)
   TARGET   := $(TARGET_NAME)_libretro_emscripten.bc
   fpic    := -fPIC
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined

else
   # Fallback: treat as unix-like
   TARGET   := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
endif

# --- Optimization / debug ---
ifeq ($(DEBUG), 1)
   OPTFLAGS := -O0 -g3 -DDEBUG
else
   OPTFLAGS := -O3
endif

# --- Global defines ---
DEFINES := -D__LIBRETRO__ -DUSE_OGG_VORBIS -D_FILE_OFFSET_BITS=64 -DLIBRETRO_FRAME_SYNC

# Build stamp: the deployed .so carries this string, so `strings` identifies the
# exact build (see ONSCRIPTER_EN_LIBRETRO_VERSION in libretro.cpp). A git
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

# Ad-hoc stderr probes (input diagnostics). `make PROBES=1` to compile them in.
ifeq ($(PROBES), 1)
   DEFINES += -DKRKR_DEBUG_PROBES
endif

# Frame synchronization: remove -DLIBRETRO_FRAME_SYNC to disable 1:1 frame sync.

# --- Build directory (per-platform to avoid object file collisions) ---
BUILD_DIR := build/$(platform)

# --- Tool defaults (overridden by platform blocks above) ---
OBJCOPY ?= objcopy
NM      ?= nm

CXXFLAGS += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic)
CFLAGS   += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall $(fpic)

# --- Source files and includes ---
include Makefile.common

# --- Object files (all in BUILD_DIR, preserving source subdirectory structure) ---
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

# Make symlinks first so .o files can find their sources.
# The build produces four artifacts that must ship together: the core plus its
# runtime and two engine modules (the core's rpath finds them beside itself).
all: $(FALLBACK_FONT_HDR) $(EXTRA_FALLBACK_FONT_HDR) $(TARGET) stage-modules install-root

# --- Embedded fallback fonts -------------------------------------------------
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
$(BUILD_DIR)/./libretro.o: $(FALLBACK_FONT_HDR) $(EXTRA_FALLBACK_FONT_HDR)

# Include compiler-generated header dependencies (-MMD). If a header changes,
# the objects that include it rebuild. Missing .d files (first build) are
# ignored via -include.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

# --- Dependency symlinks (deps-overlay provides vendored libs) ---
DEPS_SYMLINKS := deps/bzip2 deps/libmad deps/libogg deps/libvorbis deps/sdl deps/sdl-mixer deps/libretro-common


# --- Shared modules ---------------------------------------------------------
# Three shared objects, one bridge between them:
#
#   libruntime   SDL + glue + bridge + mixer + SMPEG + decoders. There must be
#                exactly one live SDL, because its event queue, surface and
#                timers are state the engine's event loop blocks in. Both
#                engines and the core resolve SDL here.
#   libengine_*  one per engine: the engine's own objects plus its C ABI wrapper.
#                Each carries its own symbol table, so the identical class names
#                (ScriptHandler, DirectReader, ...) cannot collide — which is
#                what the rename pass existed to prevent.
#   core         libretro entry points + the dlopen-based KiriKiri ABI.
#
# The engines link the runtime, and the core links all three. Nothing is
# dlopen'd here: separation of symbol tables is a link-time property, and the
# .rc/.def files below give Windows the same one-DLL-per-module layout.

RUNTIME_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(RUNTIME_C)) \
                $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(RUNTIME_CXX))
CORE_OBJS    := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CORE_CXX))
ONS_MOD_OBJS := $(ONS_OBJS) $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ONS_EXTRA_CXX))
PONS_MOD_OBJS:= $(PONS_OBJS) $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PONS_EXTRA_CXX))

# The runtime is a plain shared library with default visibility: the engines
# call into it by name (SDL_*, ons_libretro_game_clock_ms, ...), which is the
# one direction where symbol resolution is required. That is also why it is the
# runtime and not the core that holds them — the core stays hidden, per link.T.
RUNTIME_SHARED := -shared
RUNTIME_SONAME := onscripter-en_runtime

ONS_MOD   := $(BUILD_DIR)/onscripter-en_engine_ons.so
PONS_MOD  := $(BUILD_DIR)/onscripter-en_engine_pons.so
RT_MOD    := $(BUILD_DIR)/$(RUNTIME_SONAME).so

# SONAME so the modules record a name (not the build path) as their dependency,
# and the loader finds it beside them at runtime.
$(RT_MOD): $(RUNTIME_OBJS)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(RUNTIME_SHARED) -Wl,-soname,$(RUNTIME_SONAME).so -o $@ $(RUNTIME_OBJS) $(LDFLAGS)

# SONAME so the core records a name rather than the build path, and the loader
# finds the module beside itself. -fvisibility=hidden keeps the engine's internal
# symbols private; only the vtable is exported.
$(ONS_MOD): $(ONS_MOD_OBJS) $(RT_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED_NOUNDEF) -Wl,-soname,onscripter-en_engine_ons.so -o $@ $(ONS_MOD_OBJS) $(RT_MOD) $(RPATH_ORIGIN) $(LDFLAGS)

$(PONS_MOD): $(PONS_MOD_OBJS) $(RT_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED_NOUNDEF) -Wl,-soname,onscripter-en_engine_pons.so -o $@ $(PONS_MOD_OBJS) $(RT_MOD) $(RPATH_ORIGIN) $(LDFLAGS)

$(TARGET): $(FALLBACK_FONT_HDR) $(CORE_OBJS) $(RT_MOD) $(ONS_MOD) $(PONS_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED) $(LINK_EXTRA) -o $@ $(CORE_OBJS) $(RT_MOD) $(ONS_MOD) $(PONS_MOD) $(RPATH_ORIGIN) $(LDFLAGS)

# Copy the four artifacts to the repo root, where the start-* scripts load the
# core from. Replacing rather than symlinking keeps one coherent set there: the
# root previously held a core from one build and modules from another, which
# loads the wrong runtime without any error.
install-root: $(TARGET) $(RT_MOD) $(ONS_MOD) $(PONS_MOD)
	@$(if $(Q),echo "INSTALL root artifacts",)
	$(Q)cp -f $(RT_MOD) $(ONS_MOD) $(PONS_MOD) $(CORE_DIR)/

# Stage the modules next to the core so the rpath above resolves, and so a
# deploy takes the whole set rather than a core that cannot find its runtime.
stage-modules: $(TARGET) $(RT_MOD) $(ONS_MOD) $(PONS_MOD)
	@$(if $(Q),echo "STAGE modules",)
	$(Q)mkdir -p $(BUILD_DIR)/stage && cp $(RT_MOD) $(ONS_MOD) $(PONS_MOD) $(BUILD_DIR)/stage/ && cp $(TARGET) $(BUILD_DIR)/stage/

# --- Generic compile rules (output to BUILD_DIR, preserving subdirs) ---
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

# ONScripter-EN (hidden visibility, separate objects)
# Header dependencies: without these, editing an engine header (e.g.
# PonscripterLabel.h) does not rebuild the objects that include it, leaving
# stale objects compiled against the old layout -> ABI mismatch -> corruption
# at runtime. DEPFLAGS (defined above) adds the header list to each object.
$(BUILD_DIR)/%.ons.o: %.cpp
	@echo "CXX(ons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

# PONScripter (hidden visibility, separate objects)
$(BUILD_DIR)/%.pons.o: %.cpp
	@echo "CXX(pons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

$(BUILD_DIR)/%.pons.o: %.c
	@echo "CC(pons) $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -fvisibility=hidden -c -o $@ $<

clean:
	rm -rf build
	rm -f $(TARGET) $(TARGET_NAME)_runtime.so $(TARGET_NAME)_engine_ons.so $(TARGET_NAME)_engine_pons.so

install:
	@test -n "$(DESTDIR)" || (echo "ERROR: set DESTDIR" && false)
	cp -v $(TARGET) $(DESTDIR)/
	cp -v $(TARGET_NAME)_libretro.info $(DESTDIR)/

.PHONY: all clean install

print-%:
	@echo '$*=$($*)'


# C ABI wrappers. Each compiles with only its own engine's include path, because
# both engines have headers of the same names (ScriptHandler.h, and so on) and
# including both paths in one command resolves the wrong one.
$(BUILD_DIR)/engine_ons_cabi.o: engine_ons_cabi.cpp
	@$(if $(Q),echo "CXX ons-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(ONSCRIPTER_EN) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(DEPS_DIR)/libogg/include \
		-I$(DEPS_DIR)/libvorbis/include -I$(DEPS_DIR)/libmad -I$(DEPS_DIR)/bzip2 \
		-I$(CORE_DIR) -I$(SMPEG_DIR) \
		-include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

$(BUILD_DIR)/engine_pons_cabi.o: engine_pons_cabi.cpp
	@$(if $(Q),echo "CXX pons-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(PONSCRIPTER) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(DEPS_DIR)/libogg/include \
		-I$(DEPS_DIR)/libvorbis/include -I$(DEPS_DIR)/libmad -I$(DEPS_DIR)/bzip2 \
		-I$(CORE_DIR) -I$(SMPEG_DIR) \
		-include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

$(BUILD_DIR)/engine_kirikiri_cabi.o: engine_kirikiri_cabi.cpp
	@$(if $(Q),echo "CXX kirikiri-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(CORE_DIR) \
		-c -o $@ $<
