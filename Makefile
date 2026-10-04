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
SHARED_NOUNDEF = -shared $(MOD_STATIC_RT) -Wl,--no-undefined

# Shared-module extension and the flags that name one. PE has no SONAME; the
# loader finds a DLL by name in the module's own directory, which is the
# equivalent of ELF's $ORIGIN rpath.
MOD_EXT       := .so
MOD_RPATH     := -Wl,-rpath,'$$ORIGIN'
# Named once, before anything that expands it with := (a := captures the value
# at that point, so a later definition would be invisible to these).
RUNTIME_SONAME := onscripter-en_runtime

# The whole flag, not just the prefix: on PE there is no SONAME, and a
# prefix-only variable would leave the bare module name in the link line, where
# ld reads it as an input file.
SONAME_RT   := -Wl,-soname,$(RUNTIME_SONAME)$(MOD_EXT)
SONAME_ONS  := -Wl,-soname,onscripter-en_engine_ons$(MOD_EXT)
SONAME_PONS := -Wl,-soname,onscripter-en_engine_pons$(MOD_EXT)

# How the linker is told about a sibling module. On ELF the module file itself
# carries everything needed, so it is passed directly. On PE the DLL cannot be
# linked against — only its import library can, or every exported symbol reads
# as undefined — so these become the .dll.a paths.
RT_IMPLIB_FLAG :=
RT_LINK        = $(BUILD_DIR)/$(RUNTIME_SONAME)$(MOD_EXT)
ONS_LINK       = $(BUILD_DIR)/onscripter-en_engine_ons$(MOD_EXT)
PONS_LINK      = $(BUILD_DIR)/onscripter-en_engine_pons$(MOD_EXT)

# Locate the sibling modules next to the core/engine rather than at their build
# path, so the four artifacts can be deployed as a set. $ORIGIN is ELF; PE has
# no equivalent (Windows searches the module's own directory first, which is the
# behaviour we want there anyway).
#
# MOD_STATIC_RT: link the compiler runtime into each module on PE, so a deployed
# DLL does not need libgcc_s_seh-1.dll / libstdc++-6.dll beside it. Empty on
# ELF, where those are system libraries. Referenced by the recursive definitions
# above, so the platform block below can still set it.
MOD_STATIC_RT =
MOD_RT_LIBS   =
# What the module link lines pass last. Default is plain LDFLAGS; a platform
# that must link its runtime statically overrides it (see the mingw block).
comma := ,
MOD_LDFLAGS   = $(LDFLAGS) $(MOD_RT_LIBS)
# Engine modules link the runtime after their own objects — dropping it
# leaves every PrivateSDL_* / BZ2_* / ov_* reference undefined.
MOD_LDFLAGS_ENGINE = $(MOD_LDFLAGS) $(RT_LINK)
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
   MOD_EXT   := .dll
   MOD_RPATH :=
   SONAME_RT   :=
   SONAME_ONS  :=
   SONAME_PONS :=
   # PE: link the import libraries, and have each DLL emit one for the modules
   # that link against it.
   RT_IMPLIB_FLAG   = -Wl,--out-implib,$(BUILD_DIR)/lib$(RUNTIME_SONAME).dll.a
   ONS_IMPLIB_FLAG  = -Wl,--out-implib,$(BUILD_DIR)/libonscripter-en_engine_ons.dll.a
   PONS_IMPLIB_FLAG = -Wl,--out-implib,$(BUILD_DIR)/libonscripter-en_engine_pons.dll.a
   RT_LINK        = $(BUILD_DIR)/lib$(RUNTIME_SONAME).dll.a
   ONS_LINK       = $(BUILD_DIR)/libonscripter-en_engine_ons.dll.a
   PONS_LINK      = $(BUILD_DIR)/libonscripter-en_engine_pons.dll.a
   # No --version-script: PE/COFF does not support it. It is not needed either —
   # RETRO_API expands to __declspec(dllexport) on Windows, so exactly the
   # retro_* entry points are exported and everything else stays private.
   SHARED    := -shared -static-libgcc -static-libstdc++ -s -Wl,--no-undefined
   # The module rules use these; without the static runtime flags each DLL would
   # need libgcc_s_seh-1.dll / libstdc++-6.dll beside it.
   MOD_STATIC_RT = -static-libgcc -static-libstdc++
   # Keep the pthread runtime inside each DLL; otherwise every module needs
   # libwinpthread-1.dll beside it.
   # The whole non-system closure, statically, and AFTER the objects. Note
   # Makefile.common appends -lpthread -lfreetype to LDFLAGS, which lands later
   # on the command line and re-resolves both dynamically — so the static flags
   # have to come after that too. Everything after -Bstatic is static until
   # -Bdynamic restores the default.
   # Only what is not already compiled into the runtime: bzip2, ogg, vorbis,
   # libmad and SMPEG are built from source into onscripter-en_runtime, so
   # linking the system -lbz2 as well collides (multiple definition of
   # BZ2_bzRead*). freetype needs zlib and brotli, which we do not vend.
   MOD_RT_LIBS   = -Wl,-Bstatic -lwinpthread -lfreetype -lz -lbrotlidec -lbrotlicommon -Wl,-Bdynamic
   # Drop the dynamic -lpthread/-lfreetype that Makefile.common appends: ld would
   # see those first, satisfy the symbols from the import libraries, and never
   # pull in the static copies above — leaving the DLL importing them.
   # $(RT_LINK) is repeated after MOD_RT_LIBS: freetype's static archive needs
   # BZ2_bzDecompress, which lives in the runtime, and ld resolves left to right
   # — the first occurrence is already past by the time libfreetype.a is read.
   # Strip the pthread/freetype flags that Makefile.common and the mingw block
   # already put in LDFLAGS; MOD_RT_LIBS re-adds the whole closure statically
   # at the end. Leaving them would bind them dynamically first.
   # Strip, by exact token, the pthread/freetype flags LDFLAGS carries;
   # MOD_RT_LIBS re-adds the whole closure statically at the end. Any copy left
   # earlier on the line binds dynamically first and the static one is skipped.
   # $(comma) because make splits function arguments on commas, so a literal
   # -Wl,-Bstatic would parse as two arguments and never match.
   MOD_LDFLAGS   = $(filter-out -lpthread -lfreetype -lwinpthread -Wl$(comma)-Bstatic -Wl$(comma)-Bdynamic,$(LDFLAGS)) $(MOD_RT_LIBS)
   # The engine modules additionally repeat $(RT_LINK) after the static closure:
   # freetype needs BZ2_bzDecompress, which lives in the runtime, and ld resolves
   # left to right. The runtime itself must not — it would be linking its own
   # import library, which does not exist yet.
   MOD_LDFLAGS_ENGINE = $(MOD_LDFLAGS) $(RT_LINK)
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
   # PE exports nothing unless told to: without this the runtime's import
   # library is empty and the engine DLLs cannot resolve bzip2, vorbis, SMPEG
   # or the renamed SDL symbols they link against it. The runtime is internal
   # glue, not a public API, so exporting all of it is the right scope.
   RUNTIME_EXPORTS = -Wl,--export-all-symbols -Wl,--exclude-libs,ALL

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
RUNTIME_SHARED = -shared $(MOD_STATIC_RT) -Wl,-Bsymbolic-functions $(RUNTIME_EXPORTS)

