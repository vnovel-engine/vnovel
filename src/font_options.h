/* font_options.h — the user's font choice, and how a face is named.
 *
 * This is the interface shared by the core and all three engines. The core
 * resolves the frontend's core options into a choice (it is the only side that
 * can talk to the frontend, and the only side that can list a directory); each
 * engine reads the choice through the bridge and opens the face itself.
 *
 * It lives in its own header, included by the bridge and by each engine, so the
 * engine side never has to see `struct retro_core_option_definition` or any
 * other frontend type: an engine built standalone (the krkrsdl2 and ONScripter
 * trees each have their own main()) still compiles.
 *
 * WHERE THE FACES COME FROM
 * A face is a FILE in the engine data directory — the one overrides.txt and the
 * KAG polyfill already live in — as `<dir>/<file>`. Two consequences, both
 * wanted:
 *
 *   - The core ships a small bundled set (the tables below), and a user may
 *     ADD a face by dropping a .ttf/.otf into that directory. The core lists
 *     the directory at startup and offers everything it finds, so a face of
 *     one's own gets a `Font` option value without recompiling anything.
 *   - Nothing is embedded in the core binary; the faces cost it nothing, and a
 *     user may replace a bundled face under the same name.
 *
 * The two FALLBACK fonts are still embedded (resources/fonts/ → fallback_font.h),
 * because a game with no usable font must render with nothing deployed beside
 * the core. A user-chosen face has no such requirement; an unavailable one
 * falls back to the game's own font, logged.
 */

#ifndef VNOVEL_FONT_OPTIONS_H
#define VNOVEL_FONT_OPTIONS_H

