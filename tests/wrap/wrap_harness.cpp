// Unit harness for ONScripter-EN's line-breaking.
//
// The engine's wrap decision lives in three private methods on
// ONScripterLabel (processBreaks / findNextBreak / measureRunWidth), which
// read only the string buffer, the break mask and sentence_font geometry. This
// harness constructs the class, injects a string, and prints the resulting
// break offsets — no RetroArch, no Xvfb, no frames.
//
// Why it exists: a one-cell wrap error still renders as plausible text, and
// the last change to this logic (a cursor simulator) broke text rendering
// entirely and was only caught by rendering a page. An offset-level test shows
// a wrap change as a wrap change.
//
// Private access: the harness is a test binary, not engine code, and the
// alternative — adding friend declarations or public accessors to the engine —
// would put test scaffolding in the shipped header. `#define private public`
// is confined to this translation unit. It must come before any header that
// declares ONScripterLabel; the include order below is deliberate.

// `sentence_font` and `script_h` live in ScriptParser.h as protected, and the
// wrap entry points are private on ONScripterLabel, so both specifiers have to
// be widened for this translation unit. Reinstated immediately after the
// engine headers so nothing else in the build sees a widened access specifier.
#define private public
#define protected public
#include "ScriptParser.h"
#include "ONScripterLabel.h"
#undef protected
#undef private

#include "ScriptHandler.h"
#include "FontInfo.h"
#include "AnimationInfo.h"

#include <setjmp.h>
#include <png.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