ONS_MOD   := $(BUILD_DIR)/onscripter-en_engine_ons$(MOD_EXT)
PONS_MOD  := $(BUILD_DIR)/onscripter-en_engine_pons$(MOD_EXT)
RT_MOD    := $(BUILD_DIR)/$(RUNTIME_SONAME)$(MOD_EXT)

# A PE module is linked against an import library, not against the DLL itself:
# pass the .dll directly to ld and every symbol it exports is reported
# undefined. The import library is the .dll.a that -Wl,--out-implib produces.
# ELF needs no equivalent — the SONAME is enough.
$(RT_MOD): $(RUNTIME_OBJS)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(RUNTIME_SHARED) $(SONAME_RT) -o $@ $(RUNTIME_OBJS) $(RT_IMPLIB_FLAG) $(MOD_LDFLAGS)

$(ONS_MOD): $(ONS_MOD_OBJS) $(RT_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED_NOUNDEF) $(SONAME_ONS) -o $@ $(ONS_MOD_OBJS) $(ONS_IMPLIB_FLAG) $(MOD_RPATH) $(MOD_LDFLAGS_ENGINE)

$(PONS_MOD): $(PONS_MOD_OBJS) $(RT_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED_NOUNDEF) $(SONAME_PONS) -o $@ $(PONS_MOD_OBJS) $(PONS_IMPLIB_FLAG) $(MOD_RPATH) $(MOD_LDFLAGS_ENGINE)

$(TARGET): $(FALLBACK_FONT_HDR) $(CORE_OBJS) $(RT_MOD) $(ONS_MOD) $(PONS_MOD)
	@$(if $(Q),echo "LD $@",)
	$(Q)$(CXX) $(fpic) $(SHARED) $(LINK_EXTRA) -o $@ $(CORE_OBJS) $(RT_LINK) $(ONS_LINK) $(PONS_LINK) $(MOD_RPATH) $(MOD_LDFLAGS)

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

.PHONY: all clean install install-root stage-modules regen-vendored

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
		-include $(CORE_DIR)/sdl-vendored.h -include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

$(BUILD_DIR)/engine_pons_cabi.o: engine_pons_cabi.cpp
	@$(if $(Q),echo "CXX pons-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(PONSCRIPTER) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(DEPS_DIR)/libogg/include \
		-I$(DEPS_DIR)/libvorbis/include -I$(DEPS_DIR)/libmad -I$(DEPS_DIR)/bzip2 \
		-I$(CORE_DIR) -I$(SMPEG_DIR) \
		-include $(CORE_DIR)/sdl-vendored.h -include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

$(BUILD_DIR)/engine_kirikiri_cabi.o: engine_kirikiri_cabi.cpp
	@$(if $(Q),echo "CXX kirikiri-cabi $<",)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(CORE_DIR) \
		-c -o $@ $<
