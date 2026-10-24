#include <SDL_libretro.h>
#include <retro_miscellaneous.h>
#include <file/file_path.h>
#include <encodings/crc32.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
/* backtrace()/backtrace_symbols_fd() are glibc extensions: unavailable on
 * Windows, and macOS spells its equivalent differently. Guard the whole
 * symbolication path so the core builds and runs everywhere; without it the
 * handler still logs the signal and fault address, which is the part that
 * matters most. */
#if defined(__GLIBC__)
#include <execinfo.h>
#define ONS_HAVE_BACKTRACE 1
#else
#define ONS_HAVE_BACKTRACE 0
#endif
/* Vendored fallback font, generated from resources/fonts/ by the Makefile;
 * registered in retro_init for glyphs the game fonts lack. */
#include "fallback_font.h"
#include "extra_fallback_font.h"

/* The development socket: the text stream out, navigation intents in. See
 * devsock.h. Off unless VNOVEL_DEV_PORT names a port. */
#include "devsock.h"

/* Crash diagnostics: on SIGSEGV/SIGBUS/SIGABRT dump the fault address and a
 * symbolicated backtrace to the crash log, then re-raise so the default
 * action still kills the process. Core crashes otherwise surface only as
 * RetroArch's "Process exitcode: -11" with no address; this makes them
 * diagnosable (`aarch64-…-addr2line -f -C -e vnovel_libretro.so`). */
/* Portable crash-log path. Hardcoding "/tmp/..." is not portable (Windows and
 * some sandboxed frontends have no /tmp); prefer the core's writable save
 * directory, which the frontend provides and which exists on every platform,
 * and keep a fixed relative name as a last-resort fallback. Computed once at
 * load time so the handler itself does no work beyond formatting and writing.
 *
 * Signal-safety caveat: format/write/backtrace_symbols_fd are not
 * async-signal-safe (they may allocate), so this is best-effort; a fault
 * inside the handler loses the dump by design, which is acceptable for a
 * diagnostic path. */
static char ons_crash_log_path[PATH_MAX_LENGTH] = "ons_crash.log";

/* --- Path canonicalization -------------------------------------------------
 * The core holds one internal path form: '/' separators, no trailing
 * separator. A frontend hands back native paths (on Windows, '\'), and the
 * engines open files and directories with whatever they are given, so a
 * canonical path is realized — separators translated for the platform — only
 * at the boundary where it is handed to an engine (or used for the crash log).
 * Every site in between (splitting a game name off a directory, nesting the
 * per-game save dir, building a probe path) then works in the one form and
 * needs no platform knowledge. */
static void canon_path(char *out, size_t n, const char *in)
{
   if (!out || n == 0)
      return;
   if (!in)
   {
      out[0] = '\0';
      return;
   }
   size_t j = 0;
   for (size_t i = 0; in[i] && j + 1 < n; i++)
      out[j++] = (in[i] == '\\') ? '/' : in[i];
   while (j > 1 && out[j - 1] == '/')
      j--;
   out[j] = '\0';
}
/* Realize a canonical path for the platform. Returns `scratch` (a native copy)
 * on Windows, or `canon` itself where the native form is already canonical. */
static const char *realize_path(const char *canon, char *scratch, size_t n)
{
#ifdef _WIN32
   if (!scratch || n == 0)
      return canon;
   size_t j = 0;
   for (size_t i = 0; canon[i] && j + 1 < n; i++)
      scratch[j++] = (canon[i] == '/') ? '\\' : canon[i];
   scratch[j] = '\0';
   return scratch;
#else
   (void)scratch;
   (void)n;
   return canon;
#endif
}

static void ons_set_crash_log_path(const char *dir)
{
   if (dir && dir[0])
   {
      char cdir[PATH_MAX_LENGTH];
      canon_path(cdir, sizeof(cdir), dir);
      fill_pathname_join(ons_crash_log_path,
                         cdir,
                         "ons_crash.log",
                         sizeof(ons_crash_log_path));
   }
}

/* --- Game identity ---------------------------------------------------------
 * The id a game is known by, everywhere in this project: the crc32 of the file
 * the core uses for engine detection (data.xp3 on the KiriKiri path), as eight
 * upper-case hex digits. That is exactly what tools/hash-game.py writes into a
 * dat/vnovel.dat record and what vnovel.rdb is indexed on, so the engine's
 * per-game compat table can be keyed by a value the database and the tools
 * already agree on — one game, one id, no second fingerprint scheme.
 *
 * Returns `out` on success, NULL when the file cannot be read (in which case
 * the game simply has no table entry and the engine falls back to its
 * default era). */
static const char *game_id_for_file(const char *path, char *out, size_t n)
{
   if (!path || !path[0] || !out || n < 9)
      return NULL;
   FILE *f = fopen(path, "rb");
   if (!f)
      return NULL;
   unsigned char *buf = (unsigned char *)malloc(1 << 20);
   if (!buf)
   {
      fclose(f);
      return NULL;
   }
   uint32_t crc = 0;
   size_t got;
   while ((got = fread(buf, 1, 1 << 20, f)) > 0)
      crc = encoding_crc32(crc, buf, got);
   free(buf);
   bool err = ferror(f) != 0;
   fclose(f);
   if (err)
      return NULL;
   snprintf(out, n, "%08X", crc);
   return out;
}

#if !defined(_WIN32)
static void ons_crash_handler(int sig, siginfo_t *si, void *ctx)
{
   (void)ctx;
   static volatile int reentry = 0;
   if (reentry)
   {
      signal(sig, SIG_DFL);
      raise(sig);
      return;
   }
   reentry = 1;
#else
/* signal() on Windows passes only the signal number. */
static void ons_crash_handler(int sig)
{
   static volatile int reentry = 0;
   if (reentry)
   {
      signal(sig, SIG_DFL);
      raise(sig);
      return;
   }
   reentry             = 1;
   const void *si_addr = NULL;
#endif

   int fd = open(ons_crash_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd >= 0)
   {
      char buf[256];
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
#if !defined(_WIN32)
      const void *crash_addr = si ? si->si_addr : NULL;
#else
      const void *crash_addr = si_addr;
#endif
      int n = snprintf(buf,
                       sizeof buf,
                       "=== signal %d (%s) addr=%p at %lld.%03lds ===\n",
                       sig,
                       sig == SIGSEGV ? "SIGSEGV" :
#ifdef SIGBUS
                       sig == SIGBUS ? "SIGBUS"
                       :
#endif
                       sig == SIGABRT ? "SIGABRT"
                                      : "signal",
                       crash_addr,
                       (long long)ts.tv_sec,
                       ts.tv_nsec / 1000000);
      if (n > 0)
         write(fd, buf, n);
#if ONS_HAVE_BACKTRACE
      void *bt[64];
      int frames = backtrace(bt, 64);
      backtrace_symbols_fd(bt, frames, fd);
#else
      /* No symbolication on this platform; the address above is still the
     * most useful single fact for a crash report. */
      const char *note = "(no backtrace: platform lacks backtrace())\n";
      write(fd, note, strlen(note));
#endif
      close(fd);
   }
   signal(sig, SIG_DFL);
   raise(sig);
}

#include "libretro_core_options.h"
#include "engine_cabi.h"
#include "engine_bridge.h"

#ifdef _WIN32
#define ons_mkdir(path) mkdir(path)
#else
#define ons_mkdir(path) mkdir(path, 0755)
#endif

// Both engines call exit() on script errors and SDL init failures.
// We catch these via longjmp. errorAndExit() calls the engine-specific
// longjmp function which jumps back to the engine thread's setjmp checkpoint.
static void fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t log_cb = fallback_log;
/* The setjmp/longjmp storage lives in the bridge module (static storage, one
 * copy per process). The ENGINE runs both setjmp and longjmp on its own thread
 * — longjmp must not cross threads — so the core only provides the storage and
 * observes the valid flag. See engine_bridge.c. */

//don't use #include <onscripter/ONScripter.h>, use #include <ONScripterLabel.h> instead (stop including wrong header file)
#include <ONScripterLabel.h>

/* SDL_libretro_input_state_cb is defined in the bridge module (engine_bridge.c)
 * so the engine modules and the glue all resolve it to one object. */
static retro_input_state_t
      frontend_input_state_cb; /* the real frontend callback */
bool SDL_libretro_input_ready = false;

/* Virtual pointer (pad->pointer synthesis) options. Declared before the
 * synthesizer that reads them: enabled by default so pointer-only UIs stay
 * reachable out of the box; the stick follows the usual right-stick
 * convention. */
enum
{
   POINTER_SOURCE_RIGHT = 0,
   POINTER_SOURCE_LEFT  = 1
};
static bool opt_pointer_enable = true;
static int opt_pointer_source  = POINTER_SOURCE_RIGHT;

/* Engine-agnostic RetroPad -> pointer synthesis.
 *
 * RetroArch has no built-in pad-to-pointer mapping (its per-port "Mouse Index"
 * only binds real mouse devices, and its only synthesized pointer source is
 * touch -> vmouse), and some visual-novel UIs — modal Yes/No dialogs in
 * particular — are effectively pointer-only. Rather than patch each engine,
 * synthesize one here, in the core. Both the ONScripter path (via the SDL1.2
 * overlay) and the KiriKiri path (via krkr_set_input_callbacks) call
 * SDL_libretro_input_state_cb, so both get it. The synthesizer reports MOUSE
 * deltas (which both engines already integrate) and a MOUSE_LEFT press for the
 * stick click, so no engine-side change is needed.
 *
 * Whether the pointer is offered, and which stick drives it, are core options
 * (vnovel_pointer_enable / _pointer_source): the core cannot know the
 * user's pad, so refusing to guess is the point. */
static int vlx_pos = -1,
           vly_pos = -1; /* virtual pointer accumulator, -1 = unset */

/* The same pointer as an absolute position, to hand to an engine that prefers
 * one over deltas. Kept alongside the accumulator rather than derived from it:
 * the two have different origins, and the engine reads whichever its input path
 * prefers.
 *
 * The units are libretro's, which are the raw int16 the frontend returns - an
 * engine decodes them as (value + 32768) over 0..65535, so the centre here is 0,
 * not 32768. Getting that wrong railed the cursor at once.
 *
 * This is the form a position-only UI needs. A click carries no delta, so an
 * engine that only integrates deltas has nothing to aim a click with; an
 * absolute pointer can be walked onto a link or a menu item and then clicked. */
static int vabs_x = 0, vabs_y = 0;

/* The native menu (src/menu.h). */
#include "menu.h"

/* Synthesize pointer/mouse reads from the stick chosen by opt_pointer_source.
 * Returns the value for (device,id), or defers to the frontend. Clicking uses
 * the matching stick button (L3 for the left stick, R3 for the right), so the
 * "click" input follows the stick the user picked. */
static int16_t pointer_synth_input_state(unsigned port,
                                         unsigned device,
                                         unsigned index,
                                         unsigned id)
{
   if (!opt_pointer_enable || port != 0 ||
       (device != RETRO_DEVICE_MOUSE && device != RETRO_DEVICE_POINTER))
      return frontend_input_state_cb(port, device, index, id);

   bool left_stick       = (opt_pointer_source == POINTER_SOURCE_LEFT);
   unsigned analog_index = left_stick ? RETRO_DEVICE_INDEX_ANALOG_LEFT
                                      : RETRO_DEVICE_INDEX_ANALOG_RIGHT;
   unsigned stick_btn =
         left_stick ? RETRO_DEVICE_ID_JOYPAD_L3 : RETRO_DEVICE_ID_JOYPAD_R3;

   int16_t sx = frontend_input_state_cb(
         0, RETRO_DEVICE_ANALOG, analog_index, RETRO_DEVICE_ID_ANALOG_X);
   int16_t sy = frontend_input_state_cb(
         0, RETRO_DEVICE_ANALOG, analog_index, RETRO_DEVICE_ID_ANALOG_Y);
   int16_t click =
         frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, stick_btn);
   int moved = 0;
   if (sx > 4000 || sx < -4000)
   {
      vlx_pos = (vlx_pos < 0 ? 0 : vlx_pos) + sx / 4096;
      moved   = 1;
   }
   if (sy > 4000 || sy < -4000)
   {
      vly_pos = (vly_pos < 0 ? 0 : vly_pos) + sy / 4096;
      moved   = 1;
   }
   /* The absolute pointer follows the same stick. The rate is chosen so the
    * full 0..65535 range is crossed in about as long as the delta accumulator
    * takes to cross the screen (~1.7 s at full deflection): the deltas move
    * sx/4096 pixels per poll, and 65536/800 is about 82 of those steps. */
   /* Full deflection moves the whole range in about two seconds at 60 Hz, which
    * is the same feel as the delta accumulator's sweep and is fine enough to
    * land on a menu row. */
   if (sx > 4000 || sx < -4000)
   {
      vabs_x += sx / 1024;
      if (vabs_x < -32768) vabs_x = -32768;
      if (vabs_x > 32767) vabs_x = 32767;
   }
   if (sy > 4000 || sy < -4000)
   {
      vabs_y -= sy / 1024; /* the stick's Y grows up, libretro's grows down */
      if (vabs_y < -32768) vabs_y = -32768;
      if (vabs_y > 32767) vabs_y = 32767;
   }

   if (device == RETRO_DEVICE_MOUSE)
   {
      if (id == RETRO_DEVICE_ID_MOUSE_X)
      {
         int16_t r = frontend_input_state_cb(port, device, index, id);
         return r ? r : (moved ? (int16_t)(sx / 4096) : 0);
      }
      if (id == RETRO_DEVICE_ID_MOUSE_Y)
      {
         int16_t r = frontend_input_state_cb(port, device, index, id);
         return r ? r : (moved ? (int16_t)(sy / 4096) : 0);
      }
      if (id == RETRO_DEVICE_ID_MOUSE_LEFT)
         return frontend_input_state_cb(port, device, index, id) || click;
   }
   if (device == RETRO_DEVICE_POINTER &&
       (id == RETRO_DEVICE_ID_POINTER_X || id == RETRO_DEVICE_ID_POINTER_Y))
   {
      /* The frontend's absolute pointer, when it actually carries a position.
       *
       * RetroArch with a mouse bound reports POINTER_COUNT > 0 but answers X/Y
       * with 0,0 - it supplies mouse *deltas* and no absolute position at all.
       * Handing that on is worse than useless: an engine reads 0,0 as the
       * centre of the screen (libretro's encoding is raw int16, so 0 IS the
       * middle), and either parks its cursor there or, guarded against that,
       * ignores the pointer entirely and integrates deltas from an origin it
       * cannot know. Measured, that put the cursor a constant (+240,-152) away
       * from the physical pointer, which is why the game's links could not be
       * clicked - the cursor could be moved but never aimed.
       *
       * So the frontend is believed only when it names a position, and the
       * synthesized pointer otherwise. The two agree at rest (both are the
       * centre), so nothing moves until the stick is used. */
      int16_t fx = frontend_input_state_cb(
            0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
      int16_t fy = frontend_input_state_cb(
            0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
      if (fx != 0 || fy != 0)
         return id == RETRO_DEVICE_ID_POINTER_X ? fx : fy;
      if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)vabs_x;
      if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)vabs_y;
   }

   return frontend_input_state_cb(port, device, index, id);
}

/* The one callback installed into every input path. Keeps the synthesizer
 * optional and clearly separated from plain forwarding: this dispatcher owns
 * "how the core answers an input read", and pointer_synth_input_state owns the
 * one synthesized source. Future sources (e.g. a keyboard-to-pad shim) belong
 * next to it, composed here, not by growing the synthesizer. */
/* Whether the menu owned the input for the frame now in progress.
 *
 * Latched at the top of retro_run rather than read live here, because the menu
 * is fed before the engine and a click *outside* it closes it during that call.
 * Reading `vnovel_menu_should_draw()` live would then be false by the time the
 * engine polls, and the very click that dismissed the menu would fall through
 * onto whatever the game has under it. Latching at frame start means the frame
 * that closes the menu still suppresses, and the next frame no longer does. */
static int menu_owns_input = 0;

/* The bottom-right hot zone's synthesized right-click, in frames.
 *
 * A countdown rather than a flag: the engines' SDL shim synthesizes a whole
 * press/release pair from what core_input_state_cb reports, so reporting the
 * press for a few frames and then nothing is what makes it emit a complete
 * click. Written on the frontend's thread, read from the engine's through
 * core_input_state_cb -- the same funnel everything else uses. */
static int hotzone_right_frames = 0;

static int16_t
core_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
   /* The hot zone's click, delivered as a RIGHT-click: this is what both engines
    * already treat as "open my own menu", with the game's styling, its
    * menuselectvoice sounds and its modal handling. Reporting it here rather than
    * pushing an SDL event is deliberate: the engines' SDL shim polls this
    * callback (PumpMouseEvents reads RETRO_DEVICE_ID_MOUSE_RIGHT) and calls
    * SDL_PrivateMouseButton itself, so this is the channel that becomes a real
    * engine menu, through the engine's own path.
    *
    * Left is explicitly suppressed: the shim prefers whichever button it sees,
    * and the physical left click that opened the corner must not ALSO reach the
    * game (it would satisfy whatever the script is waiting on). */
   if (hotzone_right_frames > 0 && device == RETRO_DEVICE_MOUSE && index == 0)
   {
      if (id == RETRO_DEVICE_ID_MOUSE_RIGHT)
         return 1;
      if (id == RETRO_DEVICE_ID_MOUSE_LEFT)
         return 0;
   }

   /* While the menu owns the input the engine must see nothing, or a click on a
    * menu row also lands on whatever the game has under it. This is the single
    * funnel both engines pull from (ONScripter/PONScripter through their event
    * pump, KiriKiri through krkr_set_input_callbacks), so suppressing here
    * covers both without either engine knowing about the menu.
    *
    * Parking the pointer at rest (0,0 - libretro's centre) rather than
    * returning the live position also keeps a stray hover from highlighting a
    * link under the menu. */
   if (menu_owns_input &&
       (device == RETRO_DEVICE_MOUSE || device == RETRO_DEVICE_POINTER ||
        device == RETRO_DEVICE_JOYPAD || device == RETRO_DEVICE_KEYBOARD))
      return 0;

   return pointer_synth_input_state(port, device, index, id);
}

/* Composite the native menu onto a destination frame.
 *
 * The menu's own VRAM carries its pixels and its alpha plane *separately* (see
 * _VRAMHDL in vramhdl.h), so the pixel buffer alone cannot say which pixels the
 * menu owns - untouched VRAM is opaque garbage. The alpha plane is the map:
 * a non-zero byte marks a menu pixel. This mirrors upstream's own compositor
 * (NP2kai sdl/scrnmng.c scrnmng_menudraw), which reads `a[x]` the same way.
 *
 * `dst` is a copy of the engine's frame (the shadow buffer); the engine's own
 * framebuffer is never touched, so its redraw/dirty-rect tracking is unaffected.
 *
 * Both present paths (ONScripter and KiriKiri) need this, hence one function:
 * the menu must behave identically in either engine. */
static void composite_menu(Uint32 *dst, int dst_pitch, int dst_w, int dst_h)
{
   /* The gate belongs HERE, not only at the call sites.
    *
    * The page stays cached while the menu is open, and the menu is opened on
    * the game's `menu.visible` request - which, as of the bar being
    * hotkey-only, no longer means the bar should be composited. Without this
    * test the cached 640x480 NP2kai page was stamped over the top-left of the
    * frame on every frame after the game asked for a menu, which is what put a
    * silver bar reading "Hi" across Fata Morgana's artwork while the reference
    * engine (whose own menu window is unmapped) showed none. */
   if (!vnovel_menu_should_draw())
      return;

   int mw = 0, mh = 0, mpitch = 0, apitch = 0;
   const unsigned char *alpha = NULL;
   const unsigned char *mpix =
         vnovel_menu_draw(&mw, &mh, &mpitch, &alpha, &apitch);
   if (!mpix || !alpha || mw <= 0 || mh <= 0 || mpitch <= 0 || apitch <= 0)
      return;

   int rows = mh < dst_h ? mh : dst_h;
   int cols = mw < dst_w ? mw : dst_w;
   for (int y = 0; y < rows; y++)
   {
      const Uint32 *srow = (const Uint32 *)(mpix + (size_t)y * mpitch);
      const unsigned char *arow = alpha + (size_t)y * apitch;
      Uint32 *drow = dst + (size_t)y * dst_pitch;
      for (int x = 0; x < cols; x++)
      {
         if (!arow[x])
            continue; /* not the menu's pixel: leave the game showing */
         drow[x] = srow[x];
      }
   }
}

/* The live frame size, published by whichever present path ran last. The menu
 * opens at this size so it matches the running game instead of assuming
 * 640x480 (the engine may have chosen another internal resolution). */
static int vnovel_frame_w = 640, vnovel_frame_h = 480;
static void vnovel_publish_frame_size(int w, int h)
{
   if (w > 0 && h > 0 && (w != vnovel_frame_w || h != vnovel_frame_h))
   {
      vnovel_frame_w = w;
      vnovel_frame_h = h;
      vnovel_menu_set_pref_geometry(w, h);
   }
}

/* Whether a click at (x, y) lands in the "open the menu" hotspot, the
 * bottom-right corner of the frame.
 *
 * A second way in besides the hotkey, for a pointer-only reader: a game's own
 * menu is a menubar (resetmenu/insertmenu), and a bar is expected to be reachable
 * without a keyboard. The zone is a fraction of the frame rather than a pixel
 * count, because the frame the engines present varies (640x480 up to 1600x1200).
 *
 * Deliberately small and in the corner, where a visual novel's own clickable
 * regions do not live: the text box and choice buttons are centred, and the
 * corner would otherwise be dead space. */
#define MENU_HOTSPOT_W 64
#define MENU_HOTSPOT_H 32

static int vnovel_menu_hotspot_hit(int x, int y)
{
   int w = MENU_HOTSPOT_W * vnovel_frame_w / 640;
   int h = MENU_HOTSPOT_H * vnovel_frame_h / 480;
   if (w < 16) w = 16;
   if (h < 16) h = 16;
   return x >= vnovel_frame_w - w && y >= vnovel_frame_h - h;
}

