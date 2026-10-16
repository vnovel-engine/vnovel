/*
  SDL_ttf:  A companion library to SDL for working with TrueType (tm) fonts
  Copyright (C) 2001-2012 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software
  in a product, an acknowledgment in the product documentation would be
  appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
  misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/*
 * ALTERED SOURCE — this file is not the original SDL_ttf. Per restriction 2
 * above, the changes made to the vnovel core's copy are:
 *
 *   - The FreeType 2 renderer was replaced with stb_truetype (vendored in
 *     src/sdl-libretro/stb_truetype.h), because the core ships a fixed font
 *     set and must not depend on a system fontconfig/FreeType.
 *   - TTF_SetFallbackFontMem() was added: a process-wide last-resort font so a
 *     codepoint the game's own font lacks renders instead of .notdef.
 *   - Latin rendering was split into proportional and monospace paths. The
 *     game's Latin face is probed lazily (latin_monospace) and, when its ASCII
 *     advances are uniform, drawn with the proportional fallback so that
 *     designed-monospace game fonts do not look cramped next to CJK text.
 *   - Fixed-pitch CJK cells are laid out with the half-width metrics
 *     ONScripter's text engine assumes.
 */

#include <string.h>
#include "SDL_ttf.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_STREAM_TYPE SDL_RWops *
#define STBTT_STREAM_READ(s,x,y)  s->read(s, x, 1, y)
#define STBTT_STREAM_SEEK(s,x)    s->seek(s, x, RW_SEEK_SET);
#include "stb_truetype.h"

#define NUM_GRAYS       256

struct _TTF_Font {
    stbtt_fontinfo info;
    float scale;
    int ascent, descent, height;
    /* -1 unknown; 0 = game font's Latin is proportional; 1 = every sampled
     * ASCII advance is identical (a designed monospace Latin face). Computed
     * lazily; when 1, ASCII is rendered with the proportional fallback font. */
    int latin_monospace;

    /* We are responsible for closing the font stream */
    SDL_RWops *src;
    int freesrc;
};

/* Last-resort fallback font: stb_truetype has no fontconfig-style substitution,
 * so a codepoint the primary font lacks (e.g. U+2026 in the Japanese game
 * fonts) would render as .notdef. When registered, any glyph the primary font
 * does not carry is taken from here instead. Single, process-wide: the core
 * registers one at start-up. */
static struct {
    stbtt_fontinfo info;
    SDL_RWops *src;
    int loaded;
} s_fallback;

/* Register an in-memory last-resort font (e.g. the embedded ellipsis subset).
 * The buffer must outlive use; pass NULL/0 to clear. Returns 0 on success.
 * This stb build reads fonts through SDL_RWops, so wrap the buffer in one. */
int TTF_SetFallbackFontMem(const unsigned char *data, int size)
{
    if (s_fallback.loaded) {
        if (s_fallback.src) SDL_RWclose(s_fallback.src);
        s_fallback.src = NULL;
        s_fallback.loaded = 0;
    }
    if (!data || size <= 0) return 0;
    s_fallback.src = SDL_RWFromConstMem(data, size);
    if (!s_fallback.src) return -1;
    if (!stbtt_InitFont(&s_fallback.info, s_fallback.src,
                        stbtt_GetFontOffsetForIndex(s_fallback.src, 0))) {
        SDL_RWclose(s_fallback.src);
        s_fallback.src = NULL;
        return -1;
    }
    s_fallback.loaded = 1;
    return 0;
}

/* Additional fallbacks, tried in order when the primary fallback lacks a
 * codepoint. Needed because one font cannot cover everything these games use:
 * Mona is a CJK/MS PGothic clone (no Latin-1 accents), while a Latin font has
 * no CJK. The core registers both, so whichever carries the glyph wins. */
#define TTF_MAX_EXTRA_FALLBACKS 4
static struct {
    stbtt_fontinfo info;
    SDL_RWops *src;
    int loaded;
} s_extra_fallback[TTF_MAX_EXTRA_FALLBACKS];

