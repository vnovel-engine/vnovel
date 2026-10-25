/* vnovel_debug.h — one debug mask, sliced by module.
 *
 * One 64-bit word, set from the environment variable VNOVEL_DEBUG, divided into
 * fixed slices so every module in the tree owns its own bits with no shared
 * numbering to agree on:
 *
 *   bit   0- 7   core          (this repo's src/)
 *   bit   8-15   onscripter-en (deps/onscripter-en)
 *   bit  16-23   ponscripter   (deps/ponscripter)
 *   bit  24-39   kirikiri      (deps/krkrsdl2; 16 bits, it has the most probes)
 *   bit  40-63   spare
 *
 * A module reads only its own slice. It defines its own bit constants starting
 * at 1u << 0 and shifts them into place when it tests them, so two modules may
 * use the same number for different things and neither has to know the other's
 * names:
 *
 *     #define VNOVEL_DBG_CORE_CLOCK (1u << 0)
 *     if (VNOVEL_DEBUG_TEST(VNOVEL_DEBUG_SHIFT_CORE, VNOVEL_DBG_CORE_CLOCK)) ...
 *
 * Each project compiles its own copy of vnovel_debug.c: the file depends on
 * nothing but <stdint.h>, <stdio.h>, <stdlib.h> and <string.h>, so a project
 * that builds standalone (krkrsdl2 has its own CMake, the engines their own
 * Makefiles) reads the same variable and the same layout without linking
 * against the core. Copies must stay byte-identical; the layout constants below
 * are the contract between them.
 *
 * The value is resolved once per module at start-up, from VNOVEL_DEBUG, and
 * cached. Diagnostics that can turn on mid-run make two halves of one log
 * incomparable, so there is no runtime setter other than vnovel_debug_set(),
 * which a host uses at start-up to apply a frontend option that a libretro
 * frontend cannot express as an environment variable.
 *
 * VNOVEL_DEBUG accepts a word, names, or a mix:
 *
 *     VNOVEL_DEBUG=0x00010002         raw word (hex or decimal)
 *     VNOVEL_DEBUG=all                every bit of the reading module's slice
 *     VNOVEL_DEBUG=none               off (also: empty, "off")
 *     VNOVEL_DEBUG=clock,stripes      names, in the reading module's slice
 *     VNOVEL_DEBUG=core:clock,krkr:province   qualified names
 *
 * "all" and a bare name are slice-local: every module reading the variable
 * applies them to its own bits, so `all` in a combined build enables every
 * module's probes. A name belonging to another module's slice is skipped
 * without complaint (the reading module has no table for it), while a name it
 * owns and cannot find is reported to stderr and ignored — a typo in a
 * launcher's configuration must not decide whether a game starts.
 *
 * Diagnostics only. A bit in this word may not change what the engine does; a
 * switch that changes behaviour belongs with the option that owns the
 * behaviour, not here. */

#ifndef VNOVEL_DEBUG_H
#define VNOVEL_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* The environment variable the whole tree reads. */
#define VNOVEL_DEBUG_ENV "VNOVEL_DEBUG"

/* Slice ordinals, for tables that name a slice rather than a shift. */
#define VNOVEL_DEBUG_SLICE_CORE  0u
#define VNOVEL_DEBUG_SLICE_ONS   1u
#define VNOVEL_DEBUG_SLICE_PONS  2u
#define VNOVEL_DEBUG_SLICE_KRKR  3u
#define VNOVEL_DEBUG_SLICE_SPARE 4u

/* Where each module's bits start in the 64-bit word. */
#define VNOVEL_DEBUG_SHIFT_CORE  0u
#define VNOVEL_DEBUG_SHIFT_ONS   8u
#define VNOVEL_DEBUG_SHIFT_PONS  16u
#define VNOVEL_DEBUG_SHIFT_KRKR  24u
#define VNOVEL_DEBUG_SHIFT_SPARE 40u

/* How many bits each module owns. */
#define VNOVEL_DEBUG_WIDTH_CORE  8u
#define VNOVEL_DEBUG_WIDTH_ONS   8u
#define VNOVEL_DEBUG_WIDTH_PONS  8u
#define VNOVEL_DEBUG_WIDTH_KRKR  16u
#define VNOVEL_DEBUG_WIDTH_SPARE 24u

/* One probe: its local bit, its name in VNOVEL_DEBUG, and one line saying what
 * it answers (a log reader wants the question, not the function that prints). */
typedef struct VNovelDebugBit
{
   unsigned    bit; /* 1u << n, local to the slice */
   const char *name;
   const char *description;
} VNovelDebugBit;

/* A module's slice: where its bits live and what they are called. */
typedef struct VNovelDebugDesc
{
   const char *prefix; /* the module's name in VNOVEL_DEBUG, e.g. "core" */
   unsigned    shift;  /* VNOVEL_DEBUG_SHIFT_* */
   unsigned    width;  /* VNOVEL_DEBUG_WIDTH_* */
   const VNovelDebugBit *bits;
   unsigned              bit_count;
} VNovelDebugDesc;

/* The resolved word. Zero before any module has resolved the environment. */
uint64_t vnovel_debug_word(void);

/* Resolve VNOVEL_DEBUG for `desc`'s slice and OR the result into the word.
 * Called once per module at start-up; returns the bits it added. */
uint64_t vnovel_debug_env(const VNovelDebugDesc *desc);

/* Replace the word (a host applying a frontend option at start-up). */
void vnovel_debug_set(uint64_t word);

/* OR bits into the word. */
void vnovel_debug_add(uint64_t bits);

/* Parse a spec into `desc`'s slice. Exposed so a host can turn a frontend
 * string into bits with the module's own name table. */
uint64_t vnovel_debug_parse(const VNovelDebugDesc *desc, const char *spec);

/* Write this slice's live bits to stderr, one probe per line. */
void vnovel_debug_describe(const VNovelDebugDesc *desc);

/* Write the whole word to stderr, whatever slices it holds. */
void vnovel_debug_describe_word(void);

/* The form call sites use: true when `bit` (local to the slice at `shift`) is
 * set. Evaluating it costs one load and one test, and the load is a static the
 * resolver wrote once. */
#define VNOVEL_DEBUG_TEST(shift, bit)                                          \
   (((vnovel_debug_word() >> (uint64_t)(shift)) & (uint64_t)(bit)) != 0)

#ifdef __cplusplus
}
#endif

#endif /* VNOVEL_DEBUG_H */