/* The pointer, as frame pixels plus a button edge.
 *
 * Split out because the corner hotspot needs the pointer while the menu is
 * CLOSED, and the menu needs it while open: one read serves both.
 *
 * The menu takes the same absolute pointer the engines do, mapped from
 * libretro's [-32768..32767] onto the frame. The button edges matter: NP2kai's
 * convention is 0 = move, 1 = down, 2 = up, and a click outside the menu closes
 * it on the *up* edge. */
static void vnovel_read_pointer(int *out_x, int *out_y, int *out_btn)
{
   int16_t ax = frontend_input_state_cb(
         0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
   int16_t ay = frontend_input_state_cb(
         0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);

   static int last_x = -1, last_y = -1;
   int x = last_x, y = last_y;
   if (ax != 0 || ay != 0)
   {
      x = ((int)ax + 32768) * vnovel_frame_w / 65536;
      y = ((int)ay + 32768) * vnovel_frame_h / 65536;
      if (x < 0) x = 0; else if (x >= vnovel_frame_w) x = vnovel_frame_w - 1;
      if (y < 0) y = 0; else if (y >= vnovel_frame_h) y = vnovel_frame_h - 1;
      last_x = x;
      last_y = y;
   }

   int16_t left = frontend_input_state_cb(
         0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
   if (!left && SDL_libretro_leftclick_button >= 0)
      left = frontend_input_state_cb(
            0, RETRO_DEVICE_JOYPAD, 0, SDL_libretro_leftclick_button);

   /* Edge-detect the button so the menu gets one event per press, not a
    * continuous stream; the position is sent every frame so hover works. */
   static int16_t last_left = 0;
   int btn = 0;
   if (left && !last_left)
      btn = 1;
   else if (!left && last_left)
      btn = 2;
   last_left = left;

   *out_x = x;
   *out_y = y;
   *out_btn = btn;
}

/* Feed the menu the pointer, straight from the frontend - not through
 * core_input_state_cb, which now reports nothing while the menu is open. */
static void vnovel_menu_poll_input(void)
{
   int x, y, btn;
   vnovel_read_pointer(&x, &y, &btn);

   if (vnovel_menu_should_draw())
   {
      if (x >= 0 && y >= 0)
         vnovel_menu_input(x, y, btn);
      return;
   }

   /* Closed: the bottom-right corner opens the GAME's own system menu, drawn by
    * the ENGINE -- not the core's bar. The engine has the game's styling, its
    * menuselectvoice sounds and its modal handling, so asking it is both more
    * authentic and less code than re-drawing that menu here. F12 remains the way
    * to the CORE's bar (the game's resetmenu/insertmenu tree).
    *
    * The request carries nothing and may be ignored: a game that declared no
    * `rmenu` has no engine menu, and then the zone does nothing. Only the press
    * edge, so holding the button in the corner does not keep re-asking. */
   if (btn == 1 && x >= 0 && y >= 0 && vnovel_menu_hotspot_hit(x, y))
   {
      /* Convert the corner click into a RIGHT-click, which is what both engines
       * already treat as "open my own menu" -- with the game's styling, its
       * menuselectvoice sounds and its modal handling. So the hot zone needs no
       * engine change at all.
       *
       * It is done through the input callback rather than by pushing an SDL
       * event, because that is the channel the engines' SDL shim actually reads:
       * its PumpMouseEvents() polls RETRO_DEVICE_ID_MOUSE_RIGHT every pump and
       * calls SDL_PrivateMouseButton() itself, so reporting a press here becomes
       * a real engine mouse event through the engine's own path. (Pushing
       * SDL_PushEvent from this thread was the earlier attempt; it does not reach
       * a queued event the shim owns.)
       *
       * Two polls: one press, then the callback reports nothing and the shim
       * synthesizes the matching release -- a complete click. */
      hotzone_right_frames = 2;
      menu_owns_input = 1;
   }
}

/* Feed the menu the keys it navigates with, as the inputmng.h bitmask.
 *
 * The menu is also the only UI on a device with no mouse, so keyboard and
 * gamepad must walk it: directions move the highlight, Enter activates, and a
 * cancel button is what closes a popup without choosing anything. Read straight
 * from the frontend, like the pointer, because core_input_state_cb deliberately
 * reports nothing to the engines while the menu is open. */
static void vnovel_menu_poll_keys(void)
{
   if (!vnovel_menu_should_draw())
      return;

   /* Keys latched by the keyboard callback since the last frame. The menu treats
    * a key as a one-shot *command* (menusys_key: KEY_ENTER opens, LEFT/RIGHT
    * move the highlight), NOT as held state, so they must be delivered as
    * presses - and a poll cannot see a tap whose press and release both land
    * between frames, which is exactly what a frontend key tap is. */
   unsigned latched = vnovel_menu_take_latched_keys();
   if (latched)
      vnovel_menu_key_mask(latched);

   /* Directions are booleans here and become the menu's own bitmask inside
    * menu.cpp: inputmng.h is a menu-internal header, so its encoding does not
    * leak into the core. */
   /* Gamepad buttons are edge-detected here for the same reason the keyboard is
    * latched: the menu wants a press, and a short tap must not be missed. */
   static int16_t pad_up = 0, pad_down = 0, pad_left = 0, pad_right = 0;
   static int16_t pad_a = 0, pad_b = 0;
   int16_t n_up    = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_UP);
   int16_t n_down  = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_DOWN);
   int16_t n_left  = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_LEFT);
   int16_t n_right = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_RIGHT);
   int16_t n_a     = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_A);
   int16_t n_b     = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                            RETRO_DEVICE_ID_JOYPAD_B);

   unsigned mask = 0;
   if (n_up && !pad_up)       mask |= VNOVEL_MENU_UP;
   if (n_down && !pad_down)   mask |= VNOVEL_MENU_DOWN;
   if (n_left && !pad_left)   mask |= VNOVEL_MENU_LEFT;
   if (n_right && !pad_right) mask |= VNOVEL_MENU_RIGHT;
   if (n_a && !pad_a)         mask |= VNOVEL_MENU_OK;
   if (n_b && !pad_b)         mask |= VNOVEL_MENU_CANCEL;
   pad_up = n_up; pad_down = n_down; pad_left = n_left; pad_right = n_right;
   pad_a = n_a; pad_b = n_b;

   if (mask)
      vnovel_menu_key_mask(mask);
}

/* SDL_libretro_audio_spec is defined in the bridge module: the engine's SDL
 * publishes it, the core reads it. */
SDL_sem *SDL_libretro_event_sem = NULL;
/* Frame sync: when enabled, DUMMY_UpdateRects blocks on this semaphore
   after each screen flush. retro_run() posts it once per frame, giving
   1:1 frame synchronization — every flush() is displayed.
   Controlled by -DLIBRETRO_FRAME_SYNC at build time. */
#ifdef LIBRETRO_FRAME_SYNC
extern SDL_sem *SDL_libretro_wait_sem;
static SDL_sem *fs_sem = NULL;
#endif

/* Core options (set via retro_set_environment, read in retro_run) */
static bool opt_frame_sync      = true;
static bool opt_indicators      = true;
static int opt_debug_level      = 0;
static bool opt_software_cursor = true;
static int opt_text_speed       = -1;
/* Text debug view: 0 = off (present the frame), 1 = split (captured plane on
 * the left, composited frame on the right). A development aid for the
 * text-plane work; it changes nothing about capture or drawing. */
static int opt_text_debug_view = 0;
/* Whether the splice request is currently in effect, so the option reader can
 * tell a change from a steady state (see read_text_debug_view_option). */
static int splice_requested = 0;
/* Split view: present artwork and text side by side at double width (see the
 * `vnovel_split_view` option). Off by default; a demonstration of serving the
 * two surfaces independently at a different resolution. */
static int opt_split_view   = 0;
static int split_requested  = 0;
/* The key that toggles the core's menu bar, as a libretro keycode; 0 = unbound
 * (the default). Set from the `vnovel_menu_hotkey` option before the first
 * frame, and read by the keyboard callback, so the binding cannot drift from
 * what the option says. */
static unsigned menu_hotkey = 0;
/* Fast-forward rate for the single game clock (1.0 = off, N = "Nx"). */
static double opt_ff_scale = 1.0;
/* Rate currently installed in the clock; cached so retro_run only calls
 * ons_libretro_clock_set() when it actually changes. */
static double clock_scale_applied = -1.0;
/* opt_mouse_sensitivity and remap_keys are defined in the bridge module; the
 * core writes them from frontend options, the glue reads them. */
/* Key remap state */
#include "key_remap.h"

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
/* environ_cb_global is defined in the bridge module so the SDL events glue
 * reads core options without a separate plumbing path. The core sets it in
 * retro_set_environment. */
static EngineHandle engine        = nullptr;
static const EngineVTable *vtable = nullptr;

/* Frontend audio-ring occupancy (percent full), last reported by the
 * audio-buffer-status callback. -1 until the first report. It is the only
 * feedback the core gets about the ring it feeds; the KiriKiri submit rate is
 * steered by it. */
#ifdef VNOVEL_KIRIKIRI
static volatile int krkr_ring_occupancy = -1;
/* Frontend's forecast that the ring will run dry during the next frame. The
 * submit controller acts on this and on the occupancy above. */
static volatile int krkr_underrun_likely = 0;
#endif
static uint32_t delta;
static char last_archive_path[PATH_MAX_LENGTH];
static char last_save_path[PATH_MAX_LENGTH];

/* --- The captured text plane, engine-neutral -------------------------------
 *
 * Engines publish their text separately from the frame through the bridge
 * (see VNovelTextPlane in engine_bridge.h); this is where the core keeps the
 * latest copy. Engine-neutral on purpose: ONScripter draws sentences into a
 * surface and KiriKiri composites layers, but both arrive here in the same
 * shape, so the consumer below needs no per-engine branch.
 *
 * `plane_dirty` says a new update has arrived since the consumer last looked,
 * the same handshake the KiriKiri frame uses. The pixels are copied rather than
 * held by pointer because the engine rewrites its surface in place. */
static unsigned char *text_plane_pixels = NULL;
static size_t text_plane_capacity       = 0;
static int text_plane_w = 0, text_plane_h = 0, text_plane_pitch = 0;
static int text_plane_changed_x = 0, text_plane_changed_y = 0;
static int text_plane_changed_w = 0, text_plane_changed_h = 0;
static int text_plane_origin_x = 0, text_plane_origin_y = 0;
static int text_plane_plane_x  = 0, text_plane_plane_y = 0;
static int text_plane_splice   = 0;
static int text_plane_restart  = 0;
static unsigned long long text_plane_serial = 0;

/* --- The accumulated page ---------------------------------------------------
 *
 * Some engines publish a page in one go (ONScripter/PONScripter: the plane IS
 * the screen), and one publishes it a LINE at a time (KiriKiri: each
 * publication is a line-height box, ~616x40 measured). A consumer that wants
 * "the page" therefore cannot just keep the latest publication: on KiriKiri
 * that is only the most recent line.
 *
 * So the core keeps its own page buffer and blits each publication into it at
 * the position the engine reported:
 *
 *     page[plane_y + changed_y + j][plane_x + changed_x + i] = surface[...]
 *
 * which collapses to "the plane is the page" when plane_x/plane_y are 0, i.e.
 * on the two engines that already publish whole pages. One code path, no
 * per-engine branch - the placement is the engine's statement, the core just
 * obeys it.
 *
 * Built only when the sink asks for the placement context
 * (`vnovel_request_text_plane_splice`), so a run that never accumulates pays
 * nothing: no buffer, and the engines skip the per-publication work too. */
static unsigned char *page_pixels = NULL;
static size_t page_capacity       = 0;
static int page_w = 0, page_h = 0, page_pitch = 0;
static volatile int page_dirty = 0;

/* The last frame geometry the core presented, in pixels. This is the page's
 * size: an engine's text surface lives in the same window its frame does, so a
 * publication's window-relative placement is relative to this. Written on the
 * present path (both engines), read by the sink below. 0 until the first
 * frame, in which case the sink falls back to the surface's own size. */
static int text_page_frame_w = 0, text_page_frame_h = 0;

/* Read the accumulated page. Same contract as text_page_get: core-owned
 * buffers, valid until the next update. Returns 0 when nothing is accumulated
 * (splice off, or nothing captured yet). */
static int text_page_get_accumulated(const unsigned char **pixels, int *w,
                                     int *h, int *pitch)
{
   if (!page_pixels || page_w <= 0 || page_h <= 0)
      return 0;
   if (pixels) *pixels = page_pixels;
   if (w)      *w      = page_w;
   if (h)      *h      = page_h;
   if (pitch)  *pitch  = page_pitch;
   return 1;
}

/* Phase 4 of the text-plane plan, in its smallest useful form: present the
 * captured page instead of the game frame.
 *
 * The point of the seam is that text can be shown as a page, restyled or
 * scrolled, rather than being baked into the picture. This mode is what proves
 * the capture is real end to end - the frames it presents are the engine's own
 * text surface, so OCR of them reads the game's dialogue and nothing else.
 *
 * Selected by KRKRSDL2_TEXT_PAGE=1 so a normal run is untouched; a user-facing
 * control (menu entry, hotkey) belongs with the reader that replaces it. */
static int text_page_mode = 0;

/* Read the captured page. Returns 0 when nothing has been captured. The
 * buffers belong to the core; the caller must not free them, and they may be
 * replaced by the next publication, so a caller that keeps them copies. */
static int text_page_get(const unsigned char **pixels, int *w, int *h, int *pitch)
{
   if (!text_plane_pixels || text_plane_w <= 0 || text_plane_h <= 0)
      return 0;
   if (pixels) *pixels = text_plane_pixels;
   if (w)      *w      = text_plane_w;
   if (h)      *h      = text_plane_h;
   if (pitch)  *pitch  = text_plane_pitch;
   return 1;
}
/* Read "the page", whatever that is on the engine currently running.
 *
 * Two cases, and the difference is the engine's, not the caller's:
 *
 *   - the surface IS the page (ONScripter/PONScripter publish a screen-sized
 *     text surface). The latest publication is a complete page, so it is used
 *     directly - there is nothing to accumulate and `splice` is 0.
 *   - the surface is a FRAGMENT (KiriKiri publishes one line box at a time,
 *     with its own local coordinates). Then the accumulated page must exist
 *     before anything is shown, and returning 0 until it does is correct:
 *     drawing the fragment would place the current line at the origin, which is
 *     how the "three lines where the scene has two" artifact appeared.
 *
 * Both consumers (page mode, and the debug view) go through this so the rule
 * cannot drift between them. */
static int text_page_get_current(const unsigned char **pixels, int *w, int *h,
                                 int *pitch)
{
   if (text_page_get_accumulated(pixels, w, h, pitch))
      return 1;
   /* Nothing accumulated yet. Only a whole-surface engine may be shown
    * directly; the splice flag is the engine's own statement that its
    * publications are fragments. */
   if (text_plane_splice)
      return 0;
   return text_page_get(pixels, w, h, pitch);
}

static volatile int text_plane_dirty = 0;


/* The sink the bridges call. Copied here so the consumer runs on the core's
 * thread with stable pixels, for the same reason the engine's surfaces are
 * copied: the engine rewrites its own in place. */
static void vnovel_text_plane_sink(const VNovelTextPlane *plane, void *userdata)
{
   (void)userdata;

   /* pitch keeps its sign: the engine's bitmaps are bottom-up, so a negative
    * stride is the row direction. Rejecting it discarded every publication. */
   if (!plane || !plane->pixels || plane->width <= 0 || plane->height <= 0 ||
       plane->pitch == 0)
      return;

   /* Bound the geometry before trusting it: the values cross a thread
    * boundary, and a bad pitch would turn the copy below into a heap
    * overrun. The same guard the frame path uses. */
   {
      const int ap = plane->pitch < 0 ? -plane->pitch : plane->pitch;
      if (plane->width > 8192 || plane->height > 8192 ||
          ap < plane->width * 4 || ap > plane->width * 8)
         return;
   }

   /* The stride's sign is direction, not size. */
   const int      stride   = plane->pitch; /* signed: bottom-up rows */
   const size_t   rowbytes = (size_t)plane->width * 4;
   const size_t   needed   = rowbytes * (size_t)plane->height;

   /* Copy ROW BY ROW using the stride, never as one block.
    *
    * The engine's bitmaps are bottom-up, and the pointer it hands over is
    * `GetScanLineForWrite(0)`, which is `(Height-1)*Pitch + Bits` - that is,
    * near the *end* of the allocation, with the following rows at lower
    * addresses. A single `memcpy(dst, pixels, |pitch| * height)` therefore
    * reads forward off the end of the buffer: for the 808x48 plane that is
    * 155 KB starting at a pointer 152 KB into a 152 KB allocation. Small planes
    * survived because the pages past the end happened to be mapped; Fate's
    * prologue draws into a larger bitmap and it faulted in libc's memcpy
    * (confirmed from the core dump: sink <- krkr_text_plane_cb <-
    * TVPPresentTextPlane <- InternalDrawText). Walking the stride is what the
    * plane's own contract requires, and the destination is packed. */
   if (needed > text_plane_capacity)
   {
      unsigned char *np = (unsigned char *)realloc(text_plane_pixels, needed);
      if (!np)
         return; /* keep the previous plane rather than publish a torn one */
      text_plane_pixels   = np;
      text_plane_capacity = needed;
   }
   {
      const unsigned char *src = (const unsigned char *)plane->pixels;
      for (int y = 0; y < plane->height; y++)
         memcpy(text_plane_pixels + (size_t)y * rowbytes,
                src + (ptrdiff_t)y * stride, rowbytes);
   }
   text_plane_w         = plane->width;
   text_plane_h         = plane->height;
   text_plane_pitch     = (int)rowbytes; /* the copy is packed */
   text_plane_changed_x = plane->changed_x;
   text_plane_changed_y = plane->changed_y;
   text_plane_changed_w = plane->changed_w;
   text_plane_changed_h = plane->changed_h;
   text_plane_origin_x  = plane->origin_x;
   text_plane_origin_y  = plane->origin_y;
   text_plane_plane_x   = plane->plane_x;
   text_plane_plane_y   = plane->plane_y;
   text_plane_splice    = plane->splice;
   text_plane_restart   = plane->restart;
   text_plane_serial    = plane->serial;
   text_plane_dirty     = 1;

   /* Accumulate the page: blit this publication's changed rect into the page
    * buffer at the position the engine gave (see the page buffer above). Runs
    * only when the engine is maintaining the placement context, and only for
    * the region that actually moved. */
   if (plane->splice)
   {
      /* The page is the WINDOW, and the window's size comes from the frame the
       * core presented - never from the publication itself.
       *
       * This was the bug that lost the start of every page: on the first
       * publication the frame size was not yet known, so the fallback below
       * used the FRAGMENT's width (616 for a KiriKiri line box against the
       * window's 640), the page was created at 616, and the moment the real
       * frame size appeared the size "changed", which cleared the page and
       * threw away everything accumulated so far. Measured as "text before CDE
       * is lost" - the page always started over once the frame arrived.
       *
       * So: if the window size is not known yet, DO NOT ACCUMULATE. A
       * publication that arrives before the core has presented a frame cannot
       * be placed anyway (it has no window coordinate), and dropping it is
       * honest - the alternative is inventing a size and then discarding the
       * result. The cost is the few publications of the opening burst, once,
       * and on the engines that publish whole pages the page is the surface
       * itself (splice off) so nothing is lost there. */
      /* The page is the WINDOW. The size comes from the publication itself when
       * the engine reports it, because the engine publishes text during
       * start-up - before the core's present path has ever run - and sizing the
       * page from the core thread alone dropped that whole opening burst (the
       * start of the first page was missing on every run). The core-learned
       * size stays as the fallback for a publication that carries none. */
      if (plane->window_w > 0 && plane->window_h > 0)
      {
         text_page_frame_w = plane->window_w;
         text_page_frame_h = plane->window_h;
      }
      if (text_page_frame_w <= 0 || text_page_frame_h <= 0)
         return;

      const int pw = text_page_frame_w;
      const int ph = text_page_frame_h;
      const int prowbytes = pw * 4;
      const size_t pneeded = (size_t)prowbytes * (size_t)ph;
      if (pneeded > 0 && pneeded <= (size_t)64 * 1024 * 1024)
      {
         if (pneeded > page_capacity)
         {
            unsigned char *np = (unsigned char *)realloc(page_pixels, pneeded);
            if (np)
            {
               page_pixels   = np;
               page_capacity = pneeded;
               page_w = pw;
               page_h = ph;
               page_pitch = prowbytes;
               /* A fresh allocation has no valid contents. */
               memset(page_pixels, 0, pneeded);
            }
         }
         else if ((page_w != pw || page_h != ph) && page_pixels)
         {
            /* The window itself changed size. Old coordinates are meaningless,
             * so the page starts over - and this is the only case that clears
             * it besides `restart`. */
            page_w = pw;
            page_h = ph;
            page_pitch = prowbytes;
            memset(page_pixels, 0, (size_t)page_pitch * (size_t)page_h);
         }

         if (page_pixels && page_w == pw && page_h == ph)
         {
            /* A new page clears everything on it. */
            if (plane->restart)
               memset(page_pixels, 0, (size_t)page_pitch * (size_t)page_h);

            /* Clamp the source rect the engine reported to the surface it came
             * from, and the destination to the page: neither side's numbers are
             * trusted beyond their own bounds. */
            int cx = plane->changed_x, cy = plane->changed_y;
            int cw = plane->changed_w, ch = plane->changed_h;
            if (cw <= 0 || ch <= 0)
            {
               /* No pixel change reported (geometry or restart only): with a
                * restart already handled above there is nothing to blit. */
            }
            else
            {
               if (cx < 0) { cw += cx; cx = 0; }
               if (cy < 0) { ch += cy; cy = 0; }
               if (cx + cw > plane->width)  cw = plane->width - cx;
               if (cy + ch > plane->height) ch = plane->height - cy;

               int dx = plane->plane_x + cx, dy = plane->plane_y + cy;
               if (dx < 0) { cw += dx; cx -= dx; dx = 0; }
               if (dy < 0) { ch += dy; cy -= dy; dy = 0; }
               if (dx + cw > page_w) cw = page_w - dx;
               if (dy + ch > page_h) ch = page_h - dy;

               for (int j = 0; j < ch; j++)
               {
                  const unsigned char *src =
                        text_plane_pixels +
                        (size_t)(cy + j) * (size_t)text_plane_pitch +
                        (size_t)cx * 4u;
                  unsigned char *dst = page_pixels +
                                       (size_t)(dy + j) * (size_t)page_pitch +
                                       (size_t)dx * 4u;
                  memcpy(dst, src, (size_t)cw * 4u);
               }
            }
            page_dirty = 1;
         }
      }
   }
}