int TTF_AddFallbackFontMem(const unsigned char *data, int size)
{
    for (int i = 0; i < TTF_MAX_EXTRA_FALLBACKS; i++) {
        if (s_extra_fallback[i].loaded) continue;
        if (!data || size <= 0) return -1;
        s_extra_fallback[i].src = SDL_RWFromConstMem(data, size);
        if (!s_extra_fallback[i].src) return -1;
        if (!stbtt_InitFont(&s_extra_fallback[i].info, s_extra_fallback[i].src,
                            stbtt_GetFontOffsetForIndex(s_extra_fallback[i].src, 0))) {
            SDL_RWclose(s_extra_fallback[i].src);
            s_extra_fallback[i].src = NULL;
            return -1;
        }
        s_extra_fallback[i].loaded = 1;
        return 0;
    }
    return -1;
}

/* First loaded extra fallback that carries `ch`, or NULL. */
static const stbtt_fontinfo *TTF_ExtraFallbackFor(Uint16 ch)
{
    for (int i = 0; i < TTF_MAX_EXTRA_FALLBACKS; i++) {
        if (!s_extra_fallback[i].loaded) continue;
        if (stbtt_FindGlyphIndex(&s_extra_fallback[i].info, ch) != 0)
            return &s_extra_fallback[i].info;
    }
    return NULL;
}

/* Pick the font for `ch`. The engine lays text out in fixed half-width cells
 * and places glyphs using the game font's ascent, so substituting a
 * proportional Latin font for ordinary text breaks inheriting advance/metrics.
 * Only the codepoints the game font renders wrongly go through the fallback —
 * currently U+2026 (the ellipsis, which CJK fonts carry in a full-width centred
 * form) — plus a general last-resort for anything the game font entirely lacks. */
#define TTF_FALLBACK_CODEPOINT(cp) ((cp) == 0x2026)
/* A font whose ASCII advances are all identical renders Latin monospaced; in
 * that case substitute the proportional fallback for ASCII so dialog is
 * readable. CJK fonts commonly ship such a fixed-pitch Latin face. Cached per
 * font (advances never change). */
static int TTF_LatinIsMonospace(const stbtt_fontinfo *info)
{
    static const char sample[] = "ilemWaAcdg0O2 ";
    int advance = -1;
    for (const char *p = sample; *p; ++p) {
        int g = stbtt_FindGlyphIndex(info, (unsigned char)*p);
        if (g == 0) continue;
        int a;
        stbtt_GetGlyphHMetrics(info, g, &a, 0);
        if (advance < 0) advance = a;
        else if (a != advance) return 0;
    }
    return advance >= 0;
}

static const stbtt_fontinfo *TTF_PickFont(TTF_Font *font, Uint16 ch, int *glyph, float *scale)
{
    if (s_fallback.loaded) {
        int g = stbtt_FindGlyphIndex(&font->info, ch);
        int use_fb = TTF_FALLBACK_CODEPOINT(ch) || g == 0;
        if (!use_fb && ch >= 0x20 && ch <= 0x7e) {
            if (font->latin_monospace < 0)
                font->latin_monospace = TTF_LatinIsMonospace(&font->info);
            use_fb = font->latin_monospace;
        }
        if (use_fb) {
            int fg = stbtt_FindGlyphIndex(&s_fallback.info, ch);
            if (fg != 0) {
                *glyph = fg;
                *scale = stbtt_ScaleForPixelHeight(&s_fallback.info, font->height);
                return &s_fallback.info;
            }
            /* The primary fallback lacks this codepoint (e.g. a CJK fallback
             * asked for an accented Latin letter). Try the additional ones. */
            const stbtt_fontinfo *extra = TTF_ExtraFallbackFor(ch);
            if (extra) {
                *glyph = stbtt_FindGlyphIndex(extra, ch);
                *scale = stbtt_ScaleForPixelHeight(extra, font->height);
                return extra;
            }
        }
        *glyph = g;
        *scale = font->scale;
        return &font->info;
    }
    *glyph = stbtt_FindGlyphIndex(&font->info, ch);
    *scale = font->scale;
    return &font->info;
}

