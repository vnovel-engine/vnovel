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
# Link-time extras that only apply to ELF/POSIX targets:
#   -z muldefs      the symbol-rename machinery produces duplicate symbols on
#                   purpose; ELF needs this, PE/COFF does not accept -z at all
#   -ldl -lpthread  POSIX libraries with no Windows equivalent
LINK_EXTRA_unix := -Wl,-z,muldefs
LINK_LIBS_unix   := -ldl -lpthread

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
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES_C)) \
           $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES_CXX)) \
           $(BUILD_DIR)/engine_ons_cabi.o \
           $(BUILD_DIR)/engine_pons_cabi.o \
           $(BUILD_DIR)/engine_kirikiri_cabi.o \
           $(patsubst %.S,$(BUILD_DIR)/%.o,$(SOURCES_S))

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

# Make symlinks first so .o files can find their sources
all: $(FALLBACK_FONT_HDR) $(EXTRA_FALLBACK_FONT_HDR) $(TARGET)

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


$(TARGET): $(FALLBACK_FONT_HDR) $(OBJECTS) $(BUILD_DIR)/libons_rn.a $(BUILD_DIR)/libpons_rn.a
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED) $(LINK_EXTRA) -o $@ $(OBJECTS) $(BUILD_DIR)/libons_rn.a $(BUILD_DIR)/libpons_rn.a $(LDFLAGS)

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
	rm -f $(TARGET) ons_rename.txt pons_rename.txt rename_stamp

install:
	@test -n "$(DESTDIR)" || (echo "ERROR: set DESTDIR" && false)
	cp -v $(TARGET) $(DESTDIR)/
	cp -v $(TARGET_NAME)_libretro.info $(DESTDIR)/

.PHONY: all clean install

print-%:
	@echo '$*=$($*)'


# C ABI wrappers: compile with only their engine's include path
# to avoid header name conflicts between ONScripter-EN and PONScripter
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

# Engine static libraries (avoids duplicate symbol errors)
$(BUILD_DIR)/libons.a: $(ONS_OBJS)
	@mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

$(BUILD_DIR)/libpons.a: $(PONS_OBJS)
	@mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

# Generate symbol rename maps.
# Common symbols (intersection) are renamed to prevent cross-resolution.
# Additionally, weak symbols (V/W) unique to each library are renamed
# because the linker can cross-resolve them despite different names —
# COMDAT group deduplication uses the group signature (not the symbol
# name), and objcopy doesn't rename group signatures. The rename-syms.sh
# script strips the GROUP flag from affected sections to prevent this.
rename_stamp: $(BUILD_DIR)/libons.a $(BUILD_DIR)/libpons.a
	@echo "Generating symbol rename maps..."
	@$(NM) $(BUILD_DIR)/libons.a | grep ' [TDBRVWbr]' | awk '{print $$3}' | sort -u > /tmp/ons_all.txt
	@$(NM) $(BUILD_DIR)/libpons.a | grep ' [TDBRVWbr]' | awk '{print $$3}' | sort -u > /tmp/pons_all.txt
	@comm -12 /tmp/ons_all.txt /tmp/pons_all.txt | grep -v '^$$' > /tmp/ons_common.txt
	@$(NM) $(BUILD_DIR)/libons.a | grep ' [VW] ' | awk '{print $$3}' | sort -u > /tmp/ons_weak.txt
	@$(NM) $(BUILD_DIR)/libpons.a | grep ' [VW] ' | awk '{print $$3}' | sort -u > /tmp/pons_weak.txt
	@cat /tmp/ons_common.txt /tmp/ons_weak.txt | sort -u | grep -v '^$$' | awk '{print $$0 "=ons_" $$0}' > ons_rename.txt
	@cat /tmp/ons_common.txt /tmp/pons_weak.txt | sort -u | grep -v '^$$' | awk '{print $$0 "=pons_" $$0}' > pons_rename.txt
	@touch rename_stamp


$(BUILD_DIR)/libons_rn.a: $(BUILD_DIR)/libons.a rename_stamp
	@rm -rf $(BUILD_DIR)/_ons_rn && mkdir -p $(BUILD_DIR)/_ons_rn
	cd $(BUILD_DIR)/_ons_rn && $(AR) x ../libons.a
	cd $(BUILD_DIR)/_ons_rn && sh ../../../rename-syms.sh ../../../ons_rename.txt $(OBJCOPY) *.o
	$(Q)$(AR) rcs $@ $(BUILD_DIR)/_ons_rn/*.o

$(BUILD_DIR)/libpons_rn.a: $(BUILD_DIR)/libpons.a rename_stamp
	@rm -rf $(BUILD_DIR)/_pons_rn && mkdir -p $(BUILD_DIR)/_pons_rn
	cd $(BUILD_DIR)/_pons_rn && $(AR) x ../libpons.a
	cd $(BUILD_DIR)/_pons_rn && sh ../../../rename-syms.sh ../../../pons_rename.txt $(OBJCOPY) *.o
	$(Q)$(AR) rcs $@ $(BUILD_DIR)/_pons_rn/*.o
