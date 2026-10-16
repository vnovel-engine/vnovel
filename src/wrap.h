/* Total-fit line breaking, shared by every engine in the core.
 *
 * The engines are forks of one codebase: ONScripter-EN, PONScripter and
 * KiriKiri each have their own Fontinfo, their own text buffer and their own
 * idea of what a "character" costs. They cannot share types — they are separate
 * class hierarchies linked into one .so with the symbols renamed apart (see
 * ons-/pons-symbol-prefix.h). So this header shares the *decision*, and takes
 * everything engine-specific through the callback table below.
 *
 * What is shared is the part that is the same everywhere: given a set of
 * offsets where a break is legal, choose the breaks that balance the lines.
 * What is not shared is how wide a run is and how much room a line has, because
 * that is exactly where the engines differ:
 *
 *   ONScripter-EN  half-width cells, with proportional ASCII folded into cells
 *                  per character (advanceCharInPixels) and a sub-cell remainder
 *   PONScripter    float pixels, no cell grid at all (GlyphAdvance)
 *   KiriKiri       its own visual layer's text metrics
 *
 * So the unit here is the engine's own horizontal distance, summed by the
 * engine's own per-character advance, and compared against the engine's own
 * line width. The planner never converts between units or assumes a grid: it
 * asks for a run's width and a line's room, both in the same unit, and compares
 * them. That is what makes this usable for a fixed-pitch face, a proportional
 * TTF, and a page budget aimed at a handheld screen alike.
 *
 * The text itself stays in the engine: callbacks take an opaque offset range
 * into whatever buffer the engine owns. */

#ifndef VNOVEL_WRAP_H
#define VNOVEL_WRAP_H

#ifdef __cplusplus

/* One line-end candidate: an offset into the engine's text at which a break
 * may be placed. The engine produces these; the planner only chooses among
 * them, so every rule the engine already enforces (an apostrophe stays inside
 * its word, punctuation does not lead a line, CJK kinsoku) holds here without
 * being restated. */
struct WrapCandidate {
   int offset;
};

/* What the planner needs from the engine. All distances are in the engine's own
 * horizontal unit and must be mutually consistent; the planner only compares
 * them, never converts.
 *
 * Every callback is called with the same opaque `ctx` the caller passed, so an
 * engine can hand in a `this` pointer or a small struct. */
struct WrapEngine {
   void *ctx;

   /* Width of the text [from,to) drawn from a fresh line, measured with the
    * engine's own per-character advance.
    *
    * This MUST be the drawing loop's arithmetic, not a re-derivation: summing
    * advances and folding per character disagree when the fold rounds, which is
    * exactly the difference that once let a run the planner called "fits"
    * overflow the real line and clip its last word. Measure the way you draw.
    *
    * Float, because the unit differs per engine and float spans them all
    * without loss: ONScripter-EN's integer half-cells are exact as floats, and
    * PONScripter's proportional advances are genuinely fractional (it
    * accumulates pos_x as a float, so a float sum matches its drawing exactly
    * while a rounded integer sum would drift). The unit is whatever the engine
    * lays text out in — cells, pixels — and is never converted here. */
   float (*run_width)(void *ctx, int from, int to);

   /* Room on a line that has not been started, in the same unit as run_width. */
   float (*line_room)(void *ctx);

   /* Room already consumed on the line the text will start on. Zero for a
    * fresh line. Non-zero matters because a clickwait pause does not move the
    * carriage: the statement after it continues mid-line, and planning a full
    * line there puts the first break too late. */
   float (*line_used)(void *ctx);
};

/* Choose the breaks for the candidates in [start,end).
 *
 * `candidates` are the offsets where a break is legal, ascending, strictly
 * inside (start,end). `out` receives the chosen break offsets, also ascending;
 * it must have room for `out_cap`.
 *
 * Returns the number of breaks written, or -1 when no break set fits (a single
 * unsplittable run is wider than a line). The caller decides what to do with
 * -1; leaving the text alone and letting the existing per-character path handle
 * it is a reasonable choice, because that path is what a line too narrow for
 * even one word already does.
 *
 * Pure: same inputs give the same breaks. Nothing here reads engine state, so
 * text speed, skip mode and replay cannot change the result. */
int wrap_total_fit(const WrapEngine *engine,
                   const WrapCandidate *candidates, int num_candidates,
                   int start, int end,
                   int *out, int out_cap);

/* Cost of leaving `slack` unused on a `room`-wide line. Cubed, normalised, so
 * one very loose line costs more than two slightly loose ones and the pass
 * spreads the slack instead of dumping it on the last line. */
float wrap_badness(float slack, float room);

#endif /* __cplusplus */

#endif /* VNOVEL_WRAP_H */
