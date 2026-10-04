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

static const EngineSurface *engine_getSurface(EngineHandle) {
    SDL_Surface *s = SDL_GetVideoSurface();
    if (!s || !s->pixels) return NULL;
    engine_surface_view.pixels = s->pixels;
    engine_surface_view.width  = s->w;
    engine_surface_view.height = s->h;
    engine_surface_view.pitch  = s->pitch;
    return &engine_surface_view;
}

/* Sample rate the engine negotiated with SDL, or 0 before SDL_OpenAudio. The
 * glue publishes the spec; the core used to read the same variable directly. */
static int engine_getSampleRate(EngineHandle) {
    extern SDL_AudioSpec *SDL_libretro_audio_spec;
    SDL_AudioSpec *spec = SDL_libretro_audio_spec;
    return (spec && spec->freq > 0) ? spec->freq : 0;
}

static int engine_isAudioPaused(EngineHandle) {
    return SDL_GetAudioStatus() != SDL_AUDIO_PLAYING;
}

static void engine_setAudioPaused(EngineHandle, int paused) {
    SDL_PauseAudio(paused ? 1 : 0);
}

static void engine_pushKey(EngineHandle, int sym, int down) {
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = sym;
    SDL_PushEvent(&ev);
}

static void engine_pumpEvents(EngineHandle) {
    SDL_libretro_PumpEvents();
}

/* Drain queued SDL_USEREVENTs. Used after unpausing: while paused the engine's
 * timer keeps posting events that were never consumed, and replaying them all
 * at once makes the script jump forward. */
static void engine_drainUserEvents(EngineHandle) {
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT)) > 0)
        ;
}

static void engine_showCursor(EngineHandle, int show) {
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

/* Timer diagnostics for the "Timer state" log line; the engine's SDL owns
 * these counters. Pointers, not values, so the core reads them fresh. */
static const int *engine_timerStartedPtr(EngineHandle) {
    extern int SDL_timer_started;
    return &SDL_timer_started;
}

static const int *engine_timerRunningPtr(EngineHandle) {
    extern int SDL_timer_running;
    return &SDL_timer_running;
}

#endif /* ENGINE_RUNTIME_ACCESSORS_H */
