/* Total-fit line breaking. See wrap.h for the contract and the reasoning;
 * this file is the decision only — no engine types, no globals, no state. */

#include "wrap.h"

/* A very large cost, meaning "this set of lines cannot be laid out". Chosen to
 * stay well inside int range so the doubled additions below cannot overflow. */
/* "cannot be laid out". A float sentinel rather than a large int: the costs are
 * now floats, and a finite large value could in principle be reached by a very
 * bad layout, so this is deliberately not comparable. */
static const float WRAP_INF = 1e30f;

/* How many line ends can be considered. The engine's own masks are bounded by
 * its text buffer, and a page is a few hundred characters at most, so a fixed
 * bound keeps this allocation-free (it matters on the handhelds this is aimed
 * at, where an alloc per page is a real cost). */
static const int WRAP_MAX_NODES = 512;

float wrap_badness(float slack, float room)
{
    if (room <= 0.0f) return 0.0f;
    if (slack < 0.0f) slack = 0.0f;
    /* Normalised to a percentage first, so the cube is comparable across line
     * widths and font sizes — a page budget for a handheld screen is a very
     * different pixel count from a desktop window, and the weights have to mean
     * the same thing on both. Then cubed: one very loose line costs more than
     * two slightly loose ones, which is what spreads the slack rather than
     * dumping it on the last line. */
    float r = (slack * 100.0f) / room;
    return r * r * r / 100.0f;
}

int wrap_total_fit(const WrapEngine *engine,
                   const WrapCandidate *candidates, int num_candidates,
                   int start, int end,
                   int *out, int out_cap)
{
    if (!engine || !engine->run_width || !engine->line_room ||
        !engine->line_used || !out || out_cap <= 0)
        return -1;
    if (end <= start)
        return 0;

    const int room = engine->line_room(engine->ctx);
    if (room <= 0)
        return -1;

    /* The first line is only as long as the room left on it: the carriage may
     * already be part-way across, because a clickwait pauses without moving it. */
    float first_room = room - engine->line_used(engine->ctx);
    if (first_room < 0.0f) first_room = 0.0f;

    /* Nodes are the line ends to choose among: the start, every candidate, and
     * the end of the segment. Node 0 is "nothing laid out yet", so a line spans
     * the text [pos[k], pos[j]) and best[j] is the least cost of covering
     * [start, pos[j]).
     *
     * The previous version of this in ONScripter-EN started the array at the
     * first candidate while treating best[0] as the pre-first-line state, so
     * every edge measured a line beginning one candidate too late: for anything
     * needing real layout there was no path, the call returned "no fit", and
     * the old greedy wrap ran instead. Keep node 0 at `start`. */
    int pos[WRAP_MAX_NODES];
    int m = 0;
    pos[m++] = start;
    for (int i = 0; i < num_candidates && m < WRAP_MAX_NODES - 1; i++) {
        int o = candidates[i].offset;
        if (o > start && o < end)
            pos[m++] = o;
    }
    if (m == 1)
        return 0;                    /* no legal break: one line, nothing to choose */
    if (pos[m-1] != end)
        pos[m++] = end;
    const int last = m - 1;
    if (last < 1)
        return 0;

    /* best[]/prev[] over nodes. Sized to the static bound, not `m`, so the
     * fixed arrays above are the only storage: no allocation at all. */
    float best[WRAP_MAX_NODES];
    int prev[WRAP_MAX_NODES];
    for (int i = 0; i < m; i++) { best[i] = WRAP_INF; prev[i] = -1; }
    best[0] = 0.0f;

    for (int j = 1; j <= last; j++) {
        for (int k = 0; k < j; k++) {
            if (best[k] == WRAP_INF) continue;
            /* This line covers [pos[k], pos[j]). */
            float width = engine->run_width(engine->ctx, pos[k], pos[j]);
            float line_room = (k == 0) ? first_room : room;
            if (width > line_room) continue;         /* too wide: not an edge */
            /* A paragraph's last line is short by nature; charging it would
             * make the pass fight the text. */
            float cost = (j == last) ? 0.0f
                                     : wrap_badness(line_room - width, line_room);
            if (best[k] + cost < best[j]) {
                best[j] = best[k] + cost;
                prev[j] = k;
            }
        }
    }

    if (best[last] == WRAP_INF)
        return -1;                   /* a single run is wider than a line */

    /* Walk the choice back from the end. pos[last] is the segment end: a line
     * end, but not a break offset, so it is not emitted. */
    int path[WRAP_MAX_NODES];
    int np = 0;
    for (int j = last; j > 0 && np < out_cap; ) {
        int k = prev[j];
        if (k < 0) break;
        if (j != last) path[np++] = pos[j];
        j = k;
    }
    for (int k = 0; k < np; k++)         /* path was collected backwards */
        out[k] = path[np - 1 - k];
    return np;
}