/* --- Text runs: the core side of the structured stream ---------------------
 *
 * The engine hands over each draw call's characters, position and style (see
 * VNovelTextRun). At this stage the sink only OBSERVES: it writes the stream to
 * a file so the shape of what the engine actually reports can be checked before
 * anything renders from it.
 *
 * Why a log and not a data structure yet: the plan's premise is that a greedy
 * parse can hand the core the page's text as fast as the engine can produce it,
 * and that this is faster and more faithful than accumulating pixels. That
 * premise is only worth building on if the stream is actually complete and
 * correctly placed, and the way to know is to read it.
 *
 * The file is fixed and in tmpfs, for the same reason the audio diagnostic is:
 * the launcher buffers the core's stderr and only flushes it when the game
 * exits, so anything written there is invisible while a run is alive.
 *
 * The SAME records go to the development socket (devsock.h) when one is open,
 * which is the live form of this log: same shape, same decoder, no launcher
 * buffering. That is why the decode below is shared rather than duplicated -
 * two producers of these lines would drift. */
static FILE *text_run_log = NULL;

/* Decode the run's UTF-16 into `utf8`, which must have room for
 * text_length * 4 + 1 bytes. Returns the byte count.
 *
 * The buffer is sized from the run rather than being a fixed 256 bytes: a
 * page-level run is a WHOLE PAGE, and a fixed small buffer silently dropped
 * the tail of every page longer than ~255 characters. That is exactly what
 * made the Disclaimer's choice labels ("Yes," / "No,") appear to be missing
 * from the look-ahead when the walk had in fact collected them.
 *
 * Written as UTF-8 so the result is readable and greppable; surrogates are
 * passed through, which is correct for anything that later decodes the text as
 * UTF-8. */
static size_t text_run_utf8(const VNovelTextRun *run, char *utf8)
{
   size_t n = 0;
   for (int i = 0; i < run->text_length; i++)
   {
      unsigned cp = run->text[i];
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < run->text_length)
      {
         const unsigned lo = run->text[i + 1];
         if (lo >= 0xDC00 && lo <= 0xDFFF)
         {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            i++;
         }
      }
      if (cp < 0x80)
         utf8[n++] = (char)cp;
      else if (cp < 0x800)
      {
         utf8[n++] = (char)(0xC0 | (cp >> 6));
         utf8[n++] = (char)(0x80 | (cp & 0x3F));
      }
      else if (cp < 0x10000)
      {
         utf8[n++] = (char)(0xE0 | (cp >> 12));
         utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
         utf8[n++] = (char)(0x80 | (cp & 0x3F));
      }
      else
      {
         utf8[n++] = (char)(0xF0 | (cp >> 18));
         utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
         utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
         utf8[n++] = (char)(0x80 | (cp & 0x3F));
      }
   }
   utf8[n] = 0;
   return n;
}

static void vnovel_text_run_sink(const VNovelTextRun *run, void *userdata)
{
   (void)userdata;
   if (!run || !run->text || run->text_length <= 0)
      return;

   /* The socket, or the log: either one means the record is wanted. Listening
    * rather than connected, because records published before a client attaches
    * are buffered and released to it - the walk's opening pages would otherwise
    * be lost to a consumer started a moment too late. */
   const int socked = vnovel_dev_socket_listening();
   if (!text_run_log && !socked)
   {
      const char *p = getenv("VNOVEL_TEXT_RUN_LOG");
      if (!p)
         return; /* not asked for: observe nothing */
      text_run_log = fopen(p, "w");
      if (!text_run_log && !socked)
         return;
   }

   const size_t cap = (size_t)run->text_length * 4u + 1u;
   char *utf8 = (char *)malloc(cap);
   if (!utf8)
      return;
   const size_t n = text_run_utf8(run, utf8);

   /* One line per record, in the log's own vocabulary (see the header comment).
    * A page from the look-ahead is the unit a renderer wants, so it is one
    * marked, unquoted line; newlines inside it are escaped so a page stays one
    * line and `grep '^PAGE'` reads them back in order. A drawn run keeps its
    * serial, position and style, which a consumer needs to tell the two
    * producers apart and to correlate with the plane. */
   char line[8192];
   int len;
   if (run->kind == VNOVEL_RUN_PARSE)
   {
      /* The page's ORDINAL, not its byte count: numbering is structure (see
       * devsock.h), and it is what lets an event or a choice name the page it
       * belongs to. The count was redundant besides -- the text follows on this
       * same line, so its length is a strlen. */
      static unsigned int page_ordinal = 0;
      len = snprintf(line, sizeof(line), VNOVEL_REC_PAGE "%u) ",
                     ++page_ordinal);
      for (size_t k = 0; k < n && len < (int)sizeof(line) - 1; k++)
      {
         if (utf8[k] == '\n')
         {
            if (len < (int)sizeof(line) - 3)
               line[len++] = '\\', line[len++] = 'n';
         }
         else if (utf8[k] == '\r')
         {
            if (len < (int)sizeof(line) - 3)
               line[len++] = '\\', line[len++] = 'r';
         }
         else
            line[len++] = utf8[k];
      }
      line[len] = 0;
   }
   else
   {
      len = snprintf(line, sizeof(line),
                     "run %llu draw local=(%d,%d) text=%s color=%06X "
                     "opacity=%d aa=%d shadow=%d/%06X%+d%+d",
                     run->serial, run->x, run->y, utf8,
                     run->color & 0xFFFFFFu, run->opacity, run->antialias,
                     run->shadow_level, run->shadow_color & 0xFFFFFFu,
                     run->shadow_offset_x, run->shadow_offset_y);
      if (len < 0)
         len = 0;
      if (len > (int)sizeof(line) - 1)
         len = (int)sizeof(line) - 1;
   }

   if (text_run_log)
   {
      fputs(line, text_run_log);
      fputc('\n', text_run_log);
      fflush(text_run_log);
   }
   if (socked)
      vnovel_dev_socket_send(line, (size_t)len);

   free(utf8);
}

/* --- The development socket: navigation in --------------------------------
 *
 * The outbound half needs no code of its own: the text-run sink above writes its
 * records to the socket when one is connected (see devsock.h). This is the
 * inbound half - the consumer's navigation intents - and the lifecycle.
 *
 * The intent vocabulary is one line per intent:
 *
 *   continue     advance past a text pause, exactly as pressing the advance key
 *                does: the engine's next clickwait/textwait resolves and the
 *                script moves on. Also accepted as `c` and `next`.
 *   select N     take the Nth choice the current page offers, exactly as a
 *                click on that label would. 1-based, in page order.
 *
 * Why `continue` has no engine-side code at all: on every engine the advance IS
 * a key press (ONScripter's keyPressEvent sets the same button state a click
 * does; KAG advances on VK_SPACE/VK_RETURN), and the input path that turns a
 * key press into that state already exists and is already correct - engines'
 * own SDL queues, the frontend's input callback, the pad mappings. So this
 * pushes SPACE through the engine's existing key entry point rather than
 * inventing a second advance path that could drift from the real one.
 *
 * Why the ORDINAL and not a link target: an ordinal is what a page actually has
 * (the labels arrive in order), it is what a click produces on every engine
 * (ONScripter's `ButtonLink::no`, KiriKiri's index into the layer's `links`),
 * and it needs no payload grammar - `<link:...>` carries an engine-specific
 * expression whose evaluation the engine, not this socket, must own. The engine
 * is also the only side that can say whether the choice EXISTS, which is why the
 * verdict comes back rather than being assumed: see vnovel_publish_nav_result.
 *
 * Nothing here goes through the input layers: the ordinal reaches the engine
 * through its vtable (pushSelect), which mirrors the state change a click makes.
 * No synthetic mouse event, no coordinate, no hover state.
 *
 * Deliberately not a protocol with a future: unknown lines are logged and
 * ignored, and there is no ack, no sequence number and no framing beyond '\n'. */

/* Which engine takes a selection, and how. One place, because the two engine
 * families are driven differently: ONScripter/PONScripter hand it to the vtable
 * slot, while KiriKiri has no vtable at all (it is driven by direct krkr_* calls
 * and owns its own thread) and queues it for its own loop to apply. */
#ifdef VNOVEL_KIRIKIRI
/* Defined in engine_kirikiri_cabi.cpp, which is the other side of the same
 * hand-off krkr_push_key is: this file's socket poll runs on the frontend's
 * thread, so it hands the ordinal over rather than touching the message layer. */
extern "C" void krkr_push_select(int ordinal);
/* The key entry point, declared here too: the socket's `continue` intent uses
 * the same advance key the frontend does, and it is defined in the same file. */
extern "C" void krkr_push_key(int sym, int down);
#endif
static int vnovel_dispatch_select(int ordinal)
{
   if (!engine)
      return 0;
#ifdef VNOVEL_KIRIKIRI
   if (!vtable)
   {
      krkr_push_select(ordinal);
      return 1;
   }
#endif
   if (vtable->pushSelect)
   {
      vtable->pushSelect(engine, ordinal);
      return 1;
   }
   return 0;
}

/* One line from the consumer. Runs on the frontend's thread (retro_run), which
 * is NOT the engine thread on KiriKiri - hence pushSelect queues rather than
 * applies. */
static void vnovel_dev_line(const char *line, void *userdata)
{
   (void)userdata;
   if (!line || !*line)
      return;

   /* A leading command word, then its argument. Whitespace-tolerant because a
    * human is expected to be typing this into a terminal. */
   const char *p = line;
   while (*p == ' ' || *p == '\t')
      p++;

   if (strncmp(p, "select", 6) == 0 &&
       (p[6] == ' ' || p[6] == '\t' || p[6] == 0))
   {
      const char *arg = p + 6;
      while (*arg == ' ' || *arg == '\t')
         arg++;
      const int n = atoi(arg);
      if (n <= 0)
      {
         fprintf(stderr, "devsocket: 'select' needs a 1-based ordinal\n");
         return;
      }
      if (!vnovel_dispatch_select(n))
      {
         fprintf(stderr, "devsocket: select %d: this engine cannot take a "
                         "selection\n", n);
         return;
      }
      return;
   }

   /* `continue` / `c` (and `next`, the same key's other name in the engines).
    * SPACE is the advance key on all three - ONScripter's keyPressEvent treats
    * it as a left-click on the button under the cursor, KAG's own input maps it
    * to onPrimaryClick - so the existing key entry point is the advance. Press
    * and release together, the same way a tap arrives from a frontend. */
   if (strcmp(p, "continue") == 0 || strcmp(p, "c") == 0 ||
       strcmp(p, "next") == 0)
   {
#ifdef VNOVEL_KIRIKIRI
      if (!vtable)
      {
         krkr_push_key(SDLK_SPACE, 1);
         krkr_push_key(SDLK_SPACE, 0);
         return;
      }
#endif
      if (vtable && vtable->pushKey)
      {
         vtable->pushKey(engine, SDLK_SPACE, 1);
         vtable->pushKey(engine, SDLK_SPACE, 0);
         return;
      }
      fprintf(stderr, "devsocket: continue: this engine cannot take a key\n");
      return;
   }

   fprintf(stderr, "devsocket: unknown intent: %s\n", line);
}

/* The engines report the verdict through the bridge (engine_bridge.h) on their
 * own thread. Logged, not answered on the socket: the client sees the outcome
 * as the page that follows (or does not), which keeps this direction one-way
 * and the transport dumb. */
static void vnovel_nav_result(int ordinal, int accepted)
{
   fprintf(stderr, "devsocket: select %d -> %s\n",
           ordinal, accepted ? "accepted" : "no such choice");
}

/* --- Page flow out: the engine's own "paused" / "running" edge -------------
 *
 * The text-run stream is produced by a look-ahead walk, so pages arrive while
 * the engine is still several pages behind. This is the engine's own signal of
 * where it actually is: a text pause is armed (the reader must click) or the
 * reader acted and it is running again. A consumer pairs the Nth page it queued
 * with the Nth pause it sees, which is what stops a transcript running ahead of
 * the game. See engine_bridge.h for the contract.
 *
 * Published to the socket (and the run log, when one is open) as two bare words.
 * Deliberately no page number: the page's identity is its order, which both
 * sides already have, and a number here would be a second source of truth for
 * something the ordering already decides. */
static void vnovel_page_event(int event)
{
   /* The event's token, from the ONE list (devsock.h). A boundary and a wait are
    * different events and reach the client as different tokens, so a consumer
    * never has to infer which it is from context. */
   const char *line = VNOVEL_REC_PAGE_START;
   switch (event)
   {
      case VNOVEL_EV_PAGE_END:   line = VNOVEL_REC_PAGE_END;   break;
      case VNOVEL_EV_LINE_PAUSE: line = VNOVEL_REC_LINE_PAUSE; break;
      case VNOVEL_EV_PAGE_PAUSE: line = VNOVEL_REC_PAGE_PAUSE; break;
      case VNOVEL_EV_RESUME:     line = VNOVEL_REC_RESUME;     break;
      default:                   line = VNOVEL_REC_PAGE_START; break;
   }
   if (text_run_log)
   {
      fputs(line, text_run_log);
      fputc('\n', text_run_log);
      fflush(text_run_log);
   }
   if (vnovel_dev_socket_listening())
      vnovel_dev_socket_send(line, strlen(line));
}

/* One choice's label, for a menu the page text cannot describe. */
static void vnovel_choice_label(int ordinal, const char *label)
{
   char line[1024];
   const int n = snprintf(line, sizeof(line), VNOVEL_REC_CHOICE "%d) %s",
                          ordinal, label ? label : "");
   if (n <= 0 || n >= (int)sizeof(line))
      return;
   if (text_run_log)
   {
      fputs(line, text_run_log);
      fputc('\n', text_run_log);
      fflush(text_run_log);
   }
   if (vnovel_dev_socket_listening())
      vnovel_dev_socket_send(line, (size_t)n);
}

/* --- Step 0: a regression signal for the captured plane ---------------------
 *
 * The suite asserted nothing about the plane, so it passed with capture
 * entirely broken - which is how the pitch-sign rejection and the memcpy
 * overrun shipped. `snapshotsave` dumps the engine's own frame, not the core's
 * copy of the plane; this writes the copy, so a test can compare the pixels the
 * seam actually produced.
 *
 * Bottom-up 32bpp BMP, matching what the engines' own SDL_SaveBMP writes, so a
 * golden from here sits beside the frame goldens with no special handling. The
 * plane's pixels are BGRA in a line's byte order; BMP wants BGRA too, so the
 * rows copy with no channel swap. */
static void write_bmp32(const char *path, const unsigned char *pixels,
                        int w, int h, int pitch)
{
   FILE *f = fopen(path, "wb");
   if (!f)
      return;
   const int rowbytes = w * 4;
   const int imagesz  = rowbytes * h;
   const int filesz   = 54 + imagesz;
   unsigned char hdr[54] = {0};
   hdr[0] = 'B'; hdr[1] = 'M';
   hdr[2] = (unsigned char)(filesz);
   hdr[3] = (unsigned char)(filesz >> 8);
   hdr[4] = (unsigned char)(filesz >> 16);
   hdr[5] = (unsigned char)(filesz >> 24);
   hdr[10] = 54;            /* pixel data offset */
   hdr[14] = 40;            /* BITMAPINFOHEADER size */
   hdr[18] = (unsigned char)(w);
   hdr[19] = (unsigned char)(w >> 8);
   hdr[20] = (unsigned char)(w >> 16);
   hdr[21] = (unsigned char)(w >> 24);
   hdr[22] = (unsigned char)(h);
   hdr[23] = (unsigned char)(h >> 8);
   hdr[24] = (unsigned char)(h >> 16);
   hdr[25] = (unsigned char)(h >> 24); /* positive: bottom-up */
   hdr[26] = 1;                        /* planes */
   hdr[28] = 32;                       /* bpp */
   hdr[34] = (unsigned char)(imagesz);
   hdr[35] = (unsigned char)(imagesz >> 8);
   hdr[36] = (unsigned char)(imagesz >> 16);
   hdr[37] = (unsigned char)(imagesz >> 24);
   fwrite(hdr, 1, sizeof(hdr), f);
   /* Bottom-up: last source row first. */
   for (int y = h - 1; y >= 0; y--)
      fwrite(pixels + (size_t)y * (size_t)pitch, 1, (size_t)rowbytes, f);
   fclose(f);
}

/* Dump the core's captured plane to <last_save_path><name>.bmp.
 *
 * `name` comes from the script (the plane equivalent of `snapshotsave`), so a
 * test names the artifact and the harness's existing reference comparison picks
 * it up with no new machinery. No-op when nothing has been captured, so a
 * script command on a frame that drew no text does not write a stale plane. */
static int text_plane_dump(const char *name)
{
   if (!name || !*name || !text_plane_pixels || text_plane_w <= 0 ||
       text_plane_h <= 0)
      return 0;
   char path[PATH_MAX_LENGTH];
   /* last_save_path is canonical and has NO trailing separator (unlike the
    * engines' own script_h.save_path), so a plain "%s%s.bmp" concatenation
    * writes to a sibling of the save directory. Join properly instead. */
   char file[256];
   snprintf(file, sizeof(file), "%s.bmp", name);
   fill_pathname_join(path, last_save_path, file, sizeof(path));
   write_bmp32(path, text_plane_pixels, text_plane_w, text_plane_h,
               text_plane_pitch);
   return 1;
}

/* Bridge-facing wrapper: the engines request a dump through a function
 * pointer, so the core's own helper has to match that shape. */
static void vnovel_text_plane_dump_cb_impl(const char *name, void *userdata)
{
   (void)userdata;
   text_plane_dump(name);
}

/* --- Text debug view (core option `vnovel_text_debug_view`) -----------------
 *
 * A development aid: show the text the core has captured, so it can be judged
 * against the frame it was peeled out of.
 *
 * Drawn as a translucent picture-in-picture in the bottom-right of the frame,
 * showing ONE thing: the accumulated text page. There is deliberately no
 * frame panel beside it - the frame is already on screen behind the PiP, so a
 * second copy would say nothing and halve the space the text gets. The page is
 * aspect-preserving (panel_w:panel_h == w:h) and scaled to a quarter, so what
 * the panel shows is what was captured, never a distortion of it.
 *
 * The backing is flat dark rather than the frame: the page is transparent where
 * nothing was drawn, and letting artwork show through there would make "not
 * captured" look like captured content - the one confusion this tool must not
 * create. Blended at partial alpha so it reads as an overlay.
 *
 * `dst` is the core's own shadow buffer (never the engine's framebuffer), and
 * `pitch` is in pixels. Runs last, so nothing the core draws can overpaint it.
 */