const SDL_version *TTF_Linked_Version(void)
{
    static SDL_version linked_version;
    SDL_TTF_VERSION(&linked_version);
    return(&linked_version);
}

int TTF_Init(void)
{
    return 0;
}

TTF_Font* TTF_OpenFontIndexDPIRW(SDL_RWops *src, int freesrc, int ptsize, long index, unsigned int hdpi, unsigned int vdpi)
{
    TTF_Font *font;

    if (!src) {
        TTF_SetError("Passed a NULL font source");
        return NULL;
    }

    if (SDL_RWtell(src) < 0) {
        TTF_SetError("Can't seek in stream");
        if (freesrc)
            SDL_RWclose(src);
        return NULL;
    }

    font = (TTF_Font *)malloc(sizeof *font);
    if (font == NULL) {
        TTF_SetError("Out of memory");
        if (freesrc)
            SDL_RWclose( src );
        return NULL;
    }
    memset(font, 0, sizeof(*font));
    font->latin_monospace = -1;

    stbtt_InitFont(&font->info, src, stbtt_GetFontOffsetForIndex(src, index));
    stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, NULL);
    font->src = src;
    font->freesrc = freesrc;
    font->height = STBTT_iceil(ptsize) + 2;
    font->scale = stbtt_ScaleForPixelHeight(&font->info, font->height);
    font->ascent = STBTT_iceil(font->ascent * font->scale);
    font->descent = STBTT_iceil(font->descent * font->scale);

    return font;
}


TTF_Font* TTF_OpenFontDPIRW(SDL_RWops *src, int freesrc, int ptsize, unsigned int hdpi, unsigned int vdpi)
{
    return TTF_OpenFontIndexDPIRW(src, freesrc, ptsize, 0, hdpi, vdpi);
}

TTF_Font* TTF_OpenFontIndexRW(SDL_RWops *src, int freesrc, int ptsize, long index)
{
    return TTF_OpenFontIndexDPIRW(src, freesrc, ptsize, index, 0, 0);
}

TTF_Font* TTF_OpenFontRW(SDL_RWops *src, int freesrc, int ptsize)
{
    return TTF_OpenFontIndexRW(src, freesrc, ptsize, 0);
}

TTF_Font* TTF_OpenFontIndexDPI(const char *file, int ptsize, long index, unsigned int hdpi, unsigned int vdpi)
{
    SDL_RWops *rw = SDL_RWFromFile(file, "rb");
    if (rw == NULL) {
        TTF_SetError(SDL_GetError());
        return NULL;
    }
    return TTF_OpenFontIndexDPIRW(rw, 1, ptsize, index, hdpi, vdpi);
}

TTF_Font* TTF_OpenFontIndex(const char *file, int ptsize, long index)
{
    return TTF_OpenFontIndexDPI(file, ptsize, index, 0, 0);
}

TTF_Font* TTF_OpenFontDPI(const char *file, int ptsize, unsigned int hdpi, unsigned int vdpi)
{
    return TTF_OpenFontIndexDPI(file, ptsize, 0, hdpi, vdpi);
}

TTF_Font* TTF_OpenFont(const char *file, int ptsize)
{
    return TTF_OpenFontIndex(file, ptsize, 0);
}

void TTF_CloseFont(TTF_Font* font)
{
    if (font) {
        if (font->freesrc)
            SDL_RWclose(font->src);
        free(font);
    }
}

int TTF_FontHeight(const TTF_Font *font)
{
    return font->height;
}

int TTF_FontAscent(const TTF_Font *font)
{
    return font->ascent;
}

int TTF_FontDescent(const TTF_Font *font)
{
    return font->descent;
}

int TTF_FontLineSkip(const TTF_Font *font)
{
    return font->height;
}

