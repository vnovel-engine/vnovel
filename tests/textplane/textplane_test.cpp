// Unit harness for the text-plane bridge (src/engine_bridge.[ch]).
//
// The plane travels engine -> shell through one shared sink, and the properties
// worth pinning are the ones a consumer would silently get wrong:
//
//   * an uninstalled sink is inert - publishing must not crash or allocate, so
//     an engine can publish unconditionally and still be correct;
//   * vnovel_text_plane_wanted() reports whether the work is worth doing, and
//     must agree with whether the callback actually fires;
//   * the fields arrive unchanged, including the padded pitch, which is the one
//     a consumer is most likely to assume equals width * 4;
//   * vnovel_publish_text_plane with a NULL plane is ignored rather than fatal.
//
// No engine, no SDL, no frames: it links the bridge only. That is the point -
// the bridge is the shared contract, so it should be testable without either
// engine or a frontend.

#include "engine_bridge.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what)
{
	if (!ok)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
	else
	{
		printf("ok:   %s\n", what);
	}
}

/* Records what the sink was handed, so the test can assert on the fields
 * rather than on a printf the reader has to eyeball. */
static int calls = 0;
static VNovelTextPlane last;
static std::vector<unsigned char> last_pixels;

static void recording_sink(const VNovelTextPlane *plane, void *userdata)
{
	(void)userdata;
	calls++;
	last = *plane;
	if (plane->pixels && plane->pitch > 0 && plane->height > 0)
	{
		size_t n = (size_t)plane->pitch * (size_t)plane->height;
		const unsigned char *p = (const unsigned char *)plane->pixels;
		last_pixels.assign(p, p + n);
	}
}

int main(void)
{
	/* --- inert until installed ------------------------------------------- */
	check(vnovel_text_plane_wanted() == 0,
	      "no sink installed: wanted() is 0");

	VNovelTextPlane probe;
	memset(&probe, 0, sizeof(probe));
	/* Publishing with no sink must be a no-op, not a crash: an engine that
	 * publishes unconditionally is expected to be correct. */
	vnovel_publish_text_plane(&probe);
	check(calls == 0, "no sink installed: publish is a no-op");

	vnovel_publish_text_plane(NULL);
	check(calls == 0, "NULL plane ignored even with a sink absent");

	/* --- installed ------------------------------------------------------- */
	vnovel_text_plane_cb       = recording_sink;
	vnovel_text_plane_userdata = (void *)0x1234; /* must arrive unchanged */

	check(vnovel_text_plane_wanted() == 1,
	      "sink installed: wanted() is 1");

	/* A padded pitch on purpose: width * 4 would be 12 for 3 pixels, and the
	 * contract says never to assume that. Pitch is deliberately larger. */
	const int W = 3, H = 2, PITCH = 16;
	unsigned char pixels[PITCH * H];
	for (int i = 0; i < PITCH * H; i++)
		pixels[i] = (unsigned char)i;

	VNovelTextPlane plane;
	memset(&plane, 0, sizeof(plane));
	plane.pixels    = pixels;
	plane.width     = W;
	plane.height    = H;
	plane.pitch     = PITCH;
	plane.changed_x = 1;
	plane.changed_y = 0;
	plane.changed_w = 2;
	plane.changed_h = 1;
	plane.origin_x  = 7;
	plane.origin_y  = 9;
	plane.restart   = 1;
	plane.serial    = 42;

	vnovel_publish_text_plane(&plane);

	check(calls == 1, "installed: the sink is called once per publish");
	check(last.width == W && last.height == H,
	      "geometry arrives unchanged");
	check(last.pitch == PITCH && last.pitch != W * 4,
	      "padded pitch arrives unchanged (not width * 4)");
	check(last.changed_x == 1 && last.changed_y == 0 &&
	      last.changed_w == 2 && last.changed_h == 1,
	      "changed rect arrives unchanged");
	check(last.origin_x == 7 && last.origin_y == 9,
	      "origin arrives unchanged");
	check(last.restart == 1, "restart flag arrives unchanged");
	check(last.serial == 42, "serial arrives unchanged");
	check(last_pixels.size() == (size_t)PITCH * H,
	      "every byte of the padded rows is passed on");
	check(last_pixels.size() == (size_t)PITCH * H &&
	      memcmp(last_pixels.data(), pixels, sizeof(pixels)) == 0,
	      "pixel bytes are byte-identical");

	vnovel_publish_text_plane(NULL);
	check(calls == 1, "NULL plane ignored when a sink is installed");

	/* --- detached again --------------------------------------------------- */
	vnovel_text_plane_cb = 0;
	check(vnovel_text_plane_wanted() == 0, "detached: wanted() is 0 again");
	vnovel_publish_text_plane(&plane);
	check(calls == 1, "detached: publish is a no-op again");

	printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",
	       failures);
	return failures ? 1 : 0;
}