static void text_debug_view_apply(Uint32 *dst, int pitch, int w, int h)
{
   if (!opt_text_debug_view || !dst || w < 8 || h < 8)
      return;

   const unsigned char *tp = NULL;
   int tw = 0, th = 0, tpitch = 0;
   /* Through the one shared rule (see text_page_get_current): the accumulator
    * when there is one, else the publication only if the engine publishes whole
    * pages. Never a fragment drawn as if it were the page. */
   const int have = text_page_get_current(&tp, &tw, &th, &tpitch) && tp &&
                    tw > 0 && th > 0 && tpitch > 0;

   /* Aspect-preserving quarter scale: w/4 : h/4 == w : h. */
   const int pip_w = w / 4;
   const int pip_h = h / 4;
   if (pip_w < 8 || pip_h < 8)
      return;
   const int margin = 6;
   const int x0     = w - pip_w - margin;
   const int y0     = h - pip_h - margin;
   if (x0 < 0 || y0 < 0)
      return;

   /* Mostly opaque, but the frame underneath still reads through, so the PiP
    * can be judged as an overlay without hiding what it overlays. */
   const unsigned PIP_A = 208; /* of 255 */

   for (int py = 0; py < pip_h; py++)
   {
      Uint32 *drow = dst + (size_t)(y0 + py) * pitch;
      const unsigned char *prow =
            have ? tp + (size_t)((int)((long long)py * th / pip_h)) *
                             (size_t)tpitch
                 : NULL;

      for (int px = 0; px < pip_w; px++)
      {
         /* The captured text, over a flat dark backing (see above). */
         Uint32 sr = 0x40, sg = 0x40, sb = 0x40;
         if (prow)
         {
            Uint32 v;
            const int sx = (int)((long long)px * tw / pip_w);
            memcpy(&v, prow + (size_t)sx * 4u, 4);
            const Uint32 a = (v >> 24) & 0xff;
            sr = (((v >> 16) & 0xff) * a + sr * (255 - a)) / 255;
            sg = (((v >> 8) & 0xff) * a + sg * (255 - a)) / 255;
            sb = ((v & 0xff) * a + sb * (255 - a)) / 255;
         }

         /* Border, so the panel is unmistakably the tool's own overlay. */
         if (py == 0 || py == pip_h - 1 || px == 0 || px == pip_w - 1)
         {
            sr = 0x00; sg = 0xD0; sb = 0xFF;
         }

         Uint32 d;
         memcpy(&d, drow + (x0 + px), 4);
         const Uint32 r = (sr * PIP_A + ((d >> 16) & 0xff) * (255 - PIP_A)) / 255;
         const Uint32 g = (sg * PIP_A + ((d >> 8) & 0xff) * (255 - PIP_A)) / 255;
         const Uint32 b = (sb * PIP_A + (d & 0xff) * (255 - PIP_A)) / 255;
         drow[x0 + px] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
   }
}

/* --- Split view (`vnovel_split_view`) ---------------------------------------
 *
 * Present the two surfaces side by side, at the game's own size each: artwork
 * on the left, text on the right. A 640x480 game therefore presents as 1280x480.
 *
 * This is a demonstration, not a way to play: its point is that the frame and
 * the text are now genuinely separate - the engine composes artwork only (text
 * layers skipped, see TVPSetSplitViewArtworkOnly), and the text arrives on the
 * plane seam - so the core is free to lay them out however it likes, including
 * at a resolution the game never had.
 *
 * `artwork` is the composed frame (already artwork-only); `pitch` is in pixels.
 * Returns the composed buffer, or NULL if it could not be built (the caller
 * then presents normally rather than showing a broken layout).
 *
 * The result is a single buffer presented as one wide image. RetroArch scales
 * it like any other output, so the split is visible on any display without the
 * frontend needing to know about it. */
static Uint32 *split_view_compose(const Uint32 *artwork, int pitch, int w, int h)
{
   static Uint32 *canvas = NULL;
   static int canvas_px  = 0;
   const int out_w       = w * 2;
   const int out_h       = h;
   const int need        = out_w * out_h;
   if (need <= 0)
      return NULL;
   if (need > canvas_px)
   {
      Uint32 *np = (Uint32 *)realloc(canvas, (size_t)need * sizeof(Uint32));
      if (!np)
         return NULL;
      canvas    = np;
      canvas_px = need;
   }

   /* Left half: the artwork frame, copied row by row (the source pitch is in
    * pixels and may exceed w). */
   for (int y = 0; y < h; y++)
   {
      const Uint32 *src = artwork + (size_t)y * pitch;
      Uint32 *dst       = canvas + (size_t)y * out_w;
      memcpy(dst, src, (size_t)w * sizeof(Uint32));
   }

   /* Right half: the text surface, and nothing else.
    *
    * Alpha ZERO, not opaque black: this half is only the text, and what shows
    * through it is the reader's background, not a colour the core invents. An
    * opaque fill here would make the text side a black rectangle for every
    * pixel the page does not cover, which is exactly what the earlier
    * diagnostic (opaque red) was.
    *
    * The page is BGRA; it is copied through with its alpha intact, so a pixel
    * with no glyph stays transparent and the text side carries only the text. */
   for (int y = 0; y < h; y++)
   {
      Uint32 *row = canvas + (size_t)y * out_w + w;
      for (int x = 0; x < w; x++)
         row[x] = 0u;
   }
   {
      const unsigned char *tp = NULL;
      int tw = 0, th = 0, tpitch = 0;
      if (text_page_get_current(&tp, &tw, &th, &tpitch) && tp && tw > 0 &&
          th > 0 && tpitch > 0)
      {
         const int rows = th < h ? th : h;
         const int cols = tw < w ? tw : w;
         for (int y = 0; y < rows; y++)
         {
            const unsigned char *src = tp + (size_t)y * (size_t)tpitch;
            Uint32 *dst              = canvas + (size_t)y * out_w + w;
            /* BGRA in, ARGB out. The page's alpha is carried, not flattened. */
            for (int x = 0; x < cols; x++)
            {
               const Uint32 b = src[x * 4 + 0];
               const Uint32 g = src[x * 4 + 1];
               const Uint32 r = src[x * 4 + 2];
               const Uint32 a = src[x * 4 + 3];
               dst[x]         = (a << 24) | (r << 16) | (g << 8) | b;
            }
         }
      }
   }
   return canvas;
}

/* Dedicated audio diagnostic log. The launcher pipes the core's stderr into a
 * buffer that is only flushed to es_launch_stderr.log when the game *exits*,
 * so live diagnostics are invisible during a session. Write to a fixed file
 * instead (tmpfs -> always writable) so tests can read it live:
 * /tmp/ons_audio.log */
static FILE *audio_log = NULL;
static void audio_logf(const char *fmt, ...)
{
   if (!audio_log)
   {
      audio_log = fopen("/tmp/ons_audio.log", "w");
      if (!audio_log)
         return;
   }
   va_list ap;
   va_start(ap, fmt);
   vfprintf(audio_log, fmt, ap);
   va_end(ap);
   fflush(audio_log); /* live-readable */
}

/* Recursive mkdir (all components). Needed because per-game save dirs are
 * nested: <savedir>/<game>/ and ons_mkdir() is a plain single-level mkdir.
 * Both separators are honoured: a Windows path from the frontend uses '\' and
 * splitting on '/' alone would pass the whole path to a single-level mkdir. */
static void mkdir_p(const char *path)
{
   char tmp[PATH_MAX_LENGTH];
   size_t len = strlen(path);
   if (len == 0 || len >= sizeof(tmp))
      return;
   memcpy(tmp, path, len + 1);
   for (char *p = tmp + 1; *p; p++)
   {
      if (*p == '/' || *p == '\\')
      {
         char sep = *p;
         *p       = '\0';
         ons_mkdir(tmp);
         *p = sep;
      }
   }
   ons_mkdir(tmp);
}

static void show_message(const char *msg)
{
   struct retro_message message = {.msg = msg, .frames = 120};
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

/* RetroPad -> semantic action. This is the contract the user sees in
 * RetroArch's Controls screen and it must read the same for every engine; the
 * per-engine key/mouse translation lives in sdl-libretro/events.c (ONScripter)
 * and engine_kirikiri_cabi.cpp (KiriKiri). See CONTROLS.md. Users remap at the
 * RetroArch Controls level, which maps physical buttons to these RetroPad ids;
 * the core always reads the ids the same way. */
static struct retro_input_descriptor input_descs[] = {
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Focus Previous"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Focus Next"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_LEFT,
       "Focus Previous"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Focus Next"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_A,
       "Advance / Confirm"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cancel / Back"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Hide Text Window"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Save"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_SELECT,
       "Skip (toggle)"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_START,
       "Auto Mode (toggle)"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "History Back"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "History Forward"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "History Back"},
      {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "History Forward"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_L3,
       "Pointer Click (Left)"},
      {0,
       RETRO_DEVICE_JOYPAD,
       0,
       RETRO_DEVICE_ID_JOYPAD_R3,
       "Pointer Click (Right)"},
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X, "Mouse X"},
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y, "Mouse Y"},
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT, "Click"},
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT, "Menu"},
      {0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X, "Touch X"},
      {0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y, "Touch Y"},
      {0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED, "Touch"},
      {0, 0, 0, 0, NULL},
};

#ifdef VNOVEL_KIRIKIRI
static bool kiri_input_wired =
      false; /* reset clears this so a new engine re-wires */
static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t);
#endif

/* Keys the *core* acts on, as opposed to keys the game should see. Edge-driven
 * so a tap cannot be missed between frames, which is why this exists rather than
 * a poll in retro_run.
 *
 * Called from whichever keyboard callback the frontend was given: this function
 * directly for ONScripter/PONScripter, and from krkr_keyboard_cb for the
 * KiriKiri build, because RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK can only be
 * set once per session. */
static bool vnovel_core_keyboard_handle(bool down, unsigned keycode)
{
   /* The menu hotkey, from the core option. 0 means unbound, which is the
    * default: the bar is an addition to the frame rather than something a game
    * draws, so a title that never asks for one must not have a stray key open
    * one. Compared against what the option resolved to rather than against a
    * hardcoded name, so "which key opens the menu" has one answer. */
   if (menu_hotkey && keycode == menu_hotkey)
   {
      if (down)
         vnovel_menu_toggle();
      return true; /* consumed: never forwarded to the game */
   }

   /* While the menu is up it owns the keyboard, so navigation keys are latched
    * here and never reach the game - the same rule the pointer follows.
    * Latching rather than polling matters: this callback is edge-driven, so a
    * tap cannot be missed, whereas a per-frame poll can.
    *
    * Non-navigation keys still fall through (return false) so they are not
    * silently swallowed for the game once the menu closes; the engines get no
    * input at all while it is open, because core_input_state_cb reports nothing.
    */
   if (down && vnovel_menu_should_draw())
   {
      unsigned mask = 0;
      switch (keycode)
      {
      case RETROK_UP:     mask = VNOVEL_MENU_UP;     break;
      case RETROK_DOWN:   mask = VNOVEL_MENU_DOWN;   break;
      case RETROK_LEFT:   mask = VNOVEL_MENU_LEFT;   break;
      case RETROK_RIGHT:  mask = VNOVEL_MENU_RIGHT;  break;
      case RETROK_RETURN: mask = VNOVEL_MENU_OK;     break;
      case RETROK_ESCAPE: mask = VNOVEL_MENU_CANCEL; break;
      default: break;
      }
      if (mask)
      {
         vnovel_menu_latch_key(mask);
         return true;
      }
   }
   return false;
}

#ifndef VNOVEL_KIRIKIRI
static void vnovel_core_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t)
{
   vnovel_core_keyboard_handle(down, keycode);
}
#endif

void retro_set_environment(retro_environment_t cb)
{
   static struct retro_log_callback log;
   environ_cb        = cb;
   environ_cb_global = cb;
   /* libretro_script_error_longjmp() and the setjmp storage it uses are both in
   * the bridge module now; nothing to register here. */
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descs);

   // Real keyboard events (Enter/Space/arrow keys) must reach the engine:
   // RetroArch's default retropad map does not carry Space.
#ifdef VNOVEL_KIRIKIRI
   static struct retro_keyboard_callback kb_cb = {krkr_keyboard_cb};
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb_cb);
#endif

   /* A core-level keyboard callback for keys the *core* owns, registered in
    * every configuration.
    *
    * The F12 toggle cannot rely on polling the key state in retro_run: that
    * state is a level sampled once per frame, so a tap whose press and release
    * both land between two frames is simply never seen. The callback is
    * edge-driven, so it cannot miss one. It is a separate registration from the
    * KiriKiri one above because that callback forwards keys to the engine and
    * this one must not.
    *
    * RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK can only be set once per frontend
    * session, so when the KiriKiri callback is registered it is the one that
    * arrives and it must dispatch core keys too - see krkr_keyboard_cb. This
    * registration covers the ONScripter/PONScripter build, where nothing else
    * claims it. */
#ifndef VNOVEL_KIRIKIRI
   {
      static struct retro_keyboard_callback core_kb_cb = {vnovel_core_keyboard_cb};
      environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &core_kb_cb);
   }
#endif

   // Core options: frame sync, indicators, debug level
   libretro_set_core_options(
         environ_cb, &opt_frame_sync, &opt_indicators, &opt_debug_level);

   // Callback to let RetroArch know we support updating options at runtime
   struct retro_core_options_update_display_callback update_cb = {
         .callback = []() -> bool
         {
            // Options are re-read via retro_variable at each retro_run
            return true;
         }};
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK,
              &update_cb);

   /* Audio buffer occupancy reporting. The frontend calls this once per frame
   * with the audio ring's occupancy (percent full) and raises `underrun_likely`
   * when it expects the ring to run dry during the next frame. This is the only
   * feedback the core gets about the ring it feeds — RetroArch's own statistics
   * printout is #ifdef DEBUG and only at deinit — and the KiriKiri submit
   * controller steers on it (see krkr_abuf_drain). */
#ifdef VNOVEL_KIRIKIRI
   static struct retro_audio_buffer_status_callback audio_status_cb = {
         [](bool active, unsigned occupancy, bool underrun_likely)
         {
            if (active)
            {
               krkr_ring_occupancy = (int)occupancy;
            }
            krkr_underrun_likely = underrun_likely ? 1 : 0;
         }};
   environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,
              &audio_status_cb);
#endif /* VNOVEL_KIRIKIRI */

   // Shoulder buttons as mouse clicks, matching the KiriKiri shim and the
   // RetroPad convention: L = left click (the primary act: confirm/advance),
   // R = right click (cancel/back, and ONScripter's right-click menu). This was
   // previously inverted (L emitted the right-click / rmenu).
   SDL_libretro_leftclick_button  = RETRO_DEVICE_ID_JOYPAD_L;
   SDL_libretro_rightclick_button = RETRO_DEVICE_ID_JOYPAD_R;
   // L button no longer sends SDLK_o (one-page mode)
   SDL_libretro_pad_mapping[RETRO_DEVICE_ID_JOYPAD_L] = ONS_KEY_NONE;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   /* Capture the real frontend callback and route all engine input reads
   * through the pad->pointer synthesizer (installed in retro_load_game). */
   frontend_input_state_cb = cb;
}

/* Deployment-identification stamp. A fixed-size NUL-terminated string that the
 * build bakes in (see BUILD_VERSION in the Makefile), so `strings` on the
 * deployed .so identifies exactly which build is running — no md5 comparison,
 * no guessing. Reported to the frontend and greppable from the binary. */
#ifndef BUILD_VERSION
#define BUILD_VERSION "dev"
#endif
/* A git abbreviation is not a fixed width (it grows when ambiguous) and
 * "-dirty" adds six more, so the literal could outgrow the buffer. The Makefile
 * truncates to fit; this fails the build loudly rather than overflow if some
 * other caller sets BUILD_VERSION too long. */
static_assert(sizeof(BUILD_VERSION) <= 16,
              "BUILD_VERSION must fit VNOVEL_LIBRETRO_VERSION[16]");
static const char VNOVEL_LIBRETRO_VERSION[16] = BUILD_VERSION;

void retro_get_system_info(struct retro_system_info *info)
{
   info->need_fullpath = true;
   /* Must match supported_extensions in vnovel_libretro.info. xp3 is here
   * because KiriKiri games are detected from data.xp3 in the loaded file's
   * own directory, and a frontend filters "Load Content" by this list — with
   * xp3 missing, selecting data.xp3 as the entry point is refused even though
   * the .info advertises it. */
   info->valid_extensions = "txt|dat|___|ons|xp3";
   info->library_version  = VNOVEL_LIBRETRO_VERSION;
   info->library_name     = "vnovel";
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   // Always report 640x480 — the engine thread hasn't initialized yet when
   // this is called, so getWidth/getHeight return garbage. The actual
   // framebuffer is passed via video_cb each frame with correct dimensions.
   int width  = 640;
   int height = 480;
   {
      const char *tw = getenv("ONS_TEST_WIDTH");
      const char *th = getenv("ONS_TEST_HEIGHT");
      if (tw)
         width = atoi(tw);
      if (th)
         height = atoi(th);
   }
   info->geometry.base_width   = width;
   info->geometry.base_height  = height;
   info->geometry.max_width    = width;
   info->geometry.max_height   = height;
   info->geometry.aspect_ratio = 4.0 / 3.0;
   info->timing.fps            = 60.0;
   /* Guess only: the engine isn't created yet (retro_get_system_av_info runs
   * before retro_load_game). ONS/PONScripter default to 44100; the KiriKiri
   * path re-emits 48000 via SET_SYSTEM_AV_INFO once its surface is up, and the
   * ONS/PONS path re-emits the real spec->freq in retro_run. So this value is
   * only in effect for the first frame or two. */
   info->timing.sample_rate = 44100.0;
   audio_logf(
         "get_system_av_info: initial sample_rate=44100 (engine not up yet)\n");
}

static void frame_cb(retro_usec_t usec)
{
   delta = usec / 1000;
}

void retro_init(void)
{
   { /* Install crash diagnostics so a core fault leaves a backtrace instead of
     * only RetroArch's bare "Process exitcode: -11". Resolve the portable log
     * location first: unlike retro_init, retro_load_game has the save
     * directory, and it runs after this per load — but RETRO_ENVIRONMENT_GET_
     * SAVE_DIRECTORY is valid as soon as the environment is set, so ask here
     * and let retro_load_game refresh it if the directory changes. */
      const char *save_dir = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir))
         ons_set_crash_log_path(save_dir);
#if !defined(_WIN32)
      /* sigaction/SA_SIGINFO/SIGBUS are POSIX; Windows has no equivalent
     * (SetUnhandledExceptionFilter is a different mechanism, and the address
     * it gives is already reported by the frontend). Skip the install there. */
      struct sigaction sa;
      memset(&sa, 0, sizeof sa);
      sa.sa_sigaction = ons_crash_handler;
      sa.sa_flags     = SA_SIGINFO;
      sigaction(SIGSEGV, &sa, NULL);
      sigaction(SIGBUS, &sa, NULL);
      sigaction(SIGABRT, &sa, NULL);
#else
      signal(SIGABRT, ons_crash_handler);
      signal(SIGSEGV, ons_crash_handler);
#endif
   }
   enum retro_pixel_format pixfmt             = RETRO_PIXEL_FORMAT_XRGB8888;
   struct retro_frame_time_callback frametime = {
         .callback  = frame_cb,
         .reference = 1000000 / 60,
   };
   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
   environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frametime);

   /* The game fonts used by ONScripter have no U+2026, and the bundled
   * stb_truetype glyph layer has no fontconfig fallback, so a substituted
   * ellipsis rendered as .notdef. Register the vendored fallback fonts
   * (resources/fonts/, embedded by the Makefile) for any codepoint the game
   * font lacks.
   *
   * Two fonts, because neither covers everything these games use: Mona is a
   * CJK / MS PGothic clone (excellent CJK, no Latin-1 accents), FiraSans is a
   * Latin face (no CJK). Mona is primary so Japanese and ellipsis resolve; the
   * Latin font is the secondary, used for accented Latin a game font lacks. */
   {
      extern int TTF_SetFallbackFontMem(const unsigned char *data, int size);
      extern int TTF_AddFallbackFontMem(const unsigned char *data, int size);
      TTF_SetFallbackFontMem(fallback_font_ttf, fallback_font_ttf_len);
      TTF_AddFallbackFontMem(extra_fallback_font_ttf,
                             extra_fallback_font_ttf_len);

      /* Publish the same CJK font to the engine as its last-resort whole-font
     * fallback (see engine_bridge.h). The arrays are static, so the pointer is
     * valid for the process's life. */
      vnovel_fallback_font_data = fallback_font_ttf;
      vnovel_fallback_font_len  = fallback_font_ttf_len;
   }

   /* Install the text-plane sink (engine_bridge.h). Every engine publishes its
    * text through the bridge, so this one line makes capture available to all
    * three; an engine that finds no sink keeps drawing text into its frame as
    * before (vnovel_text_plane_wanted() is the check it makes).
    *
    * Installing it does not by itself change what is displayed: the consumer
    * that composites or replaces the plane is a separate, later step, so today
    * the plane is captured and held. */
   vnovel_text_plane_cb       = vnovel_text_plane_sink;
   /* The structured text stream: the characters the engine was asked to draw,
    * with position and style (see VNovelTextRun). Installed unconditionally;
    * the sink itself only writes when VNOVEL_TEXT_RUN_LOG names a file, so a
    * run that does not ask for the log pays one pointer test per glyph and
    * nothing else. */
   vnovel_text_run_cb         = vnovel_text_run_sink;
   /* The plane dump request path (see engine_bridge.h): the engines' own
    * snapshot commands reach the core's plane copy through this. */
   vnovel_text_plane_dump_cb = vnovel_text_plane_dump_cb_impl;

   /* The navigation RESULT path (engine_bridge.h): an engine answers a
    * `select N` through this, from its own thread. Registered here beside the
    * other bridge callbacks; only the development socket ever sends a request,
    * so with no socket this is one pointer nobody calls. */
   vnovel_nav_result_cb     = vnovel_nav_result;
   vnovel_nav_result_userdata = NULL;

   /* Page-flow edges (engine_bridge.h): the engine's own "text pause armed" /
    * "running again", so the stream can be revealed in step with the game
    * rather than as fast as the look-ahead parses. */
   vnovel_page_event_cb = vnovel_page_event;
   vnovel_choice_label_cb = vnovel_choice_label;

   /* The development socket (devsock.h). Off unless asked for: a core that
    * opens a port nobody expects is a liability, not a convenience. */
   {
      const char *port = getenv("VNOVEL_DEV_PORT");
      if (port && *port)
      {
         if (vnovel_dev_socket_open(atoi(port)) == 0)
            fprintf(stderr, "devsocket: development socket on 127.0.0.1:%s\n",
                    port);
         else
            fprintf(stderr, "devsocket: could not listen on port %s\n", port);
      }
   }

   /* Phase 4: present the captured page instead of the frame. Off unless
    * asked for, so a normal run is unchanged. */
   {
      const char *tp = getenv("KRKRSDL2_TEXT_PAGE");
      text_page_mode = (tp && *tp && *tp != '0') ? 1 : 0;
      if (text_page_mode)
         fprintf(stderr, "text plane: page mode ON (presenting the captured "
                         "text instead of the frame)\n");
   }
   vnovel_text_plane_userdata = NULL;

   /* The native menu (deps/np2kai-menu). Created here so a frontend action can
    * open it later; NP2kai's menu needs its font manager up before it can
    * measure text, which is why this is not lazy.
    *
    * The face is the same mona.ttf the engine uses, from the engine data dir,
    * so the menu matches the game's text rather than the system's. A missing
    * font is not fatal: fontmng falls back, and the menu still functions. */
   {
      const char *dir = getenv("KRKRSDL2_DATA_PATH");
      char font_path[PATH_MAX_LENGTH];
      if (dir && *dir)
         snprintf(font_path, sizeof(font_path), "%s/mona.ttf", dir);
      else
         snprintf(font_path, sizeof(font_path), "/usr/share/krkrsdl2/mona.ttf");
      vnovel_menu_init(font_path);

      /* Smoke test: VNOVEL_MENU_TEST=<id> opens that menu page immediately, so
       * the port can be exercised without a frontend action wired to it yet.
       * The ids are NP2kai's (menubase.h); this core has no menus of its own. */
      if (vnovel_menu_is_open() == 0)
      {
         const char *mt = getenv("VNOVEL_MENU_TEST");
         if (mt && *mt)
         {
            (void)mt;
            if (vnovel_menu_open(640, 480))
               fprintf(stderr, "menu: smoke test opened the menu bar\n");
            else
               fprintf(stderr, "menu: smoke test could not open the menu bar\n");
         }
      }
   }
}

