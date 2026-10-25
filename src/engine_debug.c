/* engine_debug.c — the two ONScripter slices of the debug mask. See
 * engine_debug.h and vnovel_debug.h.
 *
 * Both engines are built from this repository's Makefile into the same core, so
 * one copy serves both slices, and the core's start-up resolves and states them
 * beside its own. The engines themselves only test bits. */

#include "engine_debug.h"

static const VNovelDebugBit ons_debug_bits[] = {
   { ONS_DEBUG_FLAG_AVI, "avi", "the AVI decoder's stream parameters" },
};

static const VNovelDebugBit pons_debug_bits[] = {
   { PONS_DEBUG_FLAG_AVI, "avi", "the AVI decoder's stream parameters" },
};

const VNovelDebugDesc ons_debug_desc = {
   "ons",
   VNOVEL_DEBUG_SHIFT_ONS,
   VNOVEL_DEBUG_WIDTH_ONS,
   ons_debug_bits,
   sizeof(ons_debug_bits) / sizeof(ons_debug_bits[0]),
};

const VNovelDebugDesc pons_debug_desc = {
   "pons",
   VNOVEL_DEBUG_SHIFT_PONS,
   VNOVEL_DEBUG_WIDTH_PONS,
   pons_debug_bits,
   sizeof(pons_debug_bits) / sizeof(pons_debug_bits[0]),
};

void ons_debug_resolve(void)
{
   static int done = 0;
   if (done) return;
   done = 1;
   vnovel_debug_env(&ons_debug_desc);
   vnovel_debug_env(&pons_debug_desc);
   vnovel_debug_describe(&ons_debug_desc);
   vnovel_debug_describe(&pons_debug_desc);
}