int TTF_GlyphMetrics(TTF_Font *font, Uint16 ch,
                     int *minx, int *maxx, int *miny, int *maxy, int *advance)
{
    int x0, y0, x1, y1;
    int advance_width;
    int glyph; float fscale;
    const stbtt_fontinfo *fi = TTF_PickFont(font, ch, &glyph, &fscale);
    if (glyph == 0) {
        TTF_SetError("Couldn't find glyph");
        return -1;
    }

    stbtt_GetGlyphBox(fi, glyph, &x0, &y0, &x1, &y1);
    stbtt_GetGlyphHMetrics(fi, glyph, &advance_width, 0);
    if (minx)
        *minx = STBTT_ifloor(x0 * fscale);
    if (miny)
        *miny = STBTT_ifloor(y0 * fscale);
    if (maxx)
        *maxx = STBTT_iceil(x1 * fscale);
    if (maxy)
        *maxy = STBTT_iceil(y1 * fscale);
    if (advance)
        *advance = STBTT_iceil(advance_width * fscale);
    return 0;
}

void TTF_SetFontStyle(TTF_Font* font, int style)
{
}

int TTF_GetFontStyle(const TTF_Font* font)
{
    return 0;
}

void TTF_SetFontOutline(TTF_Font* font, int outline)
{
}

int TTF_GetFontOutline(const TTF_Font* font)
{
    return 0;
}

SDL_Surface* TTF_RenderGlyph_Shaded(TTF_Font* font,
                                    Uint16 ch,
                                    SDL_Color fg,
                                    SDL_Color bg)
{
    SDL_Surface *surface;
    SDL_Palette *palette;
    int rdiff, gdiff, bdiff;
    int width = 0, height = 0;

    int glyph; float fscale;
    const stbtt_fontinfo *fi = TTF_PickFont(font, ch, &glyph, &fscale);
    if (glyph == 0) {
        TTF_SetError("Couldn't find glyph");
        return NULL;
    }

    surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 0, 0, 8, 0, 0, 0, 0);
    if (surface == NULL)
        return NULL;
    surface->pixels = stbtt_GetGlyphBitmap(fi, 0.0, fscale, glyph, &width, &height, 0, 0);
    surface->w = width;
    surface->h = height;
    surface->pitch = width;
    SDL_SetClipRect(surface, NULL);

    /* Fill the palette with NUM_GRAYS levels of shading from bg to fg */
    palette = surface->format->palette;
    rdiff = fg.r - bg.r;
    gdiff = fg.g - bg.g;
    bdiff = fg.b - bg.b;
    for (int index = 0; index < NUM_GRAYS; ++index) {
        palette->colors[index].r = bg.r + (index*rdiff) / (NUM_GRAYS-1);
        palette->colors[index].g = bg.g + (index*gdiff) / (NUM_GRAYS-1);
        palette->colors[index].b = bg.b + (index*bdiff) / (NUM_GRAYS-1);
    }
    return surface;
}

/* Render a single character, "Solid" style.
 *
 * The vendored NP2kai menu's font manager (deps/np2kai-menu) calls this, and
 * calls it with a one-character string - it sets `sString[0] = c; sString[1] =
 * 0;` before each call, because it measures and caches glyphs one at a time.
 * So a single-glyph implementation is faithful here rather than a compromise.
 *
 * A multi-character string returns NULL rather than silently rendering only the
 * first glyph: a caller wanting several needs to lay them out itself, and
 * returning a partial result would look like a working call that drops text.
 *
 * Upstream's "Solid" and "Shaded" differ only in the background: Solid leaves it
 * as the colorkey (transparent), Shaded fills it. So this reuses the existing
 * Shaded rasterizer with a transparent background and colorkeys index 0 - a
 * colour-setup difference, not a second rasterizer. */
SDL_Surface* TTF_RenderUNICODE_Solid(TTF_Font* font,
                                     const Uint16 *text,
                                     SDL_Color fg)
{
    if (!font || !text)
        return NULL;

    if (text[0] == 0 || text[1] != 0)
        return NULL;

    SDL_Color bg;
    bg.r = 0;
    bg.g = 0;
    bg.b = 0;

    SDL_Surface *surface = TTF_RenderGlyph_Shaded(font, text[0], fg, bg);

    if (surface)
        SDL_SetColorKey(surface, SDL_SRCCOLORKEY,
                        (Uint32)SDL_MapRGB(surface->format, 0, 0, 0));

    return surface;
}

void TTF_Quit( void )
{
}