#ifdef VNOVEL_KIRIKIRI
// KiriKiri engine C functions (from engine_kirikiri_cabi.cpp)
extern "C"
{
   typedef void *EngineHandle;
   /* True when `path` is a PE executable carrying an embedded XP3 archive.
    *
    * A KiriKiri game may ship as ONE file: the runtime .exe with the game
    * archive appended after its last section, marked "XOPT_EMBED_AREA_" by the
    * engine itself. Measured on NORA! V1.02: a 304 MB PE with the XP3 at offset
    * 3,324,416 (851 files). Such a title has no data.xp3 on disk, so a
    * data.xp3-only test decides "not KiriKiri" and the run falls through to
    * ONScripter, which reports "no game script found".
    *
    * The RULE mirrors the engine's own (XP3Archive.cpp TVPGetXP3ArchiveOffset):
    * the first two bytes are "MZ", and the 11-byte XP3 mark sits at a 16-byte
    * (paragraph) boundary. Only aligned positions are tested, which is what
    * keeps this cheap on a 300 MB file. Reads only; called once per load.
    *
    * FILE SCOPE on purpose: it was first written at the detection site, which
    * is inside retro_load_game, and C does not allow a nested function
    * definition there. */
   static int file_has_embedded_xp3(const char *path)
   {
      static const unsigned char kXp3Mark[11] = {
         0x58, 0x50, 0x33, 0x0d, 0x0a, 0x20, 0x0a, 0x1a, 0x8b, 0x67, 0x01
      };
      FILE *f = fopen(path, "rb");
      if (!f)
         return 0;

      unsigned char head[2] = {0, 0};
      if (fread(head, 1, 2, f) != 2 || head[0] != 'M' || head[1] != 'Z')
      {
         fclose(f);
         return 0;
      }

      enum { WINDOW = 256 * 1024 };
      unsigned char *buf = (unsigned char *)malloc(WINDOW + 16);
      if (!buf)
      {
         fclose(f);
         return 0;
      }

      int found = 0;
      if (fseek(f, 16L, SEEK_SET) == 0)
      {
         size_t got;
         while (!found && (got = fread(buf, 1, WINDOW, f)) > 0)
         {
            size_t p = 0;
            while (p + 11 <= got)
            {
               if (memcmp(buf + p, kXp3Mark, 11) == 0)
               {
                  found = 1;
                  break;
               }
               p += 16;
            }
            if (got < WINDOW)
               break;
         }
      }

      free(buf);
      fclose(f);
      return found;
   }

   EngineHandle krkr_create(void);
   void krkr_setArchivePath(EngineHandle, const char *);
   void krkr_set_archive_file(const char *);
   void krkr_setSavePath(EngineHandle, const char *);
   void krkr_set_engine_compat(const char *era);
   void krkr_set_game_id(const char *id);
   void krkr_set_debug_flags(const char *spec);
   void krkr_set_override_dir(const char *dir);
   int krkr_init(EngineHandle);
   volatile int *krkr_get_auto_mode_ptr(void);
   volatile int *krkr_get_engine_stopped_ptr(void);
   void krkr_get_mouse_pos(int *x, int *y);
   void krkr_pump_events(void);
   void krkr_set_input_callbacks(retro_input_state_t state_cb,
                                 retro_input_poll_t poll_cb);
   void krkr_set_fastforward(int f);
   void krkr_notify_poll(long long ms);
   void krkr_set_audio_suspended(int suspended);
   void krkr_set_resolution(int w, int h);
   void krkr_request_stop(void);
   int krkr_finish_stop(void);
   void krkr_start(void);
   void krkr_push_key(int sym, int down);
   int krkr_get_audio(int16_t *out_buf, int max_frames);
   int krkr_audio_rate(void);
   int krkr_audio_channels(void);
   int krkr_audio_written(void);
   void krkr_shutdown(void);
   /* The presentation seam: install the core's callbacks, state the size the
    * engine should render at, and take each finished frame. The host contract
    * is the engine's visual/Present.h. */
   void krkr_install_present_callbacks(void);
   void krkr_set_present_size(int w, int h);
   int krkr_take_frame(const unsigned char **pixels, int *w, int *h,
                       int *pitch, unsigned long long *serial);
}
#endif /* VNOVEL_KIRIKIRI */

#ifdef VNOVEL_KIRIKIRI
/* --- KiriKiri audio buffer ------------------------------------------------
 * Holds the engine's mixed output between retro_run calls. The engine produces
 * in device quanta (1024 frames = 21.33 ms at 48 kHz) while retro_run runs at
 * the video rate, so production and consumption do not line up per call; this
 * buffer absorbs the phase difference. Each call drains it whole into the
 * frontend (see kiri_frame). */
#define KRKR_ABUF_MAX (48000 / 60 * 8) /* ~133 ms at 60 fps */
#define KRKR_QUANTUM (1024) /* engine device quantum (FAUDIO_1024_QUANTUM) */
static int16_t krkr_abuf[KRKR_ABUF_MAX * 2];
static int krkr_abuf_len     = 0; /* frames currently buffered */
static int krkr_abuf_dropped = 0; /* frames discarded on overflow */
static int krkr_abuf_peak    = 0; /* high-water mark of krkr_abuf_len */

/* Move everything the engine currently has into the buffer. When full, the
 * oldest half is dropped: audio is a stream, and a stalled consumer must not
 * make this grow without bound. */
static void krkr_abuf_fill(void)
{
   const int space = KRKR_ABUF_MAX - krkr_abuf_len;
   if (space <= 0)
   {
      /* No room: discard the oldest half to make way (audio is a stream). */
      int keep = KRKR_ABUF_MAX / 2;
      memmove(krkr_abuf,
              krkr_abuf + (krkr_abuf_len - keep) * 2,
              (size_t)keep * 2 * sizeof(int16_t));
      krkr_abuf_dropped += krkr_abuf_len - keep;
      krkr_abuf_len = keep;
   }
   int got = krkr_get_audio(krkr_abuf + krkr_abuf_len * 2,
                            KRKR_ABUF_MAX - krkr_abuf_len);
   if (got > 0)
      krkr_abuf_len += got;
   if (krkr_abuf_len > krkr_abuf_peak)
      krkr_abuf_peak = krkr_abuf_len;
}

/* Pacing state for the KiriKiri submit, shared by the drain and the suspend
 * path so a suspend can re-arm the clock. See krkr_abuf_drain. */
static long long krkr_drain_last_ms = 0;
static long long krkr_drain_owed    = 0; /* fractional frames owed */

/* Suspend the KiriKiri submit: the frontend's audio consumer is paused (audio
 * suspended across a fullscreen/window transition, or the frontend parked).
 * Discard whatever the engine produced and re-arm the pacing clock, so the next
 * enabled call starts clean rather than accreting the suspended span and
 * repaying it as a catch-up burst. Late audio has no value in a visual novel.
 * Returns 0 frames. */
static int krkr_abuf_drain_suspend(void)
{
   int g;
   do
   {
      g = krkr_get_audio(krkr_abuf, KRKR_ABUF_MAX);
   } while (g == KRKR_ABUF_MAX);
   krkr_abuf_len      = 0;
   krkr_drain_owed    = 0;
   krkr_drain_last_ms = 0; /* next enabled drain re-seeds from its own now */
   return 0;
}

/* Submit audio to the frontend, steered by RetroArch's audio-buffer-status
 * callback. Returns frames sent.
 *
 * This follows the libretro audio contract the way a normal core does: submit
 * the frames the frontend's device is consuming for the elapsed time —
 * elapsed_ms * rate / 1000, with the sub-frame remainder carried — so the
 * average is exactly the device rate and each call is smooth. RetroArch's own
 * pipe (audio/audio_driver.c) then holds about one frame in steady state and
 * rate-controls the device; a core blocks there only by over-submitting.
 *
 * The amount is drawn from the engine's captured ring. The engine's mixer runs
 * on its paced dummy device on its own thread, so its production is not exactly
 * aligned with our calls: sub-frame amounts are carried in `owed` so the long
 * run is exact, and a short ring is not padded.
 *
 * A large gap between calls is a pause (the frontend stops calling retro_run
 * while its menu is open or the window is unfocused), not audio owed. Accruing
 * it would make the core repay the whole pause as one catch-up burst on resume,
 * which overfills the frontend's ring and blocks the core — the accumulating
 * slowdown. And the engine keeps producing for the first moments of a pause
 * before it notices, leaving a backlog that would otherwise never drain and
 * persist as growing latency. On such a gap the debt is cleared and the stale
 * audio is discarded (the same thing an explicit suspend does). */
static int krkr_abuf_drain(int rate)
{
   long long now      = ons_libretro_game_clock_ms();
   long long dt       = krkr_drain_last_ms ? now - krkr_drain_last_ms : 0;
   krkr_drain_last_ms = now;
   if (dt < 0 || dt > 250)
   { /* first call, or a real pause */
      return krkr_abuf_drain_suspend();
   }
   /* A gap beyond one frame is the frontend not calling retro_run for a while —
   * a display reinit (a fullscreen toggle) or a hitch. The debt for that span is
   * forgiven rather than accrued: accruing it makes this call hand the whole gap
   * over as one burst, which overfills the frontend's ring, and under audio_sync
   * the core is then blocked in the frontend's wait, slowing retro_run further —
   * the spiral a fullscreen transition starts. Forgiving it means this call
   * submits one frame like any other: no burst, and the audio the engine
   * produced meanwhile stays in the buffer and plays out instead of being
   * dropped. */
   if (dt > 40)
   {
      /* Credit up to two frames: the frontend drained its ring during the gap, so
     * it needs a little, but more than its ring holds would burst it. */
      if (dt > 2000 / 60)
         dt = 2000 / 60;
   }
   krkr_abuf_fill();
   /* Bound the buffer to ~50 ms (three video frames). That is the latency we
   * accept: enough to absorb a transition's output and the quantum/frame phase
   * difference, small enough to stay real-time. A producer that runs away
   * cannot grow it beyond this, and the surplus is dropped here rather than
   * handed over as a backlog later — late audio has no value in a visual
   * novel. */
   {
      const int hold = rate / 60 * 3; /* ~50 ms */
      if (krkr_abuf_len > hold)
      {
         int drop = krkr_abuf_len - hold;
         memmove(krkr_abuf,
                 krkr_abuf + drop * 2,
                 (size_t)hold * 2 * sizeof(int16_t));
         krkr_abuf_dropped += drop;
         krkr_abuf_len = hold;
      }
   }
   krkr_drain_owed += dt * (long long)rate;
   long long want = krkr_drain_owed / 1000;
   if (want < 0)
      want = 0;
   int send = (int)want;
   if (send > krkr_abuf_len)
      send = krkr_abuf_len; /* never pad */
   if (send > 0 && audio_batch_cb)
   {
      krkr_drain_owed -= (long long)send * 1000;
      if (krkr_drain_owed < 0)
         krkr_drain_owed = 0;
      audio_batch_cb(krkr_abuf, (size_t)send);
      memmove(krkr_abuf,
              krkr_abuf + send * 2,
              (size_t)(krkr_abuf_len - send) * 2 * sizeof(int16_t));
      krkr_abuf_len -= send;
      return send;
   }
   return 0;
}

/* RetroArch keyboard callback -> engine key queue (KiriKiri). Also feeds the
 * SDL2 compat path used by ONScripter engines.
 *
 * The key-remap core options are semantic: the user picks which keyboard key
 * triggers Advance / Menu / Lookback etc. ONScripter applies that mapping in
 * sdl-libretro/events.c; KiriKiri gets its keys here, so the same options are
 * translated to KAG's accelerators (see kk_engine_keys) before queueing.
 * Without this the options did nothing for KiriKiri games. */
static void krkr_keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t)
{
   /* Keys the core owns are consumed here and never forwarded. This is the
    * single keyboard callback the frontend was given in this build (only one
    * registration is possible per session), so core actions must be dispatched
    * from it. */
   if (vnovel_core_keyboard_handle(down, keycode))
      return;

   /* Translate a remapped function key to what KAG expects. Keys here are
   * ASCII/control codes, where RETROK_* and SDLK_* coincide. */
   if (vtable == NULL)
   {
      for (int r = 0; r < REMAP_COUNT; r++)
      {
         if ((int)keycode == remap_keys[r] &&
             remap_keys[r] != kk_engine_keys[r])
         {
            keycode = (unsigned)kk_engine_keys[r];
            break;
         }
      }
   }
   /* RetroArch's keyboard callback may deliver only the release (up) without a
   * matching press. KAG's scenario drivers advance in onKeyDown, so synthesize
   * the missing keydown immediately before an unmatched keyup. */
   static unsigned char seen[512];
   if (keycode < 512 && down)
      seen[keycode] = 1;
   if (!down && keycode < 512 && !seen[keycode])
      krkr_push_key((int)keycode, 1);
   if (keycode < 512)
      seen[keycode] = 0;
   krkr_push_key((int)keycode, down ? 1 : 0);
}
#endif /* VNOVEL_KIRIKIRI */

bool retro_load_game(const struct retro_game_info *game)
{
   if (!game)
      return false;

   /* Install the composed dispatcher for every input read by either engine
   * (ONScripter drives it through the SDL1.2 overlay; KiriKiri through
   * krkr_set_input_callbacks). Installed here because the pointer synthesis it
   * owns needs the game's surface size to clamp the virtual pointer. */
   SDL_libretro_input_state_cb = core_input_state_cb;

   char archive_path[PATH_MAX_LENGTH];
   fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
   /* From here on archive_path and game_save_dir are canonical ('/' separators,
   * no trailing slash) and every intermediate use assumes that one form. */
   {
      char canon[PATH_MAX_LENGTH];
      canon_path(canon, sizeof(canon), archive_path);
      snprintf(archive_path, sizeof(archive_path), "%s/", canon);
   }
   // Get save directory early (needed for KiriKiri path)
   const char *save_dir = NULL;
   environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);
   /* Follow the save directory the frontend actually gave us for this load. */
   ons_set_crash_log_path(save_dir);

   /* Per-game save isolation. RetroArch/ES give us a *per-system* directory
   * (savefile_directory = /recalbox/share/saves/vnovel), which is shared by
   * every game in that system. Without a per-game subdir, Tsukihime and Le
   * sanglot des cigales would overwrite each other's save1.dat/gloval.sav.
   * Derive a stable name from the game folder (the parent dir of the ROM)
   * and nest under it. */
   char game_save_dir[PATH_MAX_LENGTH];
   {
      /* archive_path is canonical and ends in '/': drop that and take the last
     * component — the game folder name. */
      char trimmed[PATH_MAX_LENGTH];
      strncpy(trimmed, archive_path, sizeof(trimmed) - 1);
      trimmed[sizeof(trimmed) - 1] = '\0';
      size_t tl                    = strlen(trimmed);
      while (tl > 0 && trimmed[tl - 1] == '/')
         trimmed[--tl] = '\0';
      const char *base = strrchr(trimmed, '/');
      base             = base ? base + 1 : trimmed;
      char root[PATH_MAX_LENGTH];
      /* Never fall back to the game directory. Two engines sharing one save
       * directory corrupt each other's state: a half-written datasc.ksd is what
       * makes loadSystemVariables fail with 'syntax error, unexpected const',
       * and the native Fate.exe under Wine reads the very same file. The
       * frontend normally supplies a save directory; when it does not (a bare
       * retroarch invocation, a headless test), use a core-owned location under
       * the user's home so engine state still lands somewhere private and
       * per-game rather than beside the game data. */
      if (save_dir && save_dir[0])
         canon_path(root, sizeof(root), save_dir);
      else
      {
         const char *home = getenv("HOME");
         snprintf(root,
                  sizeof(root),
                  "%s/.local/share/vnovel/saves",
                  (home && home[0]) ? home : "/tmp");
      }
      fill_pathname_join(game_save_dir, root, base, sizeof(game_save_dir));
      canon_path(game_save_dir, sizeof(game_save_dir), game_save_dir);
   }

   /* Asset override directory: loose files here win over the game's own files
   * and .nsa/.sar archives (see engine_bridge.h). The engine's archive search
   * tries loose files before archive contents and stops at the first hit, so
   * prepending this directory is enough.
   *
   * It sits next to the game data — `.overrides/` beside nscript.dat,
   * pscript.dat, or the KiriKiri archive — following the softpatch convention
   * (a sibling folder, not a shared saves root; there is no unified standard
   * yet, so match the shape softpatchers already use). archive_path is the
   * ROM's game directory and ends in '/', so this is that directory plus the
   * trailing component. Static storage: the engine holds the pointer for the
   * process's life. */
   {
      static char override_dir[PATH_MAX_LENGTH];
      snprintf(
            override_dir, sizeof(override_dir), "%s.overrides/", archive_path);
      vnovel_override_dir = override_dir;
   }

   // Internal rendering resolution. "native" uses the host display size;
   // otherwise tell the engine the frontend expects a fixed resolution, so it
   // sizes itself (and its window/layer) from that. Read once here so both
   // engines see the same value: ONS via vtable->setResolution below, KiriKiri
   // via krkr_set_resolution in its branch.
   int forced_res_w = 0, forced_res_h = 0;
   {
      struct retro_variable resvar = {0};
      resvar.key                   = "vnovel_internal_resolution";
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &resvar) && resvar.value &&
          strcmp(resvar.value, "native") != 0)
      {
         unsigned a = 0, b = 0;
         if (sscanf(resvar.value, "%ux%u", &a, &b) == 2)
         {
            forced_res_w = (int)a;
            forced_res_h = (int)b;
         }
      }
   }

   // Detect engine: KiriKiri for .xp3, PONScripter for pscript.dat, ONScripter-EN otherwise
