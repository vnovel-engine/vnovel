/* vnovel_font_api.h — the engine side of the font options, in one place.
 *
 * Every engine that applies a user font needs the same three operations from
 * the bridge (see engine_bridge.h for what they do and why the face's bytes
 * live in the core rather than in an engine). Spelling them out at each use
 * site would be five copies of the same declarations, and five places for the
 * no-op fallback to drift; so the declarations are here and each engine file
 * includes this instead.
 *
 * The libretro build resolves them to the bridge's definitions. A standalone
 * engine build (the krkrsdl2 and ONScripter trees each have their own main())
 * has no core and no option channel, so they become inline no-ops: the engine
 * then behaves exactly as it did before this feature, and nothing has to
 * test for the core's presence at the call site.
 *
 * Include <SDL.h> or the engine's own headers first if the file needs them;
 * this header deliberately depends on nothing but size_t, so it is safe in an
 * engine's plain C++ file.
 */

#ifndef VNOVEL_FONT_API_H
#define VNOVEL_FONT_API_H

#ifdef __LIBRETRO__

extern "C" {

/* The selected face's path, or 0 for the game's own font. `out` receives the
 * full path (size `out_size`). The ONLY place the user's choice is validated,
 * so an engine cannot read past the table or build a path from an unchecked
 * index. `name` may be NULL and points into static data. */
extern int vnovel_font_selected_path(char *out, int out_size, const char **name);

/* `size` scaled by the user's font-size option, clamped and never below 1.
 * Returns `size` unchanged when the option is at its default. */
extern int vnovel_font_scaled_size(int size);

/* `rgb` (0xRRGGBB) with the user's tint applied per channel. Returns `rgb`
 * unchanged when the tint is at its default. Alpha is not in `rgb`. */
extern unsigned int vnovel_font_tint_rgb(unsigned int rgb);

/* The two user spacings, in per-mille of the em (0 = the game's own spacing,
 * which is the default). Letter-spacing is added after every character,
 * word-spacing only after a space (so it is added ON TOP of the letter spacing
 * a space already gets, which is what "wider gaps between words" means).
 *
 * Read them INSIDE the function that advances the cursor and measures a run,
 * never beside the drawing code: the total-fit planner compares the engine's own
 * advance against the engine's own room, so a spacing added only where glyphs
 * are drawn would make it wrap to a width the visible line does not reach. See
 * font_options.h for the argument in full. */
extern int vnovel_font_letter_spacing;
extern int vnovel_font_word_spacing;

/* Per-mille of the em → whole script pixels, at `em_px` = the engine's own em
 * in script pixels. Returns 0 at per_mille 0, so the default adds nothing. */
extern int vnovel_font_spacing_px(int per_mille, int em_px);

/* Set by the CORE when the font-size option changes, cleared by the ENGINE when
 * it has applied the new value at a safe boundary (see the ONS/PON refresh
 * calls). The engine owns the timing because only it knows where a page ends:
 * re-applying the scale mid-page would redraw lines already laid out at the old
 * pitch and make them overlap or drift.
 *
 * A flag rather than a direct call, so the core never reaches into an engine's
 * layout at a moment it cannot see. Engines that do not implement a refresh
 * simply never clear it; the cost is one never-read 0. */
extern int vnovel_font_scale_refresh_requested;

} /* extern "C" */

#else /* !__LIBRETRO__ — a standalone engine: no core, so no options. */

/* A standalone engine has no core and no option channel, so these are the
 * identities: spacing 0 adds nothing and the em helper returns 0. The engine
 * then lays out text exactly as it did before this feature existed. */
static const int vnovel_font_letter_spacing = 0;
static const int vnovel_font_word_spacing   = 0;

/* No core to signal a change, so the flag is always clear and the refresh path
 * — if the engine even compiles it — is dead. Mutable, unlike the two above,
 * so an engine that clears it still compiles. */
static int vnovel_font_scale_refresh_requested = 0;

static inline int vnovel_font_spacing_px(int per_mille, int em_px)
{
    (void)per_mille;
    (void)em_px;
    return 0;
}

static inline int vnovel_font_selected_path(char *out, int out_size,
                                            const char **name)
{
    if (out && out_size > 0) out[0] = '\0';
    if (name) *name = 0;
    return 0;
}

static inline int vnovel_font_scaled_size(int size) { return size; }

static inline unsigned int vnovel_font_tint_rgb(unsigned int rgb) { return rgb; }

#endif /* __LIBRETRO__ */

#endif /* VNOVEL_FONT_API_H */
