# gdb commands for driving the KiriKiri engine inside the libretro core.
#
#   gdb -batch -x tools/krkr.gdb --args retroarch -L ./vnovel_libretro.so /path/to/data.xp3
#
# Read tools/gdb.md first: the rules below are the difference between a run that
# answers a question and a run that silently answers nothing.
#
# Preliminaries that are NOT optional:
#
#   set breakpoint pending on   The engine is compiled -fvisibility=hidden and
#                               loaded long after gdb starts, so every engine
#                               symbol is "not defined" at parse time. Without
#                               pending, the whole script aborts.
#
#   no `set confirm off` needed; -batch already answers nothing.
#
# Things this file deliberately does NOT do, having cost a session each:
#
#   * It does not break unconditionally on a per-frame path. A plain breakpoint
#     on GetProvincePixel / OnMouseMove / GetProvincePixel-class code stalls the
#     VM: the engine's main loop is the thread being stopped, the screen freezes,
#     and the run under test is over. `commands ... continue` makes the stop
#     invisible to the engine (verified: 4829 hits on AddLocalVariable and 6 on
#     GetProvincePixel with the engine still loading scenarios and compositing).
#   * It does not call inlined/void methods from printf. Under `make debug`
#     (-fno-inline) the accessors have frames; under a normal -O3 build they do
#     not, and a call into one aborts the script with "Cannot evaluate function
#     -- may be inlined" and silently truncates everything after it.
#   * It does not print from a probe's own counter. A printf in the target is
#     only as good as the build that produced it; a breakpoint answers questions
#     the probe was not written to ask, at no rebuild cost.
#
# Convention for a value that cannot be had from registers: print from the
# target ("log, don't break"), which needs a rebuild but never stalls.

set pagination off
set breakpoint pending on
set print thread-events off
set debuginfod enabled off

# The engine runs on its own thread; this is where every engine frame lives.
# Set automatically once the engine has started, so it is not needed here.

# Edit below. One question per session: a breakpoint answers "which instance,
# which call path, how many times", so `silent` + a counter + `continue` is the
# default shape, and a conditional breakpoint is used when the interesting
# event is rare and unconditional logging would flood.

set $alc = 0
set $gpp = 0

# Which codegen site emits the class-scope `var` opcode, and how often.
break TJS::tTJSInterCodeContext::AddLocalVariable
commands
  silent
  set $alc = $alc + 1
  printf "AddLocalVariable cnt=%d name=%ls init=%d\n", $alc, name, init
  continue
end

# The hot hit-test path, counted rather than stopped on.
break tTJSNI_BaseLayer::GetProvincePixel
commands
  silent
  set $gpp = $gpp + 1
  printf "GetProvincePixel cnt=%d x=%d y=%d\n", $gpp, x, y
  continue
end

run
printf "RUN-RETURNED alc=%d gpp=%d\n", $alc, $gpp
