/* core_debug.c — the core's slice of the debug mask. See vnovel_debug.h for the
 * layout and the environment syntax. */

#include "core_debug.h"
#include "engine_debug.h"

static const VNovelDebugBit core_debug_bits[] = {
   { DEBUG_FLAG_CLOCK, "clock",
     "the game clock, ~2 Hz, as fast-forward scales it" },
   { DEBUG_FLAG_INPUT, "input",
     "the input path: pad/mouse poll and the events pushed to the engine" },
   { DEBUG_FLAG_PRESENT, "present",
     "tint stripes over the finished frame" },
   { DEBUG_FLAG_MENU, "menu",
     "the native menu tree as the loader built it" },
};

const VNovelDebugDesc core_debug_desc = {
   "core",
   VNOVEL_DEBUG_SHIFT_CORE,
   VNOVEL_DEBUG_WIDTH_CORE,
   core_debug_bits,
   sizeof(core_debug_bits) / sizeof(core_debug_bits[0]),
};

void core_debug_init(void)
{
   static int done = 0;
   if (done) return;
   done = 1;

   /* Every slice that lives in this binary is resolved here, so the run states
    * its whole debug mask once, at start-up, before any diagnostic can print.
    * The KiriKiri slice is not resolved here: it belongs to an engine with its
    * own standalone build and its own table, so the core hands it the slice as
    * a raw word (see krkr_set_debug_flags) and that engine states it. */
   vnovel_debug_env(&core_debug_desc);
   ons_debug_resolve();
   vnovel_debug_describe(&core_debug_desc);
   vnovel_debug_describe_word();
}