#ifdef __cplusplus
extern "C"
{
#endif

   /* The faces the core KNOWS about — the bundled set. Each is installed by
    * tools/deploy-engine-data.sh from resources/fonts/, where its licence text
    * sits beside it. All are SIL OFL 1.1. OpenDyslexic is RFN'd ("OpenDyslexic"
    * is a Reserved Font Name), which restricts *modified* redistributions only
    * — the unmodified file is redistributed here, which the OFL permits.
    *
    * `label` is the core-option value a user picks, and is a FIXED STRING here
    * rather than the filename: RetroArch stores the chosen value by name, so a
    * stable label is what keeps a saved choice meaningful across a rename of
    * the file.
    *
    * This table is the BUNDLED set, not the whole choice: the core also lists
    * the face directory at startup and offers any other .ttf/.otf it finds
    * under its filename (see build_font_face_options in libretro.cpp). So a
    * face a user adds needs no entry here, and an entry here whose file is
    * absent is simply not offered.
    *
    * All faces are opened as `<face directory>/<file>`, so `file` is a basename
    * and must not contain a separator. */
   typedef struct VNovelFontSource
   {
      const char *label;     /* core-option value */
      const char *file;      /* installed basename */
      const char *project;   /* upstream URL */
      const char *licence;   /* licence name */
   } VNovelFontSource;

   static const VNovelFontSource vnovel_font_sources[] = {
         {"OpenDyslexic", "OpenDyslexic-Regular.otf",
          "https://github.com/antijingoist/opendyslexic",
          "SIL OFL 1.1 (Reserved Font Name: OpenDyslexic)"},
         {"Atkinson Hyperlegible", "AtkinsonHyperlegible-Regular.ttf",
          "https://github.com/googlefonts/atkinson-hyperlegible",
          "SIL OFL 1.1"},
         {"Comic Neue", "ComicNeue-Regular.ttf",
          "https://github.com/crozynski/comicneue", "SIL OFL 1.1"},
         {"EB Garamond", "EBGaramond-Variable.ttf",
          "https://github.com/octaviopardo/EBGaramond12", "SIL OFL 1.1"},
         {"BIZ UDPGothic", "BIZUDPGothic-Regular.ttf",
          "https://github.com/googlefonts/morisawa-biz-ud-gothic",
          "SIL OFL 1.1"},
   };

   enum { VNOVEL_FONT_SOURCE_COUNT =
                (int)(sizeof(vnovel_font_sources) / sizeof(vnovel_font_sources[0])) };

   /* What each bundled face is, and why it is here:
    *   OpenDyslexic        Weighted letter bottoms, the face purpose-built for
    *                       readers with dyslexia. (OTF/CFF, which this build's
    *                       stb_truetype does rasterize.)
    *   Atkinson Hyperlegible Distinct letterforms rather than a weighted
    *                       baseline (Braille Institute) — the better choice
    *                       where the problem is acuity, not letter-tracking.
    *   Comic Neue          the comic-book face Comic Sans was for.
    *   EB Garamond         A reading serif; body-text comfort rather than
    *                       letterform disambiguation.
    *   BIZ UDPGothic       (Morisawa, OFL). The Japanese UD face — the only
    *                       bundled entry with CJK coverage, so it is what a
    *                       Japanese player picks.
    *
    * Latin faces have no CJK coverage and BIZ UDPGothic has no Latin beyond its
    * own; an engine therefore keeps its existing per-codepoint fallback chain
    * underneath whichever face is chosen, rather than replacing it. A chosen
    * face that lacks a codepoint falls through to Mona/FiraSans as before. */

   /* Tint: a per-channel percentage applied to a glyph's colour when it is
    * blended, so text can be pushed toward (or away from) the backdrop without
    * the game's own colour arithmetic changing. 100 for every channel is the
    * identity, and is what a normal run uses.
    *
    * It is a multiply, not a blend or a curve, on purpose: a multiply on the
    * glyph colour is the same operation whether it is applied to the whole
    * frame or to the text plane alone, which is what keeps it compatible with
    * the text separation the core already does (see the effects table in
    * private/notes/text-rendering-handover.md §10). */
   typedef struct VNovelFontTint
   {
      int r, g, b;   /* per-channel percent; 100 = unchanged */
   } VNovelFontTint;

   /* Glyph-size scale, in percent of the size the game's script asked for.
    * 100 is the identity.
    *
    * This is a SCALE, not an absolute pixel size, and that is a deliberate
    * choice: each engine derives its cell grid, line pitch and metrics from the
    * size the script asked for, and a reader who wants larger text wants those
    * to grow with it. Scaling the size at the point the script's value is
    * applied keeps the grid, the pitch, the wrapping plan and the drawn glyphs
    * consistent with each other — the invariant the total-fit planner depends
    * on (see wrap.h: it compares the engine's own advances against the engine's
    * own room, so both must move together or the plan is wrong). An absolute
    * override would break that pairing. */
   enum { VNOVEL_FONT_SCALE_MIN = 50, VNOVEL_FONT_SCALE_MAX = 300 };

   /* Letter- and word-spacing, in PER-MILLE OF THE EM (so 50 is 0.05em, and the
    * core option's 0.05 step is a step of 50 here).
    *
    * Why a fraction of the em and not a pixel count: an em is each engine's own
    * idea of "one character is this big", so the same setting means the same
    * thing whatever the face, the script's font size, and the user's size scale
    * are — and it needs no knowledge of the pixel-per-point ratios the engines
    * carry (ONScripter's screen_ratio, PONScripter's float sizes). An engine
    * converts it with its own em at the point of use.
    *
    * Positive widens, negative tightens. 0 is the identity and is the default,
    * which is what keeps a run that does not set these bit-identical.
    *
    * WHY THESE ARE SAFE FOR THE TOTAL-FIT PLANNER
    * The planner (see wrap.h) compares the engine's OWN measured advance against
    * the engine's OWN line room. Spacing is therefore added inside the one
    * function each engine uses to advance the cursor AND to measure a run, not
    * beside the drawing code: measure and draw cannot disagree, so the plan
    * stays exact whatever the spacing is. Adding it only where glyphs are drawn
    * would make the planner wrap to a width the visible line does not reach. */
   enum {
      VNOVEL_FONT_SPACING_MIN  = -1000,  /* -1.00em: glyphs overlap */
      VNOVEL_FONT_SPACING_MAX  = 1000,   /* +1.00em */
      VNOVEL_FONT_SPACING_STEP = 50      /* the core option's 0.05 step */
   };

#ifdef __cplusplus
}
#endif

#endif /* VNOVEL_FONT_OPTIONS_H */