#ifdef VNOVEL_KIRIKIRI

   char xp3_check[PATH_MAX_LENGTH];
   fill_pathname_join(xp3_check, archive_path, "data.xp3", sizeof(xp3_check));
   FILE *xp3_fp = fopen(xp3_check, "rb");
   /* No `data.xp3`? A KiriKiri game may instead ship as ONE executable with the
    * archive embedded in it (see file_has_embedded_xp3). In that layout the
    * ENGINE is pointed at the executable itself: its XP3 reader finds the mark,
    * works out the offset and reads in place, so nothing is extracted and no
    * copy of a multi-hundred-MB archive is made.
    *
    * Consulted ONLY when data.xp3 is absent, so a normal install is untouched,
    * and only for a file that actually carries the mark, so an unrelated .exe
    * in the game directory cannot hijack detection. */
   char embedded_exe[PATH_MAX_LENGTH];
   embedded_exe[0] = 0;
   if (!xp3_fp)
   {
      DIR *dir = opendir(archive_path);
      if (dir)
      {
         struct dirent *de;
         while ((de = readdir(dir)) != NULL)
         {
            if (de->d_name[0] == '.')
               continue;
            char cand[PATH_MAX_LENGTH];
            fill_pathname_join(cand, archive_path, de->d_name, sizeof(cand));
            if (file_has_embedded_xp3(cand))
            {
               strncpy(embedded_exe, cand, sizeof(embedded_exe) - 1);
               embedded_exe[sizeof(embedded_exe) - 1] = 0;
               break;
            }
         }
         closedir(dir);
      }
   }
   if (xp3_fp || embedded_exe[0])
   {
      if (xp3_fp)
         fclose(xp3_fp);
      engine = krkr_create();
      if (embedded_exe[0])
      {
         log_cb(RETRO_LOG_INFO, "Using KiriKiri engine (embedded XP3 in %s)\n", embedded_exe);
         audio_logf("ENGINE=KiriKiri (embedded XP3) save=%s\n", game_save_dir);
      }
      else
      {
         log_cb(RETRO_LOG_INFO, "Using KiriKiri engine (data.xp3)\n");
         audio_logf("ENGINE=KiriKiri (data.xp3) save=%s\n", game_save_dir);
      }
      {
         char na[PATH_MAX_LENGTH], ns[PATH_MAX_LENGTH];
         /* The DIRECTORY always: the engine chdir's here, so a file would break
          * the working directory. An embedded-XP3 title additionally names the
          * FILE, which the engine opens as storage and reads in place. */
         krkr_setArchivePath(engine,
                             realize_path(archive_path, na, sizeof(na)));
         if (embedded_exe[0])
            krkr_set_archive_file(realize_path(embedded_exe, na, sizeof(na)));
         mkdir_p(game_save_dir);
         krkr_setSavePath(engine, realize_path(game_save_dir, ns, sizeof(ns)));
      }
      strncpy(last_save_path, game_save_dir, sizeof(last_save_path));
      /* Where saves land, for the menu. This is the recurring "where did my save
       * go" question, and the answer is a path the user can act on.
       *
       * Resolved to an absolute path first: when the frontend hands over a
       * relative ROM path (a bare `retroarch data.xp3`, as the harness does) the
       * save directory is built from its dirname and comes out as `.../vnovel/.`
       * - correct but useless to read. realpath is what makes the row say where
       * the files actually are. */
      {
         char line[160], abs[PATH_MAX_LENGTH];
         const char *shown = game_save_dir;
         if (realpath(game_save_dir, abs))
            shown = abs;
         snprintf(line, sizeof(line), "Saves: %s", shown);
         vnovel_menu_set_info(VNOVEL_MENU_INFO_SAVES, line);
      }
      /* Engine era, from the core option. 'auto' (the default) passes nothing,
       * so the engine resolves the era itself: the per-game table by this
       * game's identity, then its own default. An explicit value here is the
       * per-run override and outranks the table. Read here so it is in place
       * before krkr_init resolves the engine's options. */
      {
         struct retro_variable compvar = {0};
         compvar.key                   = "vnovel_kirikiri_compat";
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &compvar) && compvar.value)
            krkr_set_engine_compat(compvar.value);
      }
      /* Diagnostic probes, on the same channel and for the same reason: the
       * engine reads its debug word from the environment at start-up, and a
       * frontend cannot set the environment. Read here so it is in place before
       * krkr_init. The engine owns the name table; this passes the string
       * through untouched. */
      {
         struct retro_variable dbgvar = {0};
         dbgvar.key                   = "vnovel_kirikiri_debug";
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dbgvar) && dbgvar.value)
            krkr_set_debug_flags(dbgvar.value);
      }
      /* The host resource override directory, computed above as a softpatch
       * sibling of the game (<gamedir>.overrides/). It was assigned to
       * vnovel_override_dir and read by nothing until now: the KiriKiri path had
       * no override channel, so a user patch to a script inside data.xp3 could
       * not win. Same channel as the two above, for the same reason. */
      krkr_set_override_dir(vnovel_override_dir);
      /* The override directory, for the menu: whether the channel is wired at
       * all is the first thing to check when a user patch does nothing, and the
       * path is what a user needs in order to put a file there.
       *
       * Made absolute for display, for the same reason as the save path above:
       * with a relative ROM path this comes out as `./.overrides/`, which names
       * nothing a user can open. The engine keeps the relative form it was
       * given - it is resolved against the game directory there - so this is a
       * second string for the menu, not a change to what the engine uses. */
      {
         char line[160], abs[PATH_MAX_LENGTH];
         const char *shown = vnovel_override_dir;
         const char *note = "";
         if (shown && shown[0])
         {
            if (realpath(shown, abs))
               shown = abs;
            else
            {
               /* realpath needs the path to exist, and the interesting case is
                * exactly when it does not: no override directory means no
                * patches are being read, which is the answer to "why did my
                * patch do nothing". Prefix the working directory so the row
                * still names a location the user can create. */
               if (shown[0] == '.' && shown[1] == '/')
               {
                  char cwd[PATH_MAX_LENGTH];
                  if (getcwd(cwd, sizeof(cwd)))
                  {
                     snprintf(abs, sizeof(abs), "%s/%s", cwd, shown + 2);
                     shown = abs;
                  }
               }
               note = " (not present)";
            }
         }
         if (!shown || !shown[0])
            snprintf(line, sizeof(line), "Overrides: (none)");
         else
            snprintf(line, sizeof(line), "Overrides: %s%s", shown, note);
         vnovel_menu_set_info(VNOVEL_MENU_INFO_OVERRIDES, line);
      }
      /* The era option, as the user set it. This is the *request*; the engine
       * resolves it (table, per-game override, or this) and logs the result, and
       * it is the request that belongs on the menu because it is what the user
       * can change and then re-open the bar to confirm. */
      {
         char line[160];
         struct retro_variable cv = {0};
         cv.key = "vnovel_kirikiri_compat";
         const char *want = "auto";
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &cv) && cv.value)
            want = cv.value;
         snprintf(line, sizeof(line), "Era: %s (see the log for the result)",
                  want);
         vnovel_menu_set_info(VNOVEL_MENU_INFO_COMPAT, line);
      }
      /* The game's identity, so the engine can key its per-game compat table.
       * Computed here because retro_load_game is where the content path is known
       * and where the engine's compat option is read; exported on the same
       * channel and for the same reason (the mask reads the environment during
       * engine start-up, on the engine's own thread).
       *
       * The frontend's extended game information
       * (RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, legal only here) was the first
       * candidate, but its retro_game_info_ext carries no hash: only paths, a
       * frontend-specific `name`, `meta`, a size and the in-archive flags. None
       * of those identifies a release. So the id is the crc32 of data.xp3 — the
       * file engine detection already opened — which is exactly what
       * tools/hash-game.py fingerprints and vnovel.rdb is indexed on; the whole
       * file is hashed, matching that tool. When it cannot be taken the engine
       * has no table entry and falls through to the documented default era;
       * that is logged rather than guessed at. */
      {
         char gid[16];
         char line[160];
         if (game_id_for_file(xp3_check, gid, sizeof(gid)))
         {
            krkr_set_game_id(gid);
            log_cb(RETRO_LOG_INFO, "Game id: %s\n", gid);
            /* What the menu shows. The frontend's own name is not used: the
             * comment above records why it was rejected as an identity, and it
             * would be misleading here for the same reason - it is whatever the
             * frontend scraped, not something this core verified. The archive's
             * basename is real and the user recognises it; the id beside it is
             * the same one tools/hash-game.py prints, so a report can be checked
             * against the database. */
            {
               const char *base = strrchr(xp3_check, '/');
               base = base ? base + 1 : xp3_check;
               snprintf(line, sizeof(line), "Game: %s (%s)", base, gid);
            }
            vnovel_menu_set_info(VNOVEL_MENU_INFO_GAME, line);
         }
         else
         {
            krkr_set_game_id("");
            log_cb(RETRO_LOG_INFO,
                   "Game id: unavailable (data.xp3 unreadable); "
                   "compat falls back to the default era\n");
            vnovel_menu_set_info(VNOVEL_MENU_INFO_GAME,
                                 "Game: (unidentified)");
         }
         /* The engine, as this build knows it. Kept short because the row is a
          * single line in a bar. */
         snprintf(line, sizeof(line), "Engine: KiriKiri (krkrsdl2)");
         vnovel_menu_set_info(VNOVEL_MENU_INFO_ENGINE, line);
         /* This core's build, which is the first thing to quote in a report. */
         snprintf(line, sizeof(line), "Core: %s", VNOVEL_LIBRETRO_VERSION);
         vnovel_menu_set_info(VNOVEL_MENU_INFO_VERSION, line);
      }
      /* Tell the engine the frontend's expected resolution so KiriKiri sizes
     * itself (System.screenWidth/screenHeight) from it. The request is stashed
     * by the cabi and applied when krkr_init resolves the engine's setter, so
     * the engine sees it before it builds its main window. 0,0 is a no-op: the
     * host display size applies. The engine's SDL surface is not forced; the
     * core scales the finished frame. */
      if (forced_res_w > 0 && forced_res_h > 0)
      {
         krkr_set_resolution(forced_res_w, forced_res_h);
         log_cb(RETRO_LOG_INFO,
                "Internal resolution: forced %dx%d\n",
                forced_res_w,
                forced_res_h);
      }
      else
      {
         krkr_set_resolution(0, 0);
         log_cb(RETRO_LOG_INFO, "Internal resolution: native\n");
      }
      /* Read the text-plane options and make the requests BEFORE the engine
       * starts, not on the first retro_run.
       *
       * This is an initialization-order requirement, and getting it wrong is
       * invisible rather than loud. `krkr_init` below starts the engine thread,
       * which boots KAG and draws its opening text immediately - a whole page,
       * in one burst, before `retro_run` is ever called. Requesting the flags
       * per-frame therefore arrived after every glyph had already been drawn
       * and composited: the text plane reported `splice=0` for all of it (so
       * the core accumulated nothing and showed a raw line box), and the
       * artwork skip could not apply to layers already in the frame.
       *
       * Measured, and confirmed the other way round: with a slow-text scenario
       * that draws over many frames, the same requests land mid-scene and both
       * switches work (6 publications with splice=0, then 38 with splice=1).
       * So the mechanism was always correct; only the moment was wrong.
       *
       * The read must happen here rather than in the option block below, which
       * runs on the first frame - too late by exactly that burst. */
      {
         struct retro_variable kvar = {0};
         kvar.key = "vnovel_text_debug_view";
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &kvar) && kvar.value)
            opt_text_debug_view = (strcmp(kvar.value, "pip") == 0) ? 1 : 0;
         kvar.key = "vnovel_split_view";
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &kvar) && kvar.value)
            opt_split_view = (strcmp(kvar.value, "enabled") == 0) ? 1 : 0;

         /* Page mode is an env var read at load, above; keep the union rule the
          * per-frame path uses (see update_splice_request) in one place by
          * asking for the same thing here. */
         if (opt_text_debug_view || text_page_mode || opt_split_view)
         {
            vnovel_request_text_plane_splice(1);
            splice_requested = 1;
         }
         if (opt_split_view)
         {
            vnovel_request_split_view(1);
            split_requested = 1;
         }
      }
      krkr_init(engine);
      strncpy(last_archive_path, archive_path, sizeof(last_archive_path));
      return true;
   }
#endif /* VNOVEL_KIRIKIRI */
   // extern C declarations are at file scope above retro_load_game
   // Check for PONScripter
   char pscript_check[PATH_MAX_LENGTH];
   fill_pathname_join(
         pscript_check, archive_path, "pscript.dat", sizeof(pscript_check));
   FILE *pscript_fp = fopen(pscript_check, "rb");
   if (pscript_fp)
   {
      fclose(pscript_fp);
      vtable = &pons_engine_vtable;
      engine = vtable->create();
      log_cb(RETRO_LOG_INFO, "Using PONScripter engine (pscript.dat)\n");
      audio_logf("ENGINE=PONScripter (pscript.dat) save=%s\n", game_save_dir);
   }
   else
   {
      vtable = &ons_engine_vtable;
      engine = vtable->create();
      log_cb(RETRO_LOG_INFO, "Using ONScripter-EN engine (nscript.dat)\n");
      audio_logf("ENGINE=ONScripter-EN (nscript.dat) save=%s\n", game_save_dir);
   }
   {
      char na[PATH_MAX_LENGTH];
      vtable->setArchivePath(engine,
                             realize_path(archive_path, na, sizeof(na)));
   }
   /* Remember the last archive that was run, so later comparisons (a restart
   * vs. a genuine content change) have something to compare against:
   * retro_reset() recreates the engine and re-applies last_archive_path.
   * (The KiriKiri branch above sets it too, but returns early.) */
   strncpy(last_archive_path, archive_path, sizeof(last_archive_path) - 1);
   last_archive_path[sizeof(last_archive_path) - 1] = '\0';

   // Per-game save directory (see above). Fall back to the game dir if
   // RetroArch gave us no save directory at all.
   {
      const char *sp = (save_dir && save_dir[0]) ? game_save_dir : archive_path;
      char ns[PATH_MAX_LENGTH];
      mkdir_p(sp);
      vtable->setSavePath(engine, realize_path(sp, ns, sizeof(ns)));
      strncpy(last_save_path, sp, sizeof(last_save_path));
   }

   // Auto-detect encryption key from .exe in game directory.
   // Only needed for encrypted scripts (nscript.___, mode 3).
   // nscript.dat (mode 1) uses simple XOR and doesn't need a key table.
   char enc_check[PATH_MAX_LENGTH];
   fill_pathname_join(
         enc_check, archive_path, "nscript.___", sizeof(enc_check));
   FILE *enc_fp = fopen(enc_check, "rb");
   if (enc_fp)
   {
      fclose(enc_fp); // nscript.___ exists — need key table
      const char *exe_names[] = {
            "nscr.exe",
            "Tsukihime.exe",
            "onscripter.exe",
            "ひぐらしのなく頃に解.exe", // Higurashi Kai
            "game.exe", // generic fallback (renamed from Japanese)
            NULL};
      for (int i = 0; exe_names[i]; i++)
      {
         char test_path[PATH_MAX_LENGTH];
         fill_pathname_join(
               test_path, archive_path, exe_names[i], sizeof(test_path));
         FILE *fp = fopen(test_path, "rb");
         if (fp)
         {
            fclose(fp);
            vtable->setKeyEXE(engine, test_path);
            log_cb(RETRO_LOG_INFO, "Using key from: %s\n", test_path);
            break;
         }
      }
   }

   SDL_libretro_event_sem = SDL_CreateSemaphore(0);
#ifdef LIBRETRO_FRAME_SYNC
   fs_sem                = SDL_CreateSemaphore(0);
   SDL_libretro_wait_sem = NULL; // disabled during init
#endif

   // Read debug level option before starting engine
   struct retro_variable dbgvar = {0};
   dbgvar.key                   = "vnovel_debug_level";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dbgvar) && dbgvar.value)
      opt_debug_level = atoi(dbgvar.value);
   vtable->setDebugLevel(engine, opt_debug_level);
   log_cb(RETRO_LOG_INFO, "Debug level: %d\n", opt_debug_level);

   // Internal rendering resolution for the ONS/PONS engines (parsed above).
   if (vtable->setResolution)
   {
      if (forced_res_w > 0 && forced_res_h > 0)
      {
         vtable->setResolution(engine, forced_res_w, forced_res_h);
         log_cb(RETRO_LOG_INFO,
                "Internal resolution: forced %dx%d\n",
                forced_res_w,
                forced_res_h);
      }
      else
      {
         log_cb(RETRO_LOG_INFO, "Internal resolution: native\n");
      }
   }

   if (vtable->startThread)
      vtable->startThread(engine);

   return true;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

/* Stop the engine thread cooperatively.
 *
 * The ONS/PONS eventLoop blocks in SDL_WaitEvent() and leaves on SDL_QUIT
 * (both engines call endCommand() on SDL_QUIT). Killing the thread would leave
 * it mid-wait with its SDL's event and mixer state inconsistent, which makes the
 * frontend spin on shutdown. So: ask the engine to post SDL_QUIT to itself, then
 * join with a timeout. The join and the kill both run inside the engine's own
 * SDL, so they cannot be done from here. */
static void stop_engine_thread(void)
{
   if (!vtable || !engine)
      return;
   if (!vtable->isRunning || !vtable->isRunning(engine))
   {
      if (vtable->joinThread)
         vtable->joinThread(engine);
      return;
   }
   if (vtable->requestStop)
      vtable->requestStop(engine);
   if (vtable->joinThread)
      vtable->joinThread(engine);
   if (vtable->isRunning && vtable->isRunning(engine))
   {
      /* Last resort: the engine ignored SDL_QUIT (e.g. stuck in a script). */
      if (vtable->killThread)
         vtable->killThread(engine);
   }
}

void retro_deinit(void)
{
   /* Drop the development socket: the listener must not outlive the core that
    * opened it (a frontend that loads, unloads and reloads the core would
    * otherwise leave the port bound). Safe with no socket open. */
   vnovel_dev_socket_close();
}

void retro_reset(void)
{
   /* First: unpause. A reset must reach the engine even if the frontend paused
   * us; otherwise the engine thread sits in SDL_Delay's pause gate and never
   * returns to its event loop to acknowledge the reset (deadlock). in_reset
   * additionally suspends the gate for the duration, because the frontend stops
   * calling retro_run() during the reset so the heartbeat goes stale. */
   SDL_libretro_paused      = false;
   SDL_libretro_in_reset    = true;
   SDL_libretro_frontend_hb = 0; /* re-armed by the next retro_run */
   ons_libretro_clock_reset();   /* drop any in-flight FF epoch */

   /* KiriKiri runs with vtable == NULL and its engine thread lives in the cabi.
   * Reset is a two-part, engine-owned handshake: request that the engine return
   * to initial conditions, then start it again. Both parts are driven here, in
   * order, with a bounded wait for the engine to acknowledge the stop — the
   * core never tears engine state down itself and never pokes engine flags. */
#ifdef VNOVEL_KIRIKIRI
   if (!vtable)
   {
      krkr_request_stop();
      krkr_finish_stop();       /* joins + tears down once the engine unwinds */
      kiri_input_wired = false; /* new engine: callbacks must be re-installed */
      krkr_start();             /* re-arms + re-creates the engine thread */
      SDL_libretro_in_reset = false;
      return;
   }
#endif

   stop_engine_thread();
#ifdef LIBRETRO_FRAME_SYNC
   SDL_libretro_wait_sem = NULL;
#endif
   if (engine && vtable)
   {
      vtable->destroy(engine);
      engine = nullptr;
   }
   if (vtable)
   {
      engine = vtable->create();
      if (engine)
      {
         char na[PATH_MAX_LENGTH], ns[PATH_MAX_LENGTH];
         vtable->setArchivePath(
               engine, realize_path(last_archive_path, na, sizeof(na)));
         vtable->setSavePath(engine,
                             realize_path(last_save_path, ns, sizeof(ns)));
         vtable->setDebugLevel(engine, opt_debug_level);
         if (vtable->setAudioPaused)
            vtable->setAudioPaused(engine, 0);
         if (vtable->startThread)
            vtable->startThread(engine);
      }
   }
   SDL_libretro_in_reset = false;
}

extern "C" void SDL_ThreadedTimerCheck(void);

/* Read the Fast-Forward Speed option and install it as the game clock's rate.
 * Both the KiriKiri and ONS paths need this: the KiriKiri branch returns before
 * the per-frame option block that owns the parse, so without an explicit call
 * there `opt_ff_scale` stays 1.0 and FF never dilates KiriKiri's timed work. */
static void read_ff_speed_option(void)
{
   struct retro_variable var = {0};
   var.key                   = "vnovel_fastforward_speed";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int mult = 0;
      if (strcmp(var.value, "2x") == 0)
         mult = 2;
      else if (strcmp(var.value, "4x") == 0)
         mult = 4;
      else if (strcmp(var.value, "8x") == 0)
         mult = 8;
      else if (strcmp(var.value, "16x") == 0)
         mult = 16;
      else if (strcmp(var.value, "32x") == 0)
         mult = 32;
      else if (strcmp(var.value, "max") == 0)
         mult = 64;
      /* The multiplier is the FF RATE for the single game clock: the clock
     * dilates by `mult` while FF is on, so every timed engine event (ONS/PON
     * timers via SDL_GetTicks, KiriKiri timers via the installed clock)
     * accelerates by the same factor. */
      opt_ff_scale = mult > 0 ? (double)mult : 1.0;
   }
}

/* Per-frame option re-read (Quick Menu hot-reload) plus the one-time
 * frame-sync setup. Split out of retro_run purely for readability. */
static void read_frame_options(void)
{
#ifdef LIBRETRO_FRAME_SYNC
   static bool frame_sync_activated = false;
   if (!frame_sync_activated)
   {
      if (engine && vtable->drainUserEvents)
         vtable->drainUserEvents(engine);
      frame_sync_activated = true;
      {
         char env_path[PATH_MAX_LENGTH];
         fill_pathname_join(
               env_path, last_save_path, "envdata", sizeof(env_path));
         FILE *ef = fopen(env_path, "rb");
         if (ef)
         {
            fseek(ef, 0, SEEK_END);
            long esz = ftell(ef);
            fseek(ef, 0, SEEK_SET);
            unsigned char buf[256];
            size_t nr = fread(buf, 1, esz < 256 ? esz : 256, ef);
            for (size_t i = 0; i < nr; i += 16)
            {
               char hex[128] = "";
               int p         = 0;
               for (size_t j = i; j < i + 16 && j < nr; j++)
                  p += snprintf(hex + p, sizeof(hex) - p, "%02x ", buf[j]);
               log_cb(RETRO_LOG_INFO, "  %04zx: %s\n", i, hex);
            }
            fclose(ef);
         }
         else
            log_cb(RETRO_LOG_INFO, "envdata: not found (using defaults)\n");
      }
   }
   // Re-read core options each frame (allows hot-reload from Quick Menu)
   struct retro_variable var = {0};

   var.key = "vnovel_frame_sync";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      bool new_sync = (strcmp(var.value, "enabled") == 0);
      if (new_sync != opt_frame_sync)
      {
         opt_frame_sync = new_sync;
         if (opt_frame_sync && !SDL_libretro_wait_sem)
            SDL_libretro_wait_sem = NULL; /* disabled — causes deadlock */
         else if (!opt_frame_sync)
            SDL_libretro_wait_sem = NULL;
      }
   }

   var.key = "vnovel_mode_indicators";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      opt_indicators = (strcmp(var.value, "enabled") == 0);

   var.key = "vnovel_debug_level";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_debug = atoi(var.value);
      if (new_debug != opt_debug_level)
      {
         opt_debug_level = new_debug;
         if (engine)
            vtable->setDebugLevel(engine, opt_debug_level);
      }
   }

   var.key = "vnovel_software_cursor";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      opt_software_cursor = (strcmp(var.value, "enabled") == 0);

   var.key = "vnovel_text_speed";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_speed = -1;
      if (strcmp(var.value, "slow") == 0)
         new_speed = 0;
      else if (strcmp(var.value, "normal") == 0)
         new_speed = 1;
      else if (strcmp(var.value, "fast") == 0)
         new_speed = 2;
      else if (strcmp(var.value, "instant") == 0)
         new_speed = 3;
      if (new_speed != opt_text_speed)
      {
         opt_text_speed = new_speed;
         if (engine && opt_text_speed >= 0 && opt_text_speed <= 2)
            vtable->setTextSpeed(engine, opt_text_speed);
      }
   }

   read_ff_speed_option(); /* shared with the KiriKiri branch (line ~917) */

   /* Text-rendering behaviour, consumed by the ONScripter engine via the
   * bridge (engine_bridge.h). Read each frame so Quick Menu changes apply. */
   var.key = "vnovel_english_word_wrap";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      vnovel_english_word_wrap = (strcmp(var.value, "enabled") == 0);

   var.key = "vnovel_fit_oversize_banners";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      vnovel_fit_oversize_banners = (strcmp(var.value, "enabled") == 0);

   var.key = "vnovel_wrap_total_fit";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      vnovel_wrap_total_fit = (strcmp(var.value, "enabled") == 0);

   var.key = "vnovel_textbox_opacity";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int pct = atoi(var.value);
      if (pct < 0)
         pct = 0;
      if (pct > 200)
         pct = 200;
      vnovel_textbox_opacity = pct;
   }

   var.key = "vnovel_mouse_sensitivity";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      opt_mouse_sensitivity = atof(var.value);
#endif
/* Read key remap options */
#define PARSE_KEY(optname, idx)                                                \
   do                                                                          \
   {                                                                           \
      var.key = optname;                                                       \
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)       \
      {                                                                        \
         if (strcmp(var.value, "[") == 0)                                      \
            remap_keys[idx] = SDLK_LEFTBRACKET;                                \
         else if (strcmp(var.value, "]") == 0)                                 \
            remap_keys[idx] = SDLK_RIGHTBRACKET;                               \
         else if (strcmp(var.value, "h") == 0)                                 \
            remap_keys[idx] = SDLK_h;                                          \
         else if (strcmp(var.value, "q") == 0)                                 \
            remap_keys[idx] = SDLK_q;                                          \
         else if (strcmp(var.value, "b") == 0)                                 \
            remap_keys[idx] = SDLK_b;                                          \
         else if (strcmp(var.value, "backspace") == 0)                         \
            remap_keys[idx] = SDLK_BACKSPACE;                                  \
         else if (strcmp(var.value, "escape") == 0)                            \
            remap_keys[idx] = SDLK_ESCAPE;                                     \
         else if (strcmp(var.value, "x") == 0)                                 \
            remap_keys[idx] = SDLK_x;                                          \
         else if (strcmp(var.value, "return") == 0)                            \
            remap_keys[idx] = SDLK_RETURN;                                     \
         else if (strcmp(var.value, "space") == 0)                             \
            remap_keys[idx] = SDLK_SPACE;                                      \
         else if (strcmp(var.value, "z") == 0)                                 \
            remap_keys[idx] = SDLK_z;                                          \
         else if (strcmp(var.value, "l") == 0)                                 \
            remap_keys[idx] = SDLK_l;                                          \
         else if (strcmp(var.value, "f") == 0)                                 \
            remap_keys[idx] = SDLK_f;                                          \
         else                                                                  \
            remap_keys[idx] = default_remap_keys[idx];                         \
      }                                                                        \
      else                                                                     \
      {                                                                        \
         remap_keys[idx] = default_remap_keys[idx];                            \
      }                                                                        \
   } while (0)

   PARSE_KEY("vnovel_key_lookback", REMAP_LOOKBACK);
   PARSE_KEY("vnovel_key_menu", REMAP_MENU);
   PARSE_KEY("vnovel_key_advance", REMAP_ADVANCE);
}

