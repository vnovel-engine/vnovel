# Building vnovel

This is the only build document. If another file disagrees with it, this file is
right and that file is stale.

The build produces **one self-contained file**: `vnovel_libretro.so` (`.dll` on
Windows, `.dylib` on macOS). It contains the core, the runtime, ONScripter-EN,
PONScripter and the KiriKiri engine. Nothing ships beside it. There is no
runtime `dlopen`, no engine module and no `vnovel_runtime.so`.

There are four workspaces. Pick the one you want; they do not combine.

| Workspace | Command | Result |
|---|---|---|
| Linux, native x86_64 | `make` | `vnovel_libretro.so` |
| Linux, native aarch64 | `make` on an arm64 host | `vnovel_libretro.so` |
| Windows x86_64, cross from Linux | `make platform=mingw` | `vnovel_libretro.dll` |
| macOS arm64, native | `make` on an Apple-silicon Mac | `vnovel_libretro.dylib` |
| Raspberry Pi 5 (and other Recalbox targets) | buildroot in Docker — see [Raspberry Pi 5](#raspberry-pi-5-recalbox) | an image, or the core package |

`make platform=aarch64` (a host cross-build) exists and works, but it is **not**
part of the Raspberry Pi 5 flow; buildroot cross-builds with its own toolchain.
See [Host aarch64 cross-build](#host-aarch64-cross-build-optional).

The CI is the authoritative list of what actually builds: `.github/workflows/build.yml`
(GitHub, which adds macOS) and `.gitlab-ci.yml` (GitLab, which does not).

---

## Common requirements

Every workspace needs:

- **git with submodules.** The engines are submodules. A clone without them
  fails at the first engine header.
  ```bash
  git clone --recursive https://github.com/vnovel-engine/vnovel.git
  # or, in an existing clone:
  git submodule update --init --recursive
  ```
- **A C/C++ toolchain, `make`, and `python3`.** `python3` runs at build time
  (`tools/embed_font.py` generates the fallback font header; `tools/make-rdb.py`
  generates `vnovel.rdb`).
- **`cmake`.** The KiriKiri engine is built by its own CMake project under
  `deps/krkrsdl2/`, on demand, from this Makefile.
- **`pkg-config`.** The engine resolves sdl2, freetype2, fontconfig and
  vorbisfile through it.
- **SDL2, freetype2, fontconfig and vorbisfile development packages.** The
  engine requires all four on Linux (SDL2 is `REQUIRED` in its CMake; the other
  three are used when found):

  | Distro | Packages |
  |---|---|
  | Debian/Ubuntu | `build-essential pkg-config cmake python3 libfreetype-dev libsdl2-dev libfontconfig-dev libvorbis-dev` |
  | Arch | `base-devel cmake python freetype2 sdl2 fontconfig libvorbis` |

The vendored libraries (SDL 1.2, SDL_mixer, libogg, libvorbis, libmad, bzip2,
libretro-common) need nothing installed: they are built from the submodule
`deps-overlay`, symlinked into `deps/` by the Makefile's first rule.

---

## Linux (native x86_64)

```bash
make                 # release, at -O3
make -j"$(nproc)"    # the same, in parallel
make DEBUG=1         # -O0 -g3 with -DDEBUG
```

`make` builds everything including the KiriKiri engine. The result is
`vnovel_libretro.so` in the repository root.

All three engines are in every build, and so is the development socket (the text
stream and navigation intents a client talks to). Neither is a build option:
KiriKiri is statically linked rather than dlopen'd, so a core without it could
not run a `.xp3` game, and the socket is what every harness, client and test
drives the core through. Dependencies are therefore not optional either — a host
building the core needs the KiriKiri engine's own CMake dependencies (SDL2,
fontconfig, vorbisfile) available at build time.

Useful targets:

```bash
make debug            # -g -O0 -fno-inline, for gdb (see AGENTS.md)
make rdb              # regenerate vnovel.rdb from dat/vnovel.dat
make clean            # remove build/
make devsock-test     # unit test for the development socket
make textplane-test   # unit test for the text-plane sink
make wrap-harness     # build tests/wrap/wrap_harness
make install          # install into a libretro cores directory
```

Run the test suite with `tests/run_tests.sh` (see [Testing](README.md#testing)).

### Where the artifacts land

Every object and the linked core go under `build/<platform>/`, one directory per
platform (plus `-debug` for a debug build), so builds for different targets
coexist:

```
build/unix/vnovel_libretro.so
build/mingw/vnovel_libretro.dll
build/aarch64/vnovel_libretro.so
```

The linked core is then **copied to the repository root** under its plain name
(`vnovel_libretro.so` / `.dll`), because the scripts, the tests, `install.sh`,
`ci/package.sh` and buildroot's recipe all expect it there. That root copy is the
one shared name: it is whatever you built last. Building for another platform
leaves the previous platform's artifact in its own directory untouched.

If a build seems to use the wrong architecture, check the root copy against the
per-platform one — that mismatch is exactly what per-platform directories exist
to make visible.

---

## Windows x86_64 (cross from Linux, mingw)

```bash
# 1. Once per tree: build the static vorbis/ogg and freetype the engine links,
#    and generate the mingw CMake toolchain file. Writes build/mingw-deps/
#    (gitignored).
tools/build-mingw-vorbis.sh

# 2. Then:
make platform=mingw
```

The result is `vnovel_libretro.dll` in the repository root.

### Why step 1 is separate

The Windows build must be self-contained: it links vorbis (BGM) and freetype
(fonts) statically rather than importing their DLLs. The mingw sysroot on
Debian/Ubuntu has no static vorbis and no libfreetype (Arch's does, which is why
this is only needed off Arch), so `tools/build-mingw-vorbis.sh` builds libogg,
libvorbis and libfreetype from vendored sources and installs them into
`build/mingw-deps/{vorbis,freetype}/`. It also writes
`build/mingw-deps/mingw-toolchain.cmake`, the toolchain file the engine's CMake
is configured with.

Both live under `build/` (gitignored) so they survive a reboot. A wiped prefix
does not fail loudly: the engine's configure simply reports "vorbis/freetype
not found", the build proceeds, and **every `.ogg` track is silently absent**
(and on a sysroot without freetype, the compile dies at `ft2build.h`).

The script needs `mingw-w64` (`x86_64-w64-mingw32-gcc`, `-g++`, `-windres`,
`-ar`, `-ranlib`, `-strip`, `x86_64-w64-mingw32-pkg-config`), `cmake`, `ninja`
and zlib (`libz-mingw-w64-dev`):

```bash
# Debian/Ubuntu
apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 mingw-w64-tools \
  cmake ninja-build libz-mingw-w64-dev
```

### What `make platform=mingw` needs from step 1

| Needed | Why |
|---|---|
| `build/mingw-deps/mingw-toolchain.cmake` | Without it CMake configures for the **host**: host-arch archives, and the link then fails with a wall of architecture errors. |
| `build/mingw-deps/vorbis/{include,lib}` | Without it the engine builds with no `.ogg` decoder — silently. |
| `build/mingw-deps/freetype/{include,lib}` | Without it the engine and the core find no `ft2build.h`; the engine's `pkg_check_modules(FREETYPE2)` comes up empty. Reached through `PKG_CONFIG_PATH_CUSTOM`, which the cross pkg-config wrapper prepends to its search path. |

SDL2 is **not** taken from the mingw sysroot: the engine vendors SDL2 and links
its static archive. There is deliberately no fontconfig and no bzip2/brotli on
Windows: the vendored freetype uses its internal gzip code, so neither is a
dependency.

---

## macOS (native, arm64)

```bash
brew install pkg-config cmake ninja sdl2 freetype fontconfig libvorbis
make -j"$(sysctl -n hw.ncpu)"
```

The result is `vnovel_libretro.dylib` in the repository root.

**arm64 only, so far.** A universal (`x86_64;arm64`) dylib needs the engine's
`-msse2`/`-mavx2` source flags to be applied per-architecture — today they are
attached whenever `CMAKE_SYSTEM_PROCESSOR` is `i686`/`amd64`, so a universal
build would hand those flags to the arm64 slice too. That is a change to the
`krkrsdl2` fork's CMake, not to this tree. Everything else is
architecture-neutral: the SIMD translation units are compiled through `simde`.

No engine source change is needed for macOS: the engine's own Apple paths
(`sysctl`, `libproc`, `TargetConditionals`, the macOS font directories, the
`mmap` bitmap allocator) are already there, and the core's platform-sensitive
code is guarded (`__GLIBC__` for `execinfo.h`, `SO_NOSIGPIPE` in `src/devsock.c`).

### What is different from the Linux build

| Piece | Linux | macOS |
|---|---|---|
| freetype | `-lfreetype`, headers on the default path | `pkg-config --cflags/--libs freetype2`. Homebrew's `freetype` is keg-only, so `ft2build.h` is otherwise unfindable and the build dies on `deps/ponscripter/src/font.cpp` |
| engine libs | `-lSDL2 -lfontconfig -lvorbisfile -lvorbis` | the same four through `pkg-config --libs`, because none of them is on the default library path |
| undefined symbols | `-Wl,--no-undefined` | `-Wl,-undefined,error` |
| rpath anchor | `$ORIGIN` | `@loader_path` |
| engine archives | `-Wl,--whole-archive … -Wl,--no-whole-archive` | `-Wl,-force_load,<archive>`; Darwin's `ld` has no `--whole-archive` |

If `pkg-config` cannot find the four libraries, the Makefile stops with that as
the message rather than failing later in the engine's CMake.

---

## Raspberry Pi 5 (Recalbox)

The supported path is **buildroot inside Docker**, driven from a separate
checkout. A host cross-build is not part of it.

```
~/dev/recalbox-devtools   the driver scripts and the builder image
~/dev/recalbox            a BR2_EXTERNAL tree over ~/dev/recalbox/buildroot
```

The core is built by two buildroot packages:

- **`libretro-krkrsdl2`** — cross-builds the KiriKiri engine's static archives
  (`libkrkrsdl2common.a`, `libFAudio.a`) and stages them in
  `$(STAGING_DIR)/usr/lib/vnovel/`. It depends on `host-pkgconf sdl2 freetype
  libvorbis fontconfig`.
- **`libretro-vnovel`** — the core. It links the staged archives via
  `KRKR_PREBUILT_DIR` and installs `vnovel_libretro.so` to
  `/usr/lib/libretro/`.

### Build

```bash
cd ~/dev/recalbox-devtools
docker compose up -d          # start the recalbox-builder container

./build-rpi5-recalbox-matcha.sh          # full image  -> ~/dev/recalbox/output/images/
./build-rpi5-recalbox-matcha.sh core     # just the engine package (fast feedback)
JOBS=6 ./build-rpi5-recalbox-matcha.sh   # override parallelism (default: 3/4 of nproc)
```

Or one package directly, which is the fastest loop:

```bash
docker exec recalbox-builder bash -c "cd /work && make libretro-vnovel-dirclean && make libretro-vnovel"
```

Rebuild the engine package whenever you touch its `.mk`:

```bash
docker exec recalbox-builder bash -c "cd /work && make libretro-krkrsdl2-dirclean && make libretro-vnovel"
```

### Logs and output

| What | Where |
|---|---|
| Step timings | `docker logs -f recalbox-builder` |
| Full make output | `tail -f ~/dev/recalbox/output/build/build-output.log` |
| Image | `~/dev/recalbox/output/images/recalbox/recalbox-rpi5_64.img.xz` |

Read build output from a file, not a pipe: a log survives a killed build.

```bash
docker exec recalbox-builder bash -c "cd /work && make libretro-vnovel > /work/output/build/bv.log 2>&1; echo EXIT=\$?"
grep -nE "Error|error:|FAIL" ~/dev/recalbox/output/build/bv.log
```

### Gotchas

Each of these has cost a session.

- **Never run a package build on the host.** The `output/` tree holds
  container-internal symlinks; do package builds *inside* the container.
- **`-dirclean`, never `rm -rf`.** Stamps live in the build directory, so
  deleting `output/build/<pkg>` by hand leaves buildroot convinced the package
  is built — and removing a directory under a running build half-stages
  `per-package` and fails with `.files-list.before: No such file or directory`.
- **Never build and delete concurrently.** Two builds of the same package rsync
  into the same `-custom` directory. Check first:
  ```bash
  docker exec recalbox-builder ps -eo stat,cmd | awk '$1 !~ /^Z/ && /make/ && !/awk/'
  ```
  `pgrep -f make` lies: a killed build leaves a zombie whose command line still
  matches.
- **A stale `.stamp_installed` silently skips the archive install**, and the core
  then fails to link with undefined `TVPSet*`. Check
  `ls output/host/aarch64-buildroot-linux-gnu/sysroot/usr/lib/vnovel/`; fix with
  the `-dirclean` pair above.
- **Keep the source tree small.** With `LIBRETRO_VNOVEL_OVERRIDE_SRCDIR` set,
  buildroot rsyncs the entire tree (minus `.git`). `build/` and `deps/` are the
  bulk; keep host build products out of it.
- **Never ship `startup.tjs`.** It is deleted from the target by the recipe: the
  data directory is an engine auto-path, so a `startup.tjs` there shadows the
  game's own by base name and the game runs the placeholder instead of booting.
- **The installed core is always `vnovel_libretro.so`.** A flavour-suffixed name
  once left a stale x86_64 file in that path and the image build died with an
  architecture mismatch.
- **Renames stop overrides applying.** `local.mk` uses
  `<PACKAGE>_OVERRIDE_SRCDIR`, which matches on name; a rename makes it silently
  fall back to the pinned tag.
- **Package names are `vnovel`, not `onscripter-en`.** Any script or note still
  saying `libretro-onscripter-en` / `onscripter-en_libretro.so` /
  `*_engine_krkrsdl2.so` is stale.

The full end-to-end image recipe (flashing, first boot, custom overlay) is in
`~/dev/recalbox-devtools/BUILD-RPI5-RECALBOX-MATCHA.md`.

### Where the buildroot recipe invokes this Makefile

`package/libretro-vnovel/libretro-vnovel.mk` calls `make` with the target
toolchain and `platform=unix`:

```make
$(MAKE) CXX="$(TARGET_CXX)" CC="$(TARGET_CC)" AR="$(TARGET_AR)" \
    OBJCOPY="$(TARGET_OBJCOPY)" NM="$(TARGET_NM)" \
    EXTRA_CFLAGS="$(TARGET_CFLAGS)" EXTRA_CXXFLAGS="$(TARGET_CXXFLAGS)" \
    EXTRA_LDFLAGS="$(TARGET_LDFLAGS)" \
    PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
    KRKR_PREBUILT_DIR="$(STAGING_DIR)/usr/lib/vnovel" \
    -C $(@D) platform=unix
```

It passes `EXTRA_*FLAGS`, never `CFLAGS`/`LDFLAGS` directly: a command-line
assignment silences the Makefile's own `+=`, which would drop the include paths
and the vendored-rename define. The recipe sets no engine or socket option
because there is none: both are always compiled in. It does not set
`PKG_CONFIG_SYSROOT_DIR` either: buildroot's `pkg-config`
is a wrapper that already exports the right sysroot and library directories, and
overriding one of them breaks dependency resolution.

---

## Host aarch64 cross-build (optional)

Not part of the Recalbox flow. Useful for a quick smoke build on an x86_64 dev
machine that has the arm64 packages.

```bash
# Debian/Ubuntu, with arm64 packages from multiarch:
dpkg --add-architecture arm64
apt-get update
apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libfreetype-dev:arm64

make platform=aarch64
```

The engine needs arm64 `sdl2`, `freetype2`, `fontconfig` and `vorbisfile`
development packages on the pkg-config search path, exactly as the native build
does.

`make platform=aarch64` aims the engine's CMake at the target sysroot itself,
because a host `pkg-config` reports host paths — see
[Troubleshooting](#host-pkg-config-includes-poison-the-aarch64-engine-build) for
what that breaks.

---

## Troubleshooting

### Host pkg-config includes poison the aarch64 engine build

Symptom, from the engine's own sources:

```
cast from 'const void*' to 'TJS::tjs_intptr_t' {aka 'int'} loses precision
```

Cause: the engine's CMake resolved its dependencies through the **host's**
`pkg-config`, which reports `-isystem /usr/include`. That puts the host's
`bits/wordsize.h` ahead of the target's, so `__WORDSIZE` is 32 and `intptr_t`
becomes `int` — on a 64-bit target.

`make platform=aarch64` prevents this by running the engine's configure and
build with `PKG_CONFIG_SYSROOT_DIR` and `PKG_CONFIG_LIBDIR` aimed at the target
sysroot (`KRKR_CMAKE_ENV` in the Makefile). Override either if your toolchain
lives elsewhere:

```bash
make platform=aarch64 AARCH64_SYSROOT=/path/to/sysroot
make platform=aarch64 AARCH64_PKG_LIBDIR=/path/to/sysroot/usr/lib/pkgconfig
```

A pkg-config given by path is left alone — buildroot passes one that already
knows its sysroot, and its recipe documents that overriding these variables
breaks it. To replace it wholesale:

```bash
make platform=aarch64 PKG_CONFIG=/path/to/sysroot-aware/pkg-config
```

### The Windows build has no music / "vorbis not found"

`build/mingw-deps/` is missing or stale. Run `tools/build-mingw-vorbis.sh`.
The failure is silent: the build succeeds without an `.ogg` decoder.

### The Windows engine build fails with host-architecture errors

`build/mingw-deps/mingw-toolchain.cmake` is missing, so CMake configured the
engine for the host. Re-run `tools/build-mingw-vorbis.sh` (it regenerates the
toolchain file if absent).

### `undefined reference to fopen_utf8` or `__imp_*` on Windows

`fopen_utf8` comes from libretro-common's `compat/fopen_utf8.c`, and Winsock
symbols from `-lws2_32`; both are in the build already. If you see these, your
source list or link line has been edited away from the Makefile's.

### `make: no rule to make target` for a target named in a doc

Docs and notes can lag the Makefile. The Makefile's targets are the truth:

```bash
grep -nE '^[a-zA-Z0-9_/.-]+:' Makefile
```

### A build directory seems to keep stale flags

The KiriKiri engine's CMake caches its configuration, so a changed
`KRKR_CMAKE_*` value can be ignored. The Makefile writes a stamp
(`$(KRKR_FLAGS_STAMP)`) that makes a flag change a real dependency; if you
suspect the cache anyway, `rm -rf deps/krkrsdl2/build-<platform>*` and rebuild.

---

## What not to write in a build note

These are gone; a note mentioning them is stale, and correcting the note is
better than following it.

- **Removed layouts:** `vnovel_runtime.so`, `vnovel_engine_ons.so`,
  `vnovel_engine_pons.so`, `vnovel_engine_krkrsdl2.so`, `libkrkrsdl2.so`. The
  core is one file with everything linked in.
- **Old names:** the package `libretro-onscripter-en` (now `libretro-vnovel`),
  the core `onscripter-en_libretro.so` (now `vnovel_libretro.so`), and
  `onscripter-en_libretro.info`.
- **Removed build flags:** `make PROBES=1` / `-DKRKR_DEBUG_PROBES` (its one
  probe is now the core's `input` debug bit), `make debug KRKR_DEBUG_PROBES=`,
  `-debugwin`, and the KiriKiri 2 experiment's `make KRKR2=1`.
- **Removed runtime variables:** `VNOVEL_MENU_DUMP` (now
  `VNOVEL_DEBUG=core:menu`), `ONS_CLOCK_DEBUG` (now `VNOVEL_DEBUG=core:clock`),
  `KRKR_INPUT_DEBUG` (now `VNOVEL_DEBUG=core:input`). All runtime diagnostics
  are one mask now — see [Diagnostics](CONTROLS.md#diagnostics-vnovel_debug).
  The engine still accepts `KRKR_SELECT_DEBUG`, `KRKR_SCRIPT_DEBUG`,
  `KRKR_SPLIT_DEBUG` and `KRKR_PROBE_FILL` as aliases for their mask bits, but
  they are not the documented spelling.
