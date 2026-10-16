// engine_runtime_accessors.h — the runtime accessors, shared by both engine
// modules. This file is #included (not compiled separately) by
// engine_ons_cabi.cpp and engine_pons_cabi.cpp, so each module carries its own
// copy and there is nothing to resolve across the boundary.
//
// Every accessor is engine-independent: they all answer from the vendored SDL
// and the sdl-libretro glue that the module links, so the two engines do not
// need separate implementations. Only the vtable assembly at the end of each
// cabi file names them per engine.
//
// The functions are static, so defining them in two translation units is fine
// and each module keeps them private.
//
// Requires, from the including file:
//   engine_cabi.h already included (EngineSurface, EngineHandle)
//   SDL.h and the glue's SDL_libretro.h already reachable

#ifndef ENGINE_RUNTIME_ACCESSORS_H
#define ENGINE_RUNTIME_ACCESSORS_H

#include <string.h>
#include <setjmp.h>
#include "engine_bridge.h"
/* Explicit path, not "SDL_libretro.h": a quoted include searches the includer's
 * own directory first, and a stray file of that name next to this header would
 * silently win. (One did — an empty tracked file in the repo root — and the
 * build failed with "SDL_libretro_PumpEvents was not declared".) */
#include "sdl-libretro/SDL_libretro.h"

/* The surface is copied into a plain EngineSurface so the core never sees an
 * SDL_Surface: it does not include SDL headers, so it cannot be confused by —
 * or link against — the SDL2/SDL3 a frontend loads globally.
 *
 * Rebuilt on every call rather than cached: SDL_SetVideoMode can replace the
 * surface mid-run (a script can change resolution), and a cached pointer would
 * have the core present freed pixels. The storage is static because the core
 * holds the returned pointer only until the next frame.
 */
static EngineSurface engine_surface_view;

static const EngineSurface *engine_getSurface(EngineHandle)
{
   SDL_Surface *s = SDL_GetVideoSurface();
   if (!s || !s->pixels)
      return NULL;
   engine_surface_view.pixels = s->pixels;
   engine_surface_view.width  = s->w;
   engine_surface_view.height = s->h;
   engine_surface_view.pitch  = s->pitch;
   return &engine_surface_view;
}

/* Sample rate the engine negotiated with SDL, or 0 before SDL_OpenAudio. */
static int engine_getSampleRate(EngineHandle)
{
   extern SDL_AudioSpec *SDL_libretro_audio_spec;
   SDL_AudioSpec *spec = SDL_libretro_audio_spec;
   return (spec && spec->freq > 0) ? spec->freq : 0;
}

static int engine_isAudioPaused(EngineHandle)
{
   return SDL_GetAudioStatus() != SDL_AUDIO_PLAYING;
}

static void engine_setAudioPaused(EngineHandle, int paused)
{
   SDL_PauseAudio(paused ? 1 : 0);
}

/* Fill `out` with up to `max_frames` stereo frames from the engine's mixer.
 * Returns frames written (0 if audio is closed, paused, or produced nothing).
 *
 * The mixer's callback wants a byte count, not a frame count: one stereo frame
 * is 2 int16 samples, so frames*4 bytes. */
static int engine_getAudio(EngineHandle, int16_t *out, int max_frames)
{
   extern SDL_AudioSpec *SDL_libretro_audio_spec;
   SDL_AudioSpec *spec = SDL_libretro_audio_spec;
   if (!spec || SDL_GetAudioStatus() != SDL_AUDIO_PLAYING)
      return 0;
   if (!spec->callback || max_frames <= 0)
      return 0;
   size_t bytes = (size_t)max_frames * 4;
   memset(out, 0, bytes);
   spec->callback(NULL, (uint8_t *)out, bytes);
   return max_frames;
}

static void engine_pushKey(EngineHandle, int sym, int down)
{
   SDL_Event ev;
   memset(&ev, 0, sizeof ev);
   ev.type                = down ? SDL_KEYDOWN : SDL_KEYUP;
   ev.key.keysym.sym      = sym;
   ev.key.keysym.scancode = sym;
   SDL_PushEvent(&ev);
}

static void engine_pumpEvents(EngineHandle)
{
   SDL_libretro_PumpEvents();
}