#ifdef VNOVEL_KIRIKIRI
/* KiriKiri frame. The engine owns its own thread and publishes a video surface;
 * this drives input, pacing, the single game clock, audio and the framebuffer
 * handoff. Runs only while KiriKiri is the active engine (vtable == NULL). */
static void kiri_frame(void)
{
   /* Test auto-advance. ONS_TEST_AUTODVANCE=<frames> injects a SPACE keypress
     every N frames so automated runs can walk a scenario without a human.
     The ONScripter path gets this through its SDL overlay's event pump
     (src/sdl-libretro/events.c); KiriKiri has no such pump on this path —
     input arrives only via retro_input_state/retro_keyboard, which a headless
     run never drives — so inject here, at the per-frame boundary, where the
     engine's own SDL event queue is drained. SPACE is KAG's advance key
     (onPrimaryClick). */
   {
      const char *env = getenv("ONS_TEST_AUTODVANCE");
      int every       = env ? atoi(env) : 0;
      if (every > 0)
      {
         static int counter = 0;
         if (++counter >= every)
         {
            counter = 0;
            krkr_push_key(SDLK_SPACE, 1);
            krkr_push_key(SDLK_SPACE, 0);
         }
      }
   }
   /* The game terminated itself (KAG `[close]` -> engine TVPTerminateAsync).
     * Per the desired behaviour: pause the core permanently, blank the screen,
     * reset the state — and do NOT let the frontend quit. The engine already
     * blanked its surface and is idle; stop polling it and present black. */
   {
      volatile int *stopped = krkr_get_engine_stopped_ptr();
      if (stopped && *stopped)
      {
         static uint32_t stopped_black[640 * 480];
         memset(stopped_black, 0, sizeof(stopped_black));
         video_cb(stopped_black, 640, 480, 640 * 4);
         return;
      }
   }
   /* Frontend poll heartbeat. RetroArch has no core-visible pause callback
     * (it never calls retro_run while paused, and 1.22 does not use
     * retro_pause/retro_unpause), so pause is signalled implicitly: the engine
     * idles its loop once this stops being refreshed. Stamp it every frame. */
   {
      struct timespec hb;
      clock_gettime(CLOCK_MONOTONIC, &hb);
      krkr_notify_poll((long long)hb.tv_sec * 1000 + hb.tv_nsec / 1000000);
   }
   /* libretro fast-forward: lift the engine's frame limiter and skip the
     * core's own pacing below so it runs unthrottled. */
   bool kiri_ff = false;
   environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &kiri_ff);
   krkr_set_fastforward(kiri_ff ? 1 : 0);
   /* Drive the single game clock. The `!vtable` KiriKiri branch returns before
     * the install block further down, so the rate must be set here too — without
     * this the clock stays at 1.0 for KiriKiri and all timed work (typewriter,
     * transitions, script waits) ignores fast-forward even though frame-driven
     * work speeds up. Same cached compare as the other branch. */
   {
      read_ff_speed_option(); /* KiriKiri returns before the main option block */
      double want = kiri_ff ? opt_ff_scale : 1.0;
      if (want != clock_scale_applied)
      {
         clock_scale_applied = want;
         ons_libretro_clock_set(want);
      }
      static int kcdbg = -1;
      if (kcdbg < 0)
         kcdbg = getenv("ONS_CLOCK_DEBUG") ? 1 : 0;
      if (kcdbg)
      {
         static int kcdbg_count = 0;
         if (++kcdbg_count >= 30)
         {
            kcdbg_count = 0;
            fprintf(stderr,
                    "[clock] kiri ff=%d scale=%.3f t=%lldms\n",
                    (int)kiri_ff,
                    clock_scale_applied,
                    ons_libretro_game_clock_ms());
         }
      }
   }
   /* Pace to 60 fps. The engine's fpslimit plugin is a no-op and its main
     * loop has no pacing, so RetroArch would otherwise call retro_run as fast
     * as the video driver allows (600+ fps unlocked), copying and uploading a
     * full 800x600 frame every time. Cap it here; audio is fed from the
     * engine ring below and RetroArch's audio_sync follows. Fast-forward
     * bypasses this pacing. */
   {
      static struct timespec last = {0, 0};
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (!kiri_ff && last.tv_sec != 0)
      {
         double dt =
               (now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9;
         const double tgt = 1.0 / 60.0;
         if (dt < tgt)
         {
            double wait = tgt - dt;
            struct timespec rem;
            rem.tv_sec  = (time_t)wait;
            rem.tv_nsec = (long)((wait - (double)rem.tv_sec) * 1e9);
            nanosleep(&rem, NULL);
         }
      }
      clock_gettime(CLOCK_MONOTONIC, &now);
      last = now;
   }
   if (!kiri_input_wired && SDL_libretro_input_state_cb && input_poll_cb)
   {
      krkr_set_input_callbacks(SDL_libretro_input_state_cb, input_poll_cb);
      kiri_input_wired = true;
   }
   if (input_poll_cb)
      input_poll_cb();
   krkr_pump_events();
   if (1)
   {
      /* Ask the engine for its latest finished frame through the presentation
       * seam. The engine hands over geometry and pitch explicitly, so there is
       * no struct layout to assume and nothing to scrape out of a window.
       *
       * This replaced reading an SDL_Surface through hardcoded byte offsets
       * (s+16 for width would have been the surface's format pointer on any
       * layout change), which was only possible at all because the engine was
       * made to render into a dummy-video-driver window that the core then
       * reached into. */
      const unsigned char *frame_pixels = NULL;
      unsigned long long frame_serial = 0;
      int w = 0, h = 0, pitch = 0;
      int fr_ok = krkr_take_frame(&frame_pixels, &w, &h, &pitch, &frame_serial);
      const void *pixels = fr_ok ? (const void *)frame_pixels : NULL;
      (void)frame_serial;
      /* Sanity-bound the geometry before using it. SDL_libretro_surface is a
       * pointer the engine publishes from another thread; if it dangles (or
       * points at a half-freed surface) these fields are garbage and the
       * downstream pitch*h copy faults. A genuine CRT SuperRez mode is wide
       * and short (e.g. 1920x240), so the horizontal cap is generous (8192)
       * while the vertical one stays at real 4K-vertical (2160); garbage is
       * overwhelmingly negative or gigantic (2097259x6225971 above), so
       * bounds that are merely a bit loose still catch the vast majority.
       * Note the caps are asymmetric on purpose: a square 8192x8192 surface
       * would be 256 MiB at 32bpp, which is not a frame anyone ships. Drop
       * the frame rather than trust it. */
      if (w <= 0 || h <= 0 || w > 8192 || h > 2160 || pitch < w * 2 ||
          pitch > w * 8)
      {
         static int last_dbg = 0;
         if (last_dbg < 8)
         {
            audio_logf(
                  "KRKR REJECT geom w=%d h=%d pitch=%d pixels=%p\n",
                  w,
                  h,
                  pitch,
                  pixels);
            last_dbg++;
         }
         return;
      }
      /* retro_get_system_av_info() runs before the engine exists, so it can
       * only guess 640x480. Report the real (larger) geometry once it is known
       * so RetroArch presents the native frame (e.g. 800x600) instead of
       * cropping to 640x480.
       *
       * Grow-only: the engine repoints its surface to modal dialogs (a save
       * confirmation is 317x117), and shrinking the reported geometry would
       * resize the whole RetroArch window and misalign all further input
       * coordinates. So only ever report a surface larger than everything seen
       * so far; anything smaller is a dialog and is ignored. */
      {
         /* `max_w`/`max_h` are the growth watermark, and split view changes the
          * OUTPUT width without changing the engine's surface - so a toggle
          * would never pass the guard and the frontend would keep the old
          * window (measured: a black screen). `last_split` remembers which
          * state the watermark describes, and a change drops it to force one
          * re-report. */
         static int max_w = 0, max_h = 0;
         static int last_split = -1;
         if (last_split != opt_split_view)
         {
            last_split = opt_split_view;
            max_w = max_h = 0;
         }
         /* Split view presents twice the width (artwork | text), so the
          * geometry reported must be the OUTPUT's, not the engine surface's. */
         const int out_w = opt_split_view ? w * 2 : w;
         const int out_h = h;
         if (w > 0 && h > 0 && (out_w > max_w || out_h > max_h))
         {
            struct retro_system_av_info av = {};
            av.geometry.base_width         = out_w;
            av.geometry.base_height        = out_h;
            av.geometry.max_width          = out_w;
            av.geometry.max_height         = out_h;
            av.geometry.aspect_ratio       = (float)out_w / (float)out_h;
            av.timing.fps                  = 60.0;
            av.timing.sample_rate          = 48000.0;
            environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
            fprintf(stderr,
                    "krkr_audio: SET_SYSTEM_AV_INFO %dx%d @ 48000 Hz\n",
                    out_w,
                    out_h);
            max_w = out_w;
            max_h = out_h;
         }
      }
      if (pixels)
      {
         /* Tell the menu what size to open at (see vnovel_menu_poll_input). */
         vnovel_publish_frame_size(w, h);
         /* The page's size, for the text-plane accumulator (see the page buffer
          * in the plane state above). Same window the text surfaces live in. */
         text_page_frame_w = w;
         text_page_frame_h = h;
         /* Auto/skip mode badge: mirror of the game's own state (kag.tflags
         * .autoMode -> krkrsdl2_auto_mode: 0 off, 1 skip, 2 auto), matching the
         * ons/pons indicators so the same core option applies. */
         volatile int *am_ptr = krkr_get_auto_mode_ptr();
         int am               = am_ptr ? *am_ptr : 0;
         bool draw_auto       = opt_indicators && am == 2;
         bool draw_skip       = opt_indicators && am == 1;
         bool draw_ff         = opt_indicators && kiri_ff;
         /* Diagnostic stripe overlay (VNOVEL_PRESENT_STRIPES="height:alpha").
          * Draws cycling low-alpha tint bands over the *finished* frame, at the
          * last boundary the core controls, so nothing the engine does can paint
          * over them. Reading the result per raster line separates two cases
          * that look identical otherwise: a line whose tint is modulated by the
          * scene underneath means the content really is in the buffer, while a
          * line that is a flat tint means the buffer held nothing to modulate -
          * the content was never composited into it. */
         static int stripe_h = 0, stripe_a = 0;
         static bool stripe_init = false;
         if (!stripe_init)
         {
            stripe_init = true;
            const char *sv = getenv("VNOVEL_PRESENT_STRIPES");
            if (sv && sv[0]) sscanf(sv, "%d:%d", &stripe_h, &stripe_a);
            if (stripe_h <= 0) stripe_h = 0;
         }
         bool draw_stripes = stripe_h > 0;
         /* Software cursor: the engine renders no pointer (dummy video
         * driver), so overlay one on a shadow copy of the framebuffer.
         * The menu composites onto that same copy, so it belongs in this
         * condition too - otherwise it would open but never draw. */
         if (opt_software_cursor || draw_auto || draw_skip || draw_ff ||
             draw_stripes || vnovel_menu_should_draw() ||
             opt_text_debug_view)
         {
            static Uint32 *kshadow = NULL;
            static int kshadow_sz  = 0;
            int fb                 = pitch * h;
            if (fb > kshadow_sz)
            {
               kshadow    = (Uint32 *)realloc(kshadow, fb);
               kshadow_sz = fb;
            }
            if (kshadow)
            {
               memcpy(kshadow, pixels, fb);
               int kpitch = pitch / 4;
               if (draw_stripes)
               {
                  static const int bands[6][3] = {
                     {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
                     {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
                  };
                  for (int y = 0; y < h; y++)
                  {
                     const int *b = bands[(y / stripe_h) % 6];
                     Uint32 *row  = kshadow + (size_t)y * kpitch;
                     for (int x = 0; x < w; x++)
                     {
                        Uint32 v  = row[x];
                        int r     = (v >> 16) & 0xff;
                        int g     = (v >> 8) & 0xff;
                        int bb    = v & 0xff;
                        r  = (r * (255 - stripe_a) + b[0] * stripe_a) / 255;
                        g  = (g * (255 - stripe_a) + b[1] * stripe_a) / 255;
                        bb = (bb * (255 - stripe_a) + b[2] * stripe_a) / 255;
                        row[x] = 0xff000000u | (r << 16) | (g << 8) | bb;
                     }
                  }
               }
               if (opt_software_cursor)
               {
                  int cx = 0, cy = 0;
                  krkr_get_mouse_pos(&cx, &cy);
/* cx/cy are negative until real pointer input arrives; nothing
               * is drawn then (a clipped arrow would otherwise appear at the
               * top-left corner). */
/* Pre-rendered 12x18 cursor sprite: the art below is interpreted
               * once, into opaque pixels (0 = transparent), then each frame is
               * a straight blit instead of a character test per pixel. '#' =
               * black body, '.' = white outline, ' ' = clear. The stem is
               * offset from the tip, classic Windows-arrow shape. */
#define ONS_CURSOR_ART                                                         \
   "##          ", "#.#         ", "#..#        ", "#...#       ",             \
         "#....#      ", "#.....#     ", "#......#    ", "#.......#   ",       \
         "#........#  ", "#.........# ", "#..........#", "#.....######",       \
         " #..#.#     ", " #.#..#     ", " ##...#     ", " #....#     ",       \
         "  #..#      ", "   ##       "
                  static Uint32 cursor_sprite[18 * 12];
                  static bool cursor_ready = false;
                  if (!cursor_ready)
                  {
                     static const char *const cursor_art[18] = {ONS_CURSOR_ART};
                     for (int dy = 0; dy < 18; dy++)
                        for (int dx = 0; dx < 12; dx++)
                        {
                           char ch = cursor_art[dy][dx];
                           cursor_sprite[dy * 12 + dx] =
                                 (ch == '#')   ? 0xFF000000u
                                 : (ch == '.') ? 0xFFFFFFFFu
                                               : 0u;
                        }
                     cursor_ready = true;
                  }
                  for (int dy = 0; dy < 18 && cx >= 0 && cy >= 0; dy++)
                  {
                     for (int dx = 0; dx < 12; dx++)
                     {
                        Uint32 v = cursor_sprite[dy * 12 + dx];
                        if (!v)
                           continue;
                        int sx = cx + dx, sy = cy + dy;
                        if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                           continue;
                        kshadow[sy * kpitch + sx] = v;
                     }
                  }
               } /* opt_software_cursor */
               /* Mode indicators (top-right), matching the ons/pons colors. */
               {
                  int bx = w - 12, by = 4;
                  if (draw_auto)
                  {
                     for (int dy = 0; dy < 8; dy++)
                        for (int dx = 0; dx < 8; dx++)
                           if (by + dy >= 0 && by + dy < h && bx + dx >= 0 &&
                               bx + dx < w)
                              kshadow[(by + dy) * kpitch + (bx + dx)] =
                                    0xCC22AA22;
                     bx -= 12;
                  }
                  if (draw_skip)
                  {
                     for (int dy = 0; dy < 8; dy++)
                        for (int dx = 0; dx < 8; dx++)
                           if (by + dy >= 0 && by + dy < h && bx + dx >= 0 &&
                               bx + dx < w)
                              kshadow[(by + dy) * kpitch + (bx + dx)] =
                                    0xCCAAAA22;
                     bx -= 12;
                  }
                  if (draw_ff)
                  {
                     for (int dy = 0; dy < 8; dy++)
                        for (int dx = 0; dx < 8; dx++)
                           if (by + dy >= 0 && by + dy < h && bx + dx >= 0 &&
                               bx + dx < w)
                              kshadow[(by + dy) * kpitch + (bx + dx)] =
                                    0xCC2266FF;
                  }
               }
               /* The native menu, composited last so it sits above the game
                * and the core's own overlays. Into kshadow, never into
                * `pixels`: the engine's framebuffer and its dirty-rect
                * tracking must be left alone. */
               composite_menu(kshadow, kpitch, w, h);

               /* Split view (experimental): artwork on the left, text on the
                * right, at double width. Checked before page mode because both
                * take over the whole output and the split is the more specific
                * request; with only one of them on, the order is moot. The
                * frame here is artwork-only already: the engine skips layers
                * that have received glyphs (TVPSetSplitViewArtworkOnly). */
               if (opt_split_view)
               {
                  Uint32 *wide = split_view_compose(kshadow, kpitch, w, h);
                  if (wide)
                  {
                     video_cb(wide, w * 2, h, (w * 2) * 4);
                     return;
                  }
               }

               /* Page mode: show the captured page instead of the frame. This
                * is the consumer the text-plane seam exists for - the page is
                * what a reader or a page-based port would present.
                *
                * One shared rule decides what "the page" is (see
                * text_page_get_current): the accumulator when there is one, and
                * on an engine that publishes fragments, nothing until the
                * accumulator has something - drawing the fragment would put the
                * current line at the origin, which is the artifact this rule
                * exists to prevent. */
               if (text_page_mode)
               {
                  const unsigned char *tp = NULL;
                  int tw = 0, th = 0, tpitch = 0;
                  if (text_page_get_current(&tp, &tw, &th, &tpitch) && tp &&
                      tw > 0 && th > 0 && tpitch > 0)
                  {
                     /* The sink stores the page packed, one row after another,
                      * so the pitch it reports is a plain row size - the
                      * engine's signed stride was resolved row by row on the
                      * way in. Signed arithmetic here would walk backwards into
                      * the first row. */
                     const int rows = th < h ? th : h;
                     const int cols = (tw * 4 < w * 4) ? tw : (w * 4) / 4;
                     for (int y = 0; y < rows; y++)
                     {
                        const unsigned char *src =
                              tp + (size_t)y * (size_t)tpitch;
                        memcpy(kshadow + (size_t)y * kpitch, src,
                               (size_t)cols * 4);
                     }
                  }
               }
               /* Last, so the right pane is the frame as presented. */
               text_debug_view_apply(kshadow, kpitch, w, h);
               video_cb(kshadow, w, h, pitch);
            }
            else
            {
               video_cb(pixels, w, h, pitch);
            }
         }
         else
         {
            video_cb(pixels, w, h, pitch);
         }
      }
      else
      {
         video_cb(NULL, 0, 0, 0);
      }
   }
   else
   {
      video_cb(NULL, 0, 0, 0);
   }
   /* Audio (KiriKiri): submit what the engine has produced, once per call
     * (see krkr_abuf_drain).
     *
     * A fullscreen/window transition is a pause of the audio consumer: RetroArch
     * marks audio suspended across the display reinit (retroarch.c,
     * CMD_EVENT_FULLSCREEN_TOGGLE), which GET_AUDIO_VIDEO_ENABLE reports as the
     * audio bit clear. Feeding during it only piles a backlog into the frontend
     * that it then throttles against (the drifting slowdown a switch caused).
     * So treat it like the pause path: discard the stale audio and clear the
     * debt, and resume cleanly when audio is enabled again. */
   {
      int av = 1;
      bool audio_on =
            !(environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av)) ||
            (av & 1);
      /* Tell the engine on the edge, so it stops advancing its mixer while the
       * frontend is not consuming (and resumes cleanly after). Logged, because
       * a window/fullscreen transition is the case this exists for and the log
       * is how we confirm the core saw it. */
      static bool audio_on_prev = true;
      if (audio_on != audio_on_prev)
      {
         fprintf(stderr,
                 "krkr_audio: audio_av=%d (edge) -> engine suspended=%d\n",
                 audio_on ? 1 : 0,
                 audio_on ? 0 : 1);
         krkr_set_audio_suspended(audio_on ? 0 : 1);
         audio_on_prev = audio_on;
      }
      int got;
      if (!audio_on)
      {
         got = krkr_abuf_drain_suspend();
      }
      else
      {
         const int rate = krkr_audio_rate() ? krkr_audio_rate() : 48000;
         got            = krkr_abuf_drain(rate);
      }
      /* Audio diagnosis: what the engine's ring says its rate is, what the
       * frontend was told, and how many frames we actually hand it per second.
       * If the ring rate and the frontend rate disagree (or the produced rate
       * drifts from the ring rate), that is the slowdown/underrun signature.
       * One line per second, to the same log as everything else. */
      {
         static long long t0   = 0;
         static int frames_out = 0;
         long long now         = ons_libretro_game_clock_ms();
         frames_out += got;
         if (t0 == 0)
            t0 = now;
         else if (now - t0 >= 1000)
         {
            static int last_written = 0;
            int written             = krkr_audio_written();
            int produced            = last_written ? written - last_written : 0;
            last_written            = written;
            /* One-second heartbeat, useful while working on audio and pure
             * noise otherwise (it interleaves with the engine log, which is
             * the one that carries meaning). Compiled out unless asked for:
             * -DVNOVEL_AUDIO_LOG. */
#ifdef VNOVEL_AUDIO_LOG
            {
               double secs = (double)(now - t0) / 1000.0;
               fprintf(stderr,
                       "krkr_audio: out=%.0f fps produced=%d/s ring_occ=%d%% "
                       "buf_pk=%d drop=%d ch=%d\n",
                       frames_out / secs,
                       produced,
                       krkr_ring_occupancy,
                       krkr_abuf_peak,
                       krkr_abuf_dropped,
                       krkr_audio_channels());
            }
#else
            (void)frames_out; (void)produced;
#endif
            t0         = now;
            frames_out = 0;
         }
      }
   }
}
#endif /* VNOVEL_KIRIKIRI */

/* Pull one batch from the engine's mixer and hand it to the frontend.
 *
 * `rate` is passed in because retro_run already fetched it for the AV geometry;
 * calling the accessor again per frame would be wasted work. */
static void
ons_submit_audio(int rate, int16_t *stream, size_t max_frames, size_t frames)
{
   if (!engine || !vtable->getAudio || frames > max_frames)
      return;
   /* Paused or closed audio yields nothing; getAudio reports that as 0. */
   if (vtable->isAudioPaused && vtable->isAudioPaused(engine))
   {
      static bool logged = false;
      if (!logged)
      {
         logged = true;
         audio_logf("STATUS paused freq=%d\n", rate);
      }
      return;
   }
   int got = vtable->getAudio(engine, stream, (int)frames);
   if (got <= 0)
      return;
   /* Heartbeat every ~120 frames: does the mixer actually produce samples
   * (non-silence), or is it handing back zeros? */
   static int hb = 0;
   if (++hb >= 120)
   {
      hb       = 0;
      int peak = 0;
      for (size_t i = 0; i < (size_t)got * 2; i++)
      {
         int v = stream[i];
         if (v < 0)
            v = -v;
         if (v > peak)
            peak = v;
      }
      audio_logf("HB freq=%d frames=%d peak=%d running=%d\n",
                 rate,
                 got,
                 peak,
                 (vtable->isRunning ? vtable->isRunning(engine) : 0));
   }
   audio_batch_cb(stream, (size_t)got);
}

/* Choose between a direct framebuffer hand-off and a shadow copy that
 * carries the mode badges and software cursor. The engine framebuffer is
 * never modified.
 *
 * Takes an EngineSurface rather than an SDL_Surface: the core does not include
 * SDL headers, so the engine's SDL cannot be confused with an SDL2/SDL3 the
 * frontend loads globally. Same four fields. */
static void ons_present_frame(const EngineSurface *screen)
{
   if (screen)
      vnovel_publish_frame_size(screen->width, screen->height);
   /* The page's size, for the text-plane accumulator (see the page buffer in the
    * plane state above). */
   if (screen)
   {
      text_page_frame_w = screen->width;
      text_page_frame_h = screen->height;
   }
   bool draw_auto = opt_indicators && engine && vtable->isAutoMode(engine);
   bool draw_skip = opt_indicators && engine && vtable->isSkipMode(engine);
   // Use a shadow buffer for overlays (indicators + cursor) so the engine's
   // framebuffer is never modified. Always use shadow when indicators or
   // cursor are enabled; otherwise pass the framebuffer directly.
   /* The menu needs the shadow too. It composites onto that copy, never onto
    * the engine's framebuffer, so the engine's own redraw is untouched - but if
    * the shadow branch is skipped the menu would be silently invisible, which
    * is why it belongs in this condition and not only in the draw below. */
   bool need_shadow = opt_software_cursor || draw_auto || draw_skip ||
                      vnovel_menu_should_draw() || opt_text_debug_view;
   if (need_shadow && screen && screen->pixels)
   {
      static Uint32 *shadow  = NULL;
      static int shadow_size = 0;
      /* Size by the row *stride*, not by width*4. The engine's surface is
       * allowed to pad each row (SDL surfaces are commonly aligned), and this
       * buffer is both indexed with the stride below and handed to video_cb()
       * with that same stride. Sizing it width*height*4 while addressing it as
       * pitch*height means that as soon as pitch > width*4 the rows walk off the
       * end of the allocation and read into the following row's bytes - a
       * stride repeat, which shows as a diagonal shear/ghost rather than
       * black - and the frontend, told the rows are `pitch` apart, reads the
       * same undersized buffer the same wrong way. */
      int fb_size            = screen->pitch * screen->height;
      if (fb_size <= 0)
      {
         video_cb(NULL, 0, 0, 0);
         return;
      }
      if (fb_size > shadow_size)
      {
         shadow      = (Uint32 *)realloc(shadow, fb_size);
         shadow_size = fb_size;
      }
      if (!shadow)
      {
         video_cb(NULL, 0, 0, 0);
         return;
      }
      memcpy(shadow, screen->pixels, fb_size);
      int w = screen->width, h = screen->height;
      int pitch = screen->pitch / 4;
      /* Draw mode indicators (top-right corner) */
      int bx = w - 12, by = 4;
      if (draw_auto)
      {
         for (int dy = 0; dy < 8; dy++)
            for (int dx = 0; dx < 8; dx++)
               if (by + dy >= 0 && by + dy < h && bx + dx >= 0 && bx + dx < w)
                  shadow[(by + dy) * pitch + (bx + dx)] = 0xCC22AA22;
         bx -= 12;
      }
      if (draw_skip)
      {
         for (int dy = 0; dy < 8; dy++)
            for (int dx = 0; dx < 8; dx++)
               if (by + dy >= 0 && by + dy < h && bx + dx >= 0 && bx + dx < w)
                  shadow[(by + dy) * pitch + (bx + dx)] = 0xCCAAAA22;
      }
      /* Draw software cursor on shadow (not on engine framebuffer) */
      if (opt_software_cursor)
      {
         int cx, cy;
         SDL_libretro_get_mouse_pos(&cx, &cy);
         /* Off-screen (negative) until real pointer input: draw nothing then. */
         for (int dy = 0; dy < 12 && cx >= 0 && cy >= 0; dy++)
         {
            for (int dx = 0; dx < 12; dx++)
            {
               int sx = cx + dx, sy = cy + dy;
               if (sx >= 0 && sx < w && sy >= 0 && sy < h)
               {
                  if (dx == 0 || dy == dx)
                     shadow[sy * pitch + sx] = 0xFFFFFFFF;
               }
            }
         }
      }
      /* The native menu draws over everything, on the same shadow buffer the
       * indicators and cursor use, so the engine's framebuffer is never
       * modified. */
      composite_menu(shadow, pitch, w, h);
      /* Split view (experimental): put the text beside the artwork instead of
       * presenting the frame alone. Takes over the whole output, so it is the
       * last thing to touch the frame; the debug view below is then off (they
       * both present a composed image and are not meant to combine). */
      if (opt_split_view)
      {
         Uint32 *wide = split_view_compose(shadow, pitch, w, h);
         if (wide)
         {
            video_cb(wide, w * 2, h, (w * 2) * 4);
            return;
         }
      }
      /* Last, so the right pane is the frame as presented. */
      text_debug_view_apply(shadow, pitch, w, h);
      video_cb(shadow, screen->width, screen->height, screen->pitch);
   }
   else if (screen && screen->pixels)
   {
      video_cb(screen->pixels, screen->width, screen->height, screen->pitch);
   }
   else
   {
      video_cb(NULL, 0, 0, 0);
   }
}

/* Drive the single game clock. Only touch it when the effective rate
 * actually changes (FF on/off, or the option changed) — a plain double
 * compare per frame, no store, no call in the common case. `scale` is the
 * Nx only while FF is on; 1.0 otherwise. */
static void ons_drive_clock(bool ff)
{
   double want = ff ? opt_ff_scale : 1.0;
   if (want != clock_scale_applied)
   {
      clock_scale_applied = want;
      ons_libretro_clock_set(want);
   }
   /* ONS_CLOCK_DEBUG: ~2 Hz trace of the live clock so a human can watch the
   * bias grow at (scale-1)x while FF is on and freeze when it is off. */
   static int cdbg = -1;
   if (cdbg < 0)
      cdbg = getenv("ONS_CLOCK_DEBUG") ? 1 : 0;
   if (cdbg)
   {
      static int cdbg_count = 0;
      if (++cdbg_count >= 30)
      {
         cdbg_count = 0;
         fprintf(stderr,
                 "[clock] ff=%d scale=%.3f t=%lldms\n",
                 (int)ff,
                 clock_scale_applied,
                 ons_libretro_game_clock_ms());
      }
   }
}

/* Frontend heartbeat for the ONS/PONS engine thread. RetroArch stops calling
 * retro_run entirely while its menu is open, so a paused flag set here would
 * never be observed. The engine thread instead watches this timestamp:
 * SDL_Delay() blocks when the heartbeat goes stale (see SDL_systimer.c), which
 * freezes text/animation exactly like a pause. */
static void stamp_frontend_heartbeat(void)
{
   struct timespec hb;
   clock_gettime(CLOCK_MONOTONIC, &hb);
   SDL_libretro_frontend_hb =
         (unsigned long long)hb.tv_sec * 1000 + hb.tv_nsec / 1000000;
}

/* Pointer options drive the input callback for *both* engines, and the KiriKiri
 * branch returns before the main option block, so read them where every path
 * sees them. */
static void read_pointer_options(void)
{
   struct retro_variable pvar = {0};
   pvar.key                   = "vnovel_pointer_enable";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
      opt_pointer_enable = (strcmp(pvar.value, "enabled") == 0);
   pvar.key = "vnovel_pointer_source";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
      opt_pointer_source = (strcmp(pvar.value, "left") == 0)
                                 ? POINTER_SOURCE_LEFT
                                 : POINTER_SOURCE_RIGHT;
}

/* The text debug view option. Read on the same shared per-frame path as the
 * pointer options, not in read_frame_options(): the KiriKiri branch returns
 * through kiri_frame() before that block, so an option read there would never
 * take effect on KiriKiri. */
static void read_text_debug_view_option(void)
{
   struct retro_variable dvar = {0};
   dvar.key                   = "vnovel_text_debug_view";
   int want = opt_text_debug_view;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dvar) && dvar.value)
      want = (strcmp(dvar.value, "pip") == 0) ? 1 : 0;

   if (want != opt_text_debug_view)
      opt_text_debug_view = want;

   /* Split view. Read here, on the same shared path, for the same reason as the
    * debug view: the KiriKiri branch returns through kiri_frame() before
    * read_frame_options(), so an option read there would never take effect on
    * the engine this mode exists for. */
   dvar.key = "vnovel_split_view";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &dvar) && dvar.value)
      opt_split_view = (strcmp(dvar.value, "enabled") == 0) ? 1 : 0;
}

