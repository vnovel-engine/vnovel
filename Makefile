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
TARGET_NAME  := onscripter-en
LIBM         := -lm

# --- Defaults (overridden by platform blocks below) ---
CC            ?= cc
CXX           ?= c++
AR            ?= ar
fpic          :=
SHARED        :=
TARGET        := $(TARGET_NAME)_libretro.so
PLATFLAGS     :=
SDL_SYSTIMER  ?= unix/SDL_systimer.c

# --- Platform configuration ---
ifeq ($(platform), unix)
   TARGET   := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined

else ifeq ($(platform), aarch64)
   TARGET   := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   CC      := aarch64-linux-gnu-gcc
   CXX     := aarch64-linux-gnu-g++ -std=c++11
   AR      := aarch64-linux-gnu-ar

else ifeq ($(platform), mingw)
   TARGET    := $(TARGET_NAME)_libretro.dll
   SHARED    := -shared -static-libgcc -static-libstdc++ -s \
               -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   CC        := x86_64-w64-mingw32-gcc
   CXX       := x86_64-w64-mingw32-g++ -std=c++11
   AR        := x86_64-w64-mingw32-ar
   PLATFLAGS := -DWIN32 -DFAKE_RECURSIVE_MUTEX=0 -Wno-narrowing
   SDL_SYSTIMER := win32/SDL_systimer.c
   LDFLAGS   += -lwinmm -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic

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

# Frame synchronization: remove -DLIBRETRO_FRAME_SYNC to disable 1:1 frame sync.
# When enabled, every flush() is displayed exactly once.
# When disabled, the engine runs freely, useful for debugging.

# --- Compiler flags ---
CFLAGS   += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall $(fpic)
CXXFLAGS += $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic)
LDFLAGS  += $(LIBM)

# --- Source files and includes ---
include Makefile.common

OBJECTS := $(SOURCES_C:.c=.o) $(SOURCES_CXX:.cpp=.o) engine_ons_cabi.o engine_pons_cabi.o $(SOURCES_S:.S=.o)

# --- Build rules ---
all: $(TARGET)

$(TARGET): $(OBJECTS) libons_rn.a libpons_rn.a
	@$(if $(Q), $(shell echo echo LD $@),)
	$(Q)$(CXX) $(fpic) $(SHARED) -o $@ $(OBJECTS) libons_rn.a libpons_rn.a $(LDFLAGS)

%.o: %.c
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	@$(if $(Q), $(shell echo echo CXX $<),)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

# ONScripter-EN (hidden visibility, separate objects)
%.ons.o: %.cpp
	@echo "CXX(ons) $<"
	$(Q)$(CXX) $(CXXFLAGS) -fvisibility=hidden -c -o $@ $<

# PONScripter (hidden visibility, separate objects)
%.pons.o: %.cpp
	@echo "CXX(pons) $<"
	$(Q)$(CXX) $(CXXFLAGS) -fvisibility=hidden -c -o $@ $<

%.pons.o: %.c
	@echo "CC(pons) $<"
	$(Q)$(CC) $(CFLAGS) -fvisibility=hidden -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(ONS_OBJS) $(PONS_OBJS) $(TARGET) libons.a libpons.a libons_rn.a libpons_rn.a ons_rename.txt pons_rename.txt rename_stamp
	rm -rf build_ons_rn build_pons_rn

install:
	@test -n "$(DESTDIR)" || (echo "ERROR: set DESTDIR" && false)
	cp -v $(TARGET) $(DESTDIR)/
	cp -v $(TARGET_NAME)_libretro.info $(DESTDIR)/

.PHONY: all clean install

print-%:
	@echo '$*=$($*)'


# C ABI wrappers: compile with only their engine's include path
# to avoid header name conflicts between ONScripter-EN and PONScripter
engine_ons_cabi.o: engine_ons_cabi.cpp
	@$(if $(Q), $(shell echo echo CXX(ons-cabi) $<),)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(ONSCRIPTER_EN) -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(CORE_DIR) -I$(SMPEG_DIR) \
		-include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

engine_pons_cabi.o: engine_pons_cabi.cpp
	@$(if $(Q), $(shell echo echo CXX(pons-cabi) $<),)
	$(Q)$(CXX) $(OPTFLAGS) $(DEFINES) $(PLATFLAGS) -Wall -fpermissive $(fpic) \
		-I$(LIBRETRO_COMM)/include -I$(PONSCRIPTER) -I/usr/include/freetype2 -I$(SDL_LIBRETRO) -I$(SDL_DIR)/include \
		-I$(DEPS_DIR) -I$(DEPS_DIR)/sdl-mixer -I$(CORE_DIR) -I$(SMPEG_DIR) \
		-include $(SDL_LIBRETRO)/SDL_config_libretro.h -c -o $@ $<

# Engine static libraries (avoids duplicate symbol errors)
# Engine static libraries
libons.a: $(ONS_OBJS)
	$(Q)$(AR) rcs $@ $^

libpons.a: $(PONS_OBJS)
	$(Q)$(AR) rcs $@ $^

# Generate symbol rename maps from conflicting symbols
# Renamed libraries (objcopy --redefine-syms to avoid symbol conflicts)
# Fallback: generate rename maps with a stamp file
rename_stamp: libons.a libpons.a
	@echo "Generating symbol rename maps..."
	@nm libons.a | grep ' [TDBRVWbr]' | awk '{print $$3}' | sort -u > /tmp/ons_all.txt
	@nm libpons.a | grep ' [TDBRVWbr]' | awk '{print $$3}' | sort -u > /tmp/pons_all.txt
	@comm -12 /tmp/ons_all.txt /tmp/pons_all.txt | awk '{print $$0 " ons_" $$0}' > ons_rename.txt
	@comm -12 /tmp/ons_all.txt /tmp/pons_all.txt | awk '{print $$0 " pons_" $$0}' > pons_rename.txt
	@touch rename_stamp


libons_rn.a: libons.a rename_stamp
	rm -rf build_ons_rn && mkdir build_ons_rn
	cd build_ons_rn && ar x ../libons.a
	cd build_ons_rn && for f in *.o; do objcopy --redefine-syms=../ons_rename.txt "$$f" 2>/dev/null; done
	$(Q)$(AR) rcs $@ build_ons_rn/*.o

libpons_rn.a: libpons.a rename_stamp
	rm -rf build_pons_rn && mkdir build_pons_rn
	cd build_pons_rn && ar x ../libpons.a
	cd build_pons_rn && for f in *.o; do objcopy --redefine-syms=../pons_rename.txt "$$f" 2>/dev/null; done
	$(Q)$(AR) rcs $@ build_pons_rn/*.o