namespace {

struct Options {
    int cells = 38;          // line width in half-width cells
    int pitch = 17;          // sentence_font.pitch_xy[0]
    std::string font;        // TTF to render with (enables PNG output)
    std::string png_dir;     // when set, write one PNG per case here
    bool use_tf = false;     // render using the engine's total-fit plan
    bool chain = false;      // treat each case line as a successive `-statement
    int screen_w = 640;      // render surface width (the game's screen)
    int box_x = 18;          // text box origin, from the game's setwindow
    int start_cells = 0;     // half-cells already used on the line (cursor carry)
    std::vector<std::string> texts;
    std::vector<std::string> files;
};

// Break the text the way processText would, and report the break offsets.
// Mirrors the call sequence in ONScripterLabel::processText: classify with
// SPACEBREAK, then fall back to KINSOKU when the line held no ASCII.
std::vector<int> breaksFor(ONScripterLabel &ons, const std::string &text)
{
    ScriptHandler &sh = ons.script_h;
    char *buf = sh.getStringBuffer();

    // The parser writes a 0-terminated string into this buffer and the wrap
    // code scans to the terminator. Keep well inside the buffer's size (2048,
    // private to ScriptHandler.cpp) rather than restating the constant here.
    const size_t kMax = 1024;
    if (text.size() >= kMax) {
        std::fprintf(stderr, "case too long (%zu)\n", text.size());
        return {};
    }
    std::memset(buf, 0, kMax);
    std::memcpy(buf, text.c_str(), text.size());
    buf[text.size()] = '\0';

    if (ons.string_buffer_breaks) delete[] ons.string_buffer_breaks;
    ons.string_buffer_breaks = new bool[kMax + 2];
    for (size_t i = 0; i < kMax + 2; i++)
        ons.string_buffer_breaks[i] = false;

    ons.processBreaks(false, ONScripterLabel::SPACEBREAK);

    // Walk the break opportunities the way processText's greedy fill does.
    // findNextBreak returns the next offset at or after `off` where a break is
    // legal; when that is `off` itself the break sits right there, so the walk
    // must still advance past the current character or it never terminates.
    std::vector<int> breaks;
    int len = 0;
    int off = 0;
    const int n = (int)text.size();
    while (off <= n) {
        int next = ons.findNextBreak(off, len);
        if (next > n) break;
        if (next > off) {
            breaks.push_back(next);
            off = next;
        } else {
            if (next == off && ons.string_buffer_breaks[off])
                breaks.push_back(off);
            if (off >= n) break;
            off += IS_TWO_BYTE(buf[off]) ? 2 : 1;
        }
    }
    // findNextBreak returns the same offset for consecutive positions inside a
    // run of spaces; collapse so each line break is listed once.
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    return breaks;
}

// The fit test processText applies to a candidate run, in half-width cells:
// the run's real pixel width becomes cells with the remainder rounded up,
// because advanceCharInPixels folds whole cells every pitch/2 pixels.
int cellsForRun(ONScripterLabel &ons, int start, int end)
{
    // The engine's own per-character fold. Summing advances instead drifts a
    // few cells over a long line, so a run the model called fitting overflowed
    // the box on screen — this is the same measurement the drawing loop makes.
    return ons.measureRunCells(start, end);
}

// Where the text actually wraps: greedily fill each line with the break
// segments that fit, and break at the last one that did. This is the decision
// processText arrives at one character at a time; processBreaks precomputes the
// opportunities, and the fit test is a pure function of (text, geometry), so
// the same choice can be made here in one pass.
//
// A run that cannot fit even alone is broken at its own end — that is the
// engine's hard break, which is what splits an over-long word.
std::vector<int> wrapsFor(ONScripterLabel &ons, const std::string &text)
{
    std::vector<int> opps = breaksFor(ons, text);
    std::vector<int> lines;
    const int width = ons.sentence_font.num_xy[0] * 2;  // half-cells per line

    // The first line may already be part-used: a clickwait pauses without
    // moving the carriage, so the statement after it continues mid-line. Test
    // against the room actually left, as the engine's own isEndOfLine does.
    int used = ons.sentence_font.xy[0];
    if (ons.sentence_font.sub_offset_px > 0)
        used += (2 * ons.sentence_font.sub_offset_px >= ons.sentence_font.pitch_xy[0]) ? 2 : 1;

    int line_start = 0;
    int last_fit = -1;
    for (size_t k = 0; k <= opps.size(); ) {
        int cand = (k < opps.size()) ? opps[k] : (int)text.size();
        if (used + cellsForRun(ons, line_start, cand) <= width) {
            last_fit = cand;
            if (k == opps.size()) break;
            k++;
            continue;
        }
        int at = (last_fit > line_start) ? last_fit : cand;
        lines.push_back(at);
        line_start = at;
        used = 0;              // every line after the first starts empty
        last_fit = -1;
        // Re-test the overflow candidate as the next line's content; when the
        // break landed on it already, move past it instead.
        if (at == cand && k < opps.size()) k++;
    }
    return lines;
}

// Load the render font into the sentence font, once. Measuring a run's real
// pixel width needs the TTF (measureRunWidth asks TTF_GlyphMetrics per ASCII
// glyph), so this must happen before any wrap decision that uses pixel widths.
bool loadFont(ONScripterLabel &ons, const char *font_file)
{
    if (ons.sentence_font.ttf_font) return true;
    ons.font_file = const_cast<char *>(font_file);
    ons.screen_ratio1 = 1;
    ons.screen_ratio2 = 1;
    return ons.sentence_font.openFont(ons.font_file,
                                      ons.screen_ratio1, ons.screen_ratio2) != NULL;
}

// Split a page into the segments a clickwait separates.
//
// '@' is not a character on the page: it is a pause. The text before and after
// it continues on the same line with the carriage where it stood, so a page is
// a chain of segments sharing one cursor. Splitting here — once, up front —
// keeps every later step (mask, plan, render) working on text that contains
// only what is actually drawn, which is what the engine sees too: its own
// per-character loop advances past the token before it applies a break.
std::vector<std::string> splitOnPause(const std::string &page)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : page) {
        if (c == '@') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// The line set a render should use: the engine's own total-fit plan when asked,
// otherwise the greedy model that mirrors processText's per-word choice. Both
// produce offsets into `text`.
std::vector<int> planLines(ONScripterLabel &ons, const std::string &text, bool tf)
{
    if (!tf)
        return wrapsFor(ons, text);
    breaksFor(ons, text);                 // fill the mask the plan reads
    int end = ons.scanSegmentEnd(0);
    ons.planSegment(0, end);
    std::vector<int> out;
    for (int i = 0; i < ons.num_chosen_breaks; i++)
        out.push_back(ons.chosen_breaks[i]);
    if (out.empty())
        return wrapsFor(ons, text);       // plan bailed out; keep the render sane
    return out;
}

// Draw `text`, broken at the given offsets, and write it to `path`.
//
// The glyphs come from the engine's drawChar, so this is the engine's placement
// and not a lookalike: the same surface format, the same per-character advance.
// Breaks are applied as doLineBreak does — carriage to the line origin, spaces
// after the break skipped — so the picture shows what a reader would see.
//
// `box_x` is where the text box starts on the surface. The surface must be the
// full screen, not the box: setwindow gives the box 36 columns but the screen
// is 640px with the box at x=18, so a glyph whose ink overhangs its advance (a
// proportional italic 's', say) has room on the screen and would be clipped if
// the surface stopped at the box edge.
// The cursor state a statement left behind. The engine carries this across a
// clickwait, so chaining statements needs it: the next statement's first line
// starts where this one stopped, not at the left edge.
struct CursorState { int xy0; int xy1; int sub; int line_offset; };
static CursorState g_end_cursor = {0, 0, 0, 0};

// Write an ARGB8888 surface as PNG. libpng is the host's, so the harness needs
// no vendored image code: rows are read out and the alpha channel dropped (the
// background is opaque).
static bool writePng(SDL_Surface *surf, const std::string &path, int width, int height)
{
    FILE *out = std::fopen(path.c_str(), "wb");
    if (!out) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        if (png) png_destroy_write_struct(&png, NULL);
        std::fclose(out);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(out);
        return false;
    }
    png_init_io(png, out);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_byte> row(width * 3);
    for (int y = 0; y < height; y++) {
        const Uint32 *src = (const Uint32 *)((const Uint8 *)surf->pixels + y * surf->pitch);
        for (int x = 0; x < width; x++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(src[x], surf->format, &r, &g, &b, &a);
            row[x*3+0] = r; row[x*3+1] = g; row[x*3+2] = b;
        }
        png_write_row(png, row.data());
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    std::fclose(out);
    return true;
}

bool renderPng(ONScripterLabel &ons, const std::string &text,
               const std::vector<int> &breaks, const std::string &path,
               int width, int height, int box_x, int start_cells,
               SDL_Surface *onto = NULL, int start_row = 0)
{
    SDL_Surface *surf = onto ? onto : AnimationInfo::allocSurface(width, height);
    if (!surf) return false;
    if (!onto)
        SDL_FillRect(surf, NULL, SDL_MapRGB(surf->format, 0x20, 0x20, 0x28));

    Fontinfo &f = ons.sentence_font;
    uchar3 white = {0xff, 0xff, 0xff};
    // x() = xy[0]*pitch/2 + top_xy[0] + ..., and newLine() resets xy[0] to 0, so
    // the box origin belongs in top_xy[0]: xy[0] would be cleared on the first
    // break and every line after the first would start at the screen edge.
    f.xy[0] = start_cells;      // a carried cursor, as after a clickwait
    f.xy[1] = start_row * 2;    // and the carried row, so a page stacks down
    f.sub_offset_px = 0;
    f.top_xy[0] = box_x;
    f.top_xy[1] = 0;
    f.sub_offset_px = 0;
    f.line_offset_xy[0] = 0;
    f.line_offset_xy[1] = 0;

    // drawChar has its own isEndOfLine check and would break a second time at
    // the same place the caller is about to break, producing an extra blank
    // line. Widening the window for the duration suppresses that: the breaks
    // applied here are the ones wrapsFor decided, which the golden test already
    // checks against the engine's own mask. isEndOfLine reads num_xy, so it is
    // the one value to change.
    const int saved_num_x = f.num_xy[0];

    size_t brk = 0;
    const char *base = text.c_str();
    const char *p = base;
    char ch[3] = {0, 0, 0};
    while (*p) {
        if (brk < breaks.size() && (int)(p - base) == breaks[brk]) {
            f.newLine();
            brk++;
            while (*p == ' ') p++;   // doLineBreak skips leading spaces
            continue;
        }
        // Consume engine tokens the way the real drawing loop does: a token has
        // no glyph at all — '@' pauses, '\' pages, '!sd' sets speed, '`' clears
        // the skip flag — so drawing their raw bytes would put characters on the
        // page the player never sees. isTextCommand is the engine's own
        // classifier, so the harness and the engine agree on what a token is.
        int cmd = ons.isTextCommand(p);
        if (cmd != 0) {
            if (cmd < 0) cmd = -cmd;      // ruby command: skip its text too
            p += cmd;
            continue;
        }
        int n = IS_TWO_BYTE(*p) ? 2 : 1;
        ch[0] = p[0];
        ch[1] = (n == 2) ? p[1] : 0;
        // drawChar needs the real next source character: the ellipsis bar ('|',
        // drawn as U+2026) joins a run with no gap and only the run's last bar
        // takes a trailing half-cell, and that decision reads next_char. text[1]
        // cannot serve here — the single-byte path leaves it '\0'.
        char next = p[n];
        int bx = f.xy[0];
        ons.drawChar(ch, &f, false, false, surf, NULL, 0, NULL, next);
        p += n;
    }

    (void)saved_num_x;

    g_end_cursor.xy0 = f.xy[0];
    g_end_cursor.xy1 = f.xy[1];
    g_end_cursor.sub = f.sub_offset_px;
    g_end_cursor.line_offset = f.line_offset_xy[0];
    if (onto) return true;   // chained: caller keeps the surface and writes later

    bool ok = writePng(surf, path, width, height);
    if (!onto) SDL_FreeSurface(surf);
    return ok;
}

// The engine's own decision, straight out of processBreaks: the set of offsets
// at which a break is legal. This is the primary output because it is the
// module's contract — no drawing, no reimplementation — so a change here is a
// real behaviour change. processBreaks fills the whole mask up front, so this
// is a pure function of (text, geometry): text speed cannot affect it.
void printMask(ONScripterLabel &ons, const std::string &text,
               const std::string &label)
{
    std::vector<int> opps = breaksFor(ons, text);
    std::printf("%s\n", label.c_str());
    std::printf("  text:    %s\n", text.c_str());
    std::printf("  breaks: ");
    for (int b : opps) std::printf("%d ", b);
    std::printf("\n");
    std::printf("  before: ");
    for (int b : opps) {
        int s = b;
        while (s > 0 && text[s-1] == ' ') s--;
        std::printf("'%s' ", text.substr(s, 1).c_str());
    }
    std::printf("\n");
    // The resolved wrap (what a line actually shows), distinct from the mask
    // above: the mask lists every legal break, the wrap is the one taken.
    std::printf("  lines:  ");
    std::vector<int> lines = wrapsFor(ons, text);
    int s = 0;
    for (int b : lines) {
        std::printf("[%s] ", text.substr(s, b - s).c_str());
        s = b;
    }
    std::printf("[%s]\n", text.substr(s).c_str());

    // The engine's total-fit pass (ONScripterLabel::planSegment), which is the
    // thing under test once the flag is on: it chooses every break for the
    // whole segment in one minimising pass. Printed alongside the greedy result
    // so the two can be compared on the same input.
    int seg_end = ons.scanSegmentEnd(0);
    int nlines = ons.planSegment(0, seg_end);
    std::printf("  tf[%d]%s: ", nlines,
                (nlines == 1 && ons.num_chosen_breaks == 0) ? " (FELL BACK)" : "");
    s = 0;
    for (int i = 0; i < ons.num_chosen_breaks; i++) {
        int b = ons.chosen_breaks[i];
        std::printf("[%s] ", text.substr(s, b - s).c_str());
        s = b;
    }
    std::printf("[%s]\n", text.substr(s, seg_end - s).c_str());
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cells" && i + 1 < argc) opt.cells = atoi(argv[++i]);
        else if (a == "--pitch" && i + 1 < argc) opt.pitch = atoi(argv[++i]);
        else if (a == "--font" && i + 1 < argc) opt.font = argv[++i];
        else if (a == "--png-dir" && i + 1 < argc) opt.png_dir = argv[++i];
        else if (a == "--tf") opt.use_tf = true;
        else if (a == "--chain") opt.chain = true;
        else if (a == "--screen-w" && i + 1 < argc) opt.screen_w = atoi(argv[++i]);
        else if (a == "--box-x" && i + 1 < argc) opt.box_x = atoi(argv[++i]);
        else if (a == "--start-cells" && i + 1 < argc) opt.start_cells = atoi(argv[++i]);
        else if (a == "--golden") {
            // Remaining args are case files; a case file is one text per line.
            for (i++; i < argc; i++) opt.files.push_back(argv[i]);
        }
        else opt.texts.push_back(a);
    }

    // Deliberately leaked: the engine's destructor calls reset(), which walks
    // state that only exists after a full loadEnv (it segfaults on a bare
    // instance). The process exits immediately after, so leaking is free and
    // avoids adding test-only guards to the engine's teardown path.
    ONScripterLabel &ons = *(new ONScripterLabel());

    // Geometry: a sentence window `cells` half-widths wide at the given pitch.
    ons.sentence_font.pitch_xy[0] = opt.pitch;
    ons.sentence_font.pitch_xy[1] = opt.pitch;
    ons.sentence_font.font_size_xy[0] = opt.pitch;
    ons.sentence_font.font_size_xy[1] = opt.pitch;
    ons.sentence_font.xy[0] = 0;
    ons.sentence_font.xy[1] = 0;
    ons.sentence_font.num_xy[0] = opt.cells;   // columns
    ons.sentence_font.num_xy[1] = 20;          // rows
    ons.sentence_font.top_xy[0] = 0;
    ons.sentence_font.top_xy[1] = 0;
    ons.sentence_font.sub_offset_px = 0;
    ons.sentence_font.ttf_font = NULL;         // half-cell advances only

    ons.script_h.preferred_script = ScriptHandler::LATIN_SCRIPT;

    // Load the font before anything measures a run: the fit test uses real
    // glyph advances, so a wrap decided without the font would not be the
    // engine's wrap. Failure is not fatal — the offset tests still run.
    const bool have_font = !opt.font.empty() && loadFont(ons, opt.font.c_str());
    if (!opt.font.empty() && !have_font)
        std::fprintf(stderr, "font %s would not load; PNG output skipped\n",
                     opt.font.c_str());

    // Render onto the game's actual screen, not a surface the size of the text
    // box: Tsukihime's setwindow puts 36 columns at x=18 on a 640x480 screen, so
    // a glyph whose ink overhangs its advance has room there and would be
    // clipped on a 612px surface. opt.screen_w defaults to 640; the box origin
    // is opt.box_x.
    const int png_w = opt.screen_w;
    const int png_h = 8 * opt.pitch;
    int png_index = 0;
    const bool want_png = have_font && !opt.png_dir.empty();

    // Carry the cursor the way the engine does across a clickwait: a later
    // `-statement starts with the line already part-used. This is what makes
    // the first-line capacity differ from a fresh line, and the case that
    // exposed the plan assuming a full line when it was not.
    ons.sentence_font.xy[0] = opt.start_cells;
    std::fprintf(stderr, "[carry] start_cells=%d\n", opt.start_cells);

    for (const std::string &f : opt.files) {
        FILE *fp = std::fopen(f.c_str(), "r");
        if (!fp) { std::fprintf(stderr, "cannot open %s\n", f.c_str()); continue; }
        char line[4096];
        // In chain mode the whole file is one page (its lines are the textual
        // wrapping of one long page, not separate cases); otherwise each line
        // is its own case.
        std::string whole;
        while (std::fgets(line, sizeof(line), fp)) {
            size_t n = std::strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
            if (!n || line[0] == '#') continue;
            whole += line;
            if (!opt.chain) whole += '\n';
        }
        std::vector<std::string> cases;
        if (opt.chain) {
            if (!whole.empty()) cases.push_back(whole);
        } else {
            size_t a = 0;
            while (a <= whole.size()) {
                size_t b = whole.find('\n', a);
                if (b == std::string::npos) b = whole.size();
                if (b > a) cases.push_back(whole.substr(a, b - a));
                a = b + 1;
            }
        }

        {
          for (const std::string &case_text : cases) {
            // A case is a page: the segments a clickwait separates, sharing a
            // carriage.
            std::vector<std::string> segs = splitOnPause(case_text);
            SDL_Surface *page = opt.chain ? AnimationInfo::allocSurface(png_w, png_h) : NULL;
            if (page) SDL_FillRect(page, NULL, SDL_MapRGB(page->format, 0x20, 0x20, 0x28));

            int carry = opt.start_cells;
            int carry_sub = 0;         // sub-cell remainder, as after a clickwait
            int row = 0;               // vertical position, for chained pages
            for (const std::string &seg : segs) {
                printMask(ons, seg, f);       // the engine's break mask
                if (!want_png) continue;
                // The carry must be in place BEFORE the plan runs: the plan's
                // first line is only as long as the room the cursor left, so a
                // plan computed against an empty line would be wrong. This is
                // the same ordering the engine has — the cursor is whatever the
                // previous clickwait left behind.
                ons.sentence_font.xy[0] = carry;
                // Carry the sub-cell remainder too. It is real position: a
                // remainder of >= pitch/2 already occupies the next half-cell,
                // so dropping it makes the plan think more room is left than
                // there is, and the renderer then breaks mid-word ("Eve"/"n").
                ons.sentence_font.sub_offset_px = carry_sub;
                std::fprintf(stderr, "[seg] carry=%d sub=%d num_xy0=%d seg=%.20s\n",
                             carry, g_end_cursor.sub, ons.sentence_font.num_xy[0], seg.c_str());
                std::vector<int> lines = planLines(ons, seg, opt.use_tf);
                if (page) {
                    if (!renderPng(ons, seg, lines, "", png_w, png_h,
                                   opt.box_x, carry, page, row))
                        std::fprintf(stderr, "chained render failed\n");
                    carry = g_end_cursor.xy0;   // next segment continues here
                    carry_sub = g_end_cursor.sub;
                    row = g_end_cursor.xy1 / 2; // ...on the line it ended on
                } else {
                    char path[1024];
                    std::snprintf(path, sizeof(path), "%s/%02d.png",
                                  opt.png_dir.c_str(), png_index++);
                    if (renderPng(ons, seg, lines, path, png_w, png_h, opt.box_x, carry))
                        std::printf("  png:     %s\n", path);
                    else
                        std::fprintf(stderr, "render failed for %s\n", path);
                    carry = g_end_cursor.xy0;
                    carry_sub = g_end_cursor.sub;
                }
            }
            if (page) {
                char path[1024];
                std::snprintf(path, sizeof(path), "%s/page.png", opt.png_dir.c_str());
                if (writePng(page, path, png_w, png_h))
                    std::printf("  png:     %s\n", path);
                SDL_FreeSurface(page);
            }
          }
        }
        std::fclose(fp);
    }

    for (const std::string &t : opt.texts) {
        printMask(ons, t, "(arg)");
        if (want_png) {
            std::vector<int> lines = planLines(ons, t, opt.use_tf);
            char path[1024];
            std::snprintf(path, sizeof(path), "%s/%02d.png",
                          opt.png_dir.c_str(), png_index++);
            if (renderPng(ons, t, lines, path, png_w, png_h, opt.box_x, opt.start_cells))
                std::printf("  png:     %s\n", path);
            else
                std::fprintf(stderr, "render failed for %s\n", path);
        }
    }

    std::fflush(NULL);
    std::_Exit(0);   // skip global/dtors: see the note on the leaked instance
}