/* Whether anything currently wants a page assembled. Called after every option
 * that can ask for one, so the request tracks the union of consumers rather
 * than one of them: page mode needs the accumulated page just as much as the
 * debug view does, and tying the request to only the debug view meant page mode
 * read an accumulation that was never built (it showed one line on KiriKiri).
 *
 * Asked on change only - the engines gate their own per-publication work on it,
 * and the request is not free to repeat per frame. */
static void update_splice_request(void)
{
   int want = opt_text_debug_view || text_page_mode || opt_split_view;
   if (want != splice_requested)
   {
      splice_requested = want;
      vnovel_request_text_plane_splice(splice_requested);
      /* Whatever was accumulated under the old setting does not correspond to
       * what the consumers now want: drop it so nothing shows a stale page, and
       * the next publication rebuilds from scratch. */
      if (!splice_requested)
      {
         page_w = page_h = 0;
         page_dirty       = 0;
      }
   }

   /* The split view needs the text as a page of its own (the right half), so it
    * asks for the same accumulation the debug view does; and it asks the engine
    * to compose artwork only, so the left half has no text in it. Both are
    * change-driven: they gate engine-side work. */
   if (opt_split_view != split_requested)
   {
      split_requested = opt_split_view;
      vnovel_request_split_view(split_requested);
   }
}

/* The menu hotkey. Read on the same per-frame path as the pointer options so a
 * change takes effect without a restart, and so the binding is a property of
 * the option rather than of a key name compiled into the handler.
 *
 * The default is `none` - unbound - deliberately. The menu bar is the core's
 * own overlay, not something the game draws, and a game that never asks for one
 * must not have a key that puts one on screen; a user who wants it binds it. */
static void read_menu_hotkey(void)
{
   struct retro_variable mvar = {0};
   mvar.key                   = "vnovel_menu_hotkey";
   if (!(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &mvar) && mvar.value))
      return;

   unsigned k = 0;
   if      (!strcmp(mvar.value, "F12"))       k = RETROK_F12;
   else if (!strcmp(mvar.value, "F11"))       k = RETROK_F11;
   else if (!strcmp(mvar.value, "F10"))       k = RETROK_F10;
   else if (!strcmp(mvar.value, "Tab"))       k = RETROK_TAB;
   else if (!strcmp(mvar.value, "Backquote")) k = RETROK_BACKQUOTE;
   menu_hotkey = k; /* "none" or anything unrecognised leaves it unbound */
}

void retro_run(void)
{
   stamp_frontend_heartbeat();
   read_pointer_options();
   read_text_debug_view_option();
   update_splice_request();
   read_menu_hotkey();

   /* The development socket (devsock.h). Read here, on the frontend's thread,
    * before the engines run: this is the same place the frontend's own input is
    * read, so a `select N` lands in the same frame the engine's next loop
    * iteration sees. A no-op with no socket open (one listener test) or no
    * consumer connected (one accept test). */
   vnovel_dev_socket_poll(vnovel_dev_line, NULL);

   /* The menu hotkey is handled by the keyboard callback
    * (vnovel_core_keyboard_handle), not polled here: the polled key state is a
    * level sampled once per frame, so a tap whose press and release both land
    * between two frames is never seen. Only the option is read here. */

   /* Decide once, at frame start, whether the menu owns this frame's input (see
    * menu_owns_input), then feed it before the engines run. */
   menu_owns_input = vnovel_menu_should_draw();
   vnovel_menu_poll_input();
   vnovel_menu_poll_keys();
   /* The hot zone's synthesized click lasts a couple of frames, so the engine's
    * SDL shim sees a press and then a release (it synthesizes the pair from what
    * core_input_state_cb reports). */
   if (hotzone_right_frames > 0)
      hotzone_right_frames--;

   /* The menu advances one frame whether or not it is drawn: it must process
    * input and its own timers while open, and doing it unconditionally keeps
    * "open but hidden" from freezing. */
   vnovel_menu_proc();

   /* KiriKiri runs its own engine thread and owns its frame loop; ONS/PONS do
   * not, so the two paths are entirely separate. */
#ifdef VNOVEL_KIRIKIRI
   if (!vtable)
   {
      kiri_frame();
      return;
   }
#endif
   if (!vtable->isRunning || !vtable->isRunning(engine))
   {
      static Uint32 black[640 * 480];
      memset(black, 0, sizeof(black));
      video_cb(black, 640, 480, 640 * 4);
      return;
   }
   read_frame_options();
   // Detect fast-forward mode
   bool ff = false;
   environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &ff);
   SDL_libretro_fastforward = ff;
   ons_drive_clock(ff);
   if (!engine || !vtable)
      return;
   int av_enable = 1;
   if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable) &&
       !(av_enable & 1))
   {
      SDL_libretro_paused = true;
      return;
   }
   // Unpause: clear flag and drain accumulated timer events
   if (SDL_libretro_paused)
   {
      SDL_libretro_paused = false;
      /* Drop timer events the engine queued while paused instead of replaying
     * them at once, which would make the script jump forward. */
      if (engine && vtable->drainUserEvents)
         vtable->drainUserEvents(engine);
   }

   /* The engine owns its SDL now, so ask it for the framebuffer and the audio
   * rate rather than reaching into SDL from here. Both are NULL/0 until the
   * engine's video mode and audio are up. */
   const EngineSurface *screen =
         (engine && vtable->getSurface) ? vtable->getSurface(engine) : NULL;
   int rate =
         (engine && vtable->getSampleRate) ? vtable->getSampleRate(engine) : 0;
   if (rate <= 0)
      rate = 44100;
   /* retro_get_system_av_info() runs before load_game, when the engine does not
   * exist yet, so it can only guess. Once the engine's surface is up, report
   * its real internal resolution (ONScripter/Ponscripter derive it from the
   * script: commonly 640x480 or 800x600) so RetroArch sizes the viewport
   * correctly. Scaling/aspect stay RetroArch's decision.
   *
   * Sample rate: report the rate the engine actually negotiated with SDL, NOT
   * a hardcoded value. ONScripter/PONScripter open at 44100; advertising 48000
   * here made RetroArch consume 800 samples/frame while the core only submitted
   * 736 (round-up of 44100/60) — a 64-sample/frame deficit that drained
   * RetroArch's audio buffer and caused periodic BGM underruns. Re-emit
   * whenever the rate appears too, since the engine may open audio after the
   * first frame without the geometry changing. */
   if (screen && screen->width > 0 && screen->height > 0)
   {
      static int last_w = 0, last_h = 0, last_rate = 0, last_split = -1;
      /* Split view doubles the presented width, and toggling it changes the
       * output without changing the engine's surface - so both the split state
       * and the rate are part of the "did the output geometry change" test. */
      const int out_w = opt_split_view ? screen->width * 2 : screen->width;
      const int out_h = screen->height;
      if (out_w != last_w || out_h != last_h || rate != last_rate ||
          opt_split_view != last_split)
      {
         struct retro_system_av_info av;
         av.geometry.base_width  = out_w;
         av.geometry.base_height = out_h;
         av.geometry.max_width   = out_w;
         av.geometry.max_height  = out_h;
         av.geometry.aspect_ratio = (float)out_w / (float)out_h;
         av.timing.fps         = 60.0;
         av.timing.sample_rate = (double)rate;
         environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
         fprintf(stderr,
                 "ons_audio: SET_SYSTEM_AV_INFO %dx%d @ %d Hz (was %d Hz)\n",
                 out_w,
                 out_h,
                 rate,
                 last_rate);
         audio_logf(
               "SET_SYSTEM_AV_INFO %dx%d @ %d Hz (was %d) frames/frame=%zu\n",
               out_w,
               out_h,
               rate,
               last_rate,
               ((size_t)rate / 60 / 32 + 1) * 32);
         last_w     = out_w;
         last_h     = out_h;
         last_rate  = rate;
         last_split = opt_split_view;
      }
   }
   static const size_t max_frames = 2048;
   static int16_t stream[max_frames * 2];
   size_t frames = (size_t)rate / 60;
   frames        = (frames / 32 + 1) * 32;
   input_poll_cb();
   SDL_libretro_input_ready = true;
   vtable->pumpEvents ? vtable->pumpEvents(engine) : (void)0;
   /* Geometry is left to the core's reported values: retro_get_system_av_info()
   * sizes the window and video_cb() dimensions drive the viewport each frame.
   * (A hardcoded 640x480 SET_GEOMETRY here would also fight the KiriKiri path,
   * which reports 800x600, and overrode the user's scaling/aspect settings.) */

   // Show toast when engine state actually changes
   static bool prev_auto = false, prev_skip = false;
   bool cur_auto = engine && vtable->isAutoMode(engine);
   bool cur_skip = engine && vtable->isSkipMode(engine);
   if (cur_auto != prev_auto)
      show_message(cur_auto ? "Auto: on" : "Auto: off");
   if (cur_skip != prev_skip)
      show_message(cur_skip ? "Skip: on" : "Skip: off");
   prev_auto = cur_auto;
   prev_skip = cur_skip;

   // Draw mode indicators (auto/skip badges) on the shadow buffer
   // along with the cursor, so the engine framebuffer is never modified.
   ons_present_frame(screen);
   ons_submit_audio(rate, stream, max_frames, frames);
}
size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   return false;
}

void retro_cheat_reset(void)
{
}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
   return false;
}

void retro_unload_game(void)
{
   /* Force the engine reachable before tearing it down. If the frontend is
   * paused (or simply stopped calling retro_run), the engine thread is parked
   * in SDL_Delay's pause gate and would never observe the shutdown request, so
   * close-content would hang. Same rule as reset: unpause first. */
   SDL_libretro_paused   = false;
   SDL_libretro_in_reset = true; /* suspend the pause gate until we're gone */
   SDL_libretro_frontend_hb = 0;
   /* Close content ends this run: drop the FF epoch/bias and rebase the clock so
   * the next load starts on a fresh timeline (retro_load_game is not guaranteed
   * to follow a reset, and a frozen bias would otherwise leak into the new
   * game). Mirrors retro_reset(). */
   ons_libretro_clock_reset();
   clock_scale_applied = -1.0; /* force retro_run to re-install the rate */

#ifdef VNOVEL_KIRIKIRI
   if (!vtable)
   {
      // KiriKiri path: shut down the krkrsdl2 engine thread cleanly.
      krkr_shutdown();
      SDL_libretro_in_reset = false;
      return;
   }
#endif
   stop_engine_thread();
   SDL_DestroySemaphore(SDL_libretro_event_sem);
   SDL_libretro_event_sem = nullptr;
#ifdef LIBRETRO_FRAME_SYNC
   SDL_libretro_wait_sem = NULL; // unblock engine before destroying
   SDL_DestroySemaphore(fs_sem);
#endif
   vtable->destroy(engine);
   engine = nullptr;
   SDL_Quit();
   SDL_libretro_in_reset = false;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   return 0;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

#ifdef ANDROID
extern "C"
{
#undef fseek
   int fseek_ons(FILE *stream, long offset, int whence)
   {
      return fseek(stream, offset, whence);
   }

#undef ftell
   long ftell_ons(FILE *stream)
   {
      return ftell(stream);
   }

#undef fgetc
   int fgetc_ons(FILE *stream)
   {
      return fgetc(stream);
   }

#undef fgets
   char *fgets_ons(char *s, int size, FILE *stream)
   {
      return fgets(s, size, stream);
   }

#undef fread
   size_t fread_ons(void *ptr, size_t size, size_t nmemb, FILE *stream)
   {
      return fread(ptr, size, nmemb, stream);
   }

#undef fopen
   FILE *fopen_ons(const char *str, const char *mode)
   {
      return fopen(str, mode);
   }

#undef mkdir
   extern int mkdir(const char *pathname, mode_t mode);
   int mkdir_ons(const char *pathname, mode_t mode)
   {
      return mkdir(pathname, mode);
   }

   void playVideoAndroid(const char *filename)
   {
   }
   int __android_log_print(int prio, const char *tag, const char *fmt, ...)
   {
      va_list va;
      va_start(va, fmt);
      log_cb(RETRO_LOG_INFO, fmt, va);
      va_end(va);
      return 0;
   }
}
#endif