/* Drain queued SDL_USEREVENTs. Used after unpausing: while paused the engine's
 * timer keeps posting events that were never consumed, and replaying them all
 * at once makes the script jump forward. */
static void engine_drainUserEvents(EngineHandle)
{
   SDL_Event ev;
   while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT)) >
          0)
      ;
}

static void engine_showCursor(EngineHandle, int show)
{
   SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

/* Timer diagnostics for the "Timer state" log line; the engine's SDL owns
 * these counters. Pointers, not values, so the core reads them fresh. */
static const int *engine_timerStartedPtr(EngineHandle)
{
   extern int SDL_timer_started;
   return &SDL_timer_started;
}

static const int *engine_timerRunningPtr(EngineHandle)
{
   extern int SDL_timer_running;
   return &SDL_timer_running;
}

/* --- thread ownership ------------------------------------------------------
 *
 * The engine's script thread touches only the engine's own SDL (event queue,
 * timers, surface, mixer), so it must be created against that SDL and not the
 * frontend's. The same applies to the setjmp that catches engine exit(): it
 * arms a buffer that libretro_script_error_longjmp() jumps to, and longjmp
 * restores a stack frame, so both calls have to happen on one thread. Owning
 * the thread means owning the setjmp, which is why both live here.
 *
 * The buffer is vnovel_script_error_jmp, in the bridge module: static storage that
 * both sides can name, armed and jumped to only from this thread.
 *
 * The body of the thread is engine-dependent, so each cabi file defines
 * engine_do_run() after including this header. It should init the engine and
 * run its loop; a non-zero return means init failed. */
static void
      engine_do_run(EngineHandle); /* defined by the including cabi file */

static SDL_Thread *engine_thread          = NULL;
static EngineHandle engine_thread_handle  = NULL;
static volatile int engine_thread_running = 0;

static int engine_thread_main(void *)
{
   if (setjmp(vnovel_script_error_jmp) != 0)
   {
      /* The engine called errorAndExit() — a script error, or exit() deep in
         * engine code. longjmp landed here, so the thread returns cleanly
         * instead of taking the whole process down. */
      fprintf(stderr, "engine: script error — thread returning cleanly\n");
      vnovel_script_error_jmp_valid = 0;
      engine_thread_running      = 0;
      return 0;
   }
   vnovel_script_error_jmp_valid = 1;
   engine_thread_running      = 1;
   engine_do_run(engine_thread_handle);
   engine_thread_running      = 0;
   vnovel_script_error_jmp_valid = 0;
   return 0;
}

static int engine_startThread(EngineHandle h)
{
   vnovel_script_error_jmp_valid = 0;
   engine_thread_handle       = h;
   engine_thread              = SDL_CreateThread(engine_thread_main, NULL);
   return engine_thread ? 0 : -1;
}

/* Wake the engine's event loop so it can observe the shutdown request. Both
 * engines leave their loop on SDL_QUIT (they call endCommand() for it). */
static void engine_requestStop(EngineHandle)
{
   SDL_Event ev;
   memset(&ev, 0, sizeof ev);
   ev.type = SDL_QUIT;
   SDL_PushEvent(&ev);
}

static int engine_isRunning(EngineHandle)
{
   return engine_thread_running;
}

/* Wait for a clean exit. SDL 1.2 has no timed join, so poll; the engine paces
 * itself through SDL_Delay, which the pause gate in SDL_systimer.c honours. */
static void engine_joinThread(EngineHandle)
{
   if (!engine_thread)
      return;
   for (int i = 0; i < 200 && engine_thread_running; i++)
      SDL_Delay(10);
   if (!engine_thread_running)
   {
      SDL_WaitThread(engine_thread, NULL);
      engine_thread = NULL;
   }
}

/* Last resort when the engine ignored SDL_QUIT (e.g. stuck in a script).
 * SDL_KillThread leaves that thread mid-wait with SDL's event and mixer state
 * inconsistent, which makes the frontend spin on shutdown — so it is only for
 * the case where joining would hang forever. */
static void engine_killThread(EngineHandle)
{
   if (!engine_thread)
      return;
   SDL_KillThread(engine_thread);
   engine_thread         = NULL;
   engine_thread_running = 0;
}

#endif /* ENGINE_RUNTIME_ACCESSORS_H */
