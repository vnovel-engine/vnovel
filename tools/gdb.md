# Debugging the KiriKiri engine under gdb

The conventions in `AGENTS.md` are right in spirit but were written for a split
build (an `Fate.exe`-style standalone plus a dlopen'd engine). The current
layout is different in two ways that change the recipe.

## What is different now

1. **The engine is statically linked into `vnovel_libretro.so`.** There is no
   dlopen, no separate engine module: `nm vnovel_libretro.so` resolves
   `tTJSInterCodeContext`, `tTJSNI_BaseLayer`, and the TJS interpreter directly.
   So `set breakpoint pending on` is still useful (the symbols do not exist until
   the shared object is loaded) but the "the engine is a dlopen'd .so" reasoning
   no longer applies — and there is no second `.so` to attach to.

2. **`make debug` was silently producing optimised, inlined code.** The flags
   were passed in `CMAKE_CXX_FLAGS`, which cmake expands *before*
   `CMAKE_CXX_FLAGS_RELEASE`. Both engine trees force
   `-DCMAKE_BUILD_TYPE=Release`, so the trailing `-O3 -DNDEBUG` won and
   `-O0`/`-fno-inline` were undone. The objects still carried `-g`, so the
   failure looked like "symbols resolve but accessors have no frame" — the exact
   symptom the conventions describe, from a different cause.

   Fixed: the flags now go in `CMAKE_CXX_FLAGS_RELEASE` / `CMAKE_C_FLAGS_RELEASE`,
   which are expanded last. Verified from DWARF, not from the command line:

   ```
   DW_AT_producer: GNU C++14 ... -g -O0 -std=gnu++14 -fvisibility=hidden ... -fno-inline -fPIC
   ```

   Confirm it after any change with:

   ```
   readelf --debug-dump=info vnovel_libretro.so | grep -m1 DW_AT_producer
   nm -C vnovel_libretro.so | grep -c 'tTJSNI_BaseLayer::GetHoldAlpha'   # nonzero = out of line
   ```

   A passing command line is not evidence; the DWARF producer string is.

## Driving a session

```
make debug                                   # -g -O0 -fno-inline
./tools/kstop.sh
gdb -batch -x tools/krkr.gdb \
    --args retroarch -L ./vnovel_libretro.so /path/to/data.xp3
```

`tools/krkr.gdb` carries the two patterns that matter: a `silent` + counter +
`continue` breakpoint, and a conditional breakpoint. Edit the `break` lines for
the question at hand.

## Rules that are easy to get wrong

- **An unconditional breakpoint on a per-frame path kills the run.** The engine
  loop is the thread being stopped. Use `commands ... continue` so the stop is
  invisible to the engine, or a condition, or printf-from-the-target. Measured:
  a breakpoint on `AddLocalVariable` hits 4829 times and a `GetProvincePixel`
  breakpoint 10 times in one short run with the engine still loading scenarios
  and compositing normally.
- **Never call an inlined/void method from a gdb `printf`.** Under `make debug`
  the accessors have frames and this works; under a normal `-O3` build it aborts
  the script (`Cannot evaluate function -- may be inlined`) and silently
  truncates everything after it. When in doubt, read raw fields.
- **The engine runs on its own thread.** Expect `[Switching to thread N]`; set
  breakpoints by symbol, not by thread number.
- **`kill` the gdb parent before `kstop.sh`.** A killed parent leaves the
  traced `retroarch` orphaned; `pkill -9 -x gdb` first.

## Reading a level/flag decision

The engine logs its resolved compatibility level and the composite algorithm it
bound at start-up, so a run proves which implementation produced the frame:

```
(info) engine compatibility profile: 2.19-era (pre-2.25 defaults)
(info) layer compositing level: Legacy219 (pre-2006: children's output stands)
```

If those two lines are absent, the engine never reached base-systems init and
nothing downstream is trustworthy.
