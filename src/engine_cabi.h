// engine_cabi.h — C ABI for engine backends.
//
// Each engine builds as its own module (see the module layout note in engine_cabi.h), so the
// identical C++ class names in ONScripter-EN and PONScripter cannot collide in
// one symbol table. Everything the core drives in an engine goes through this
// vtable; everything the engine needs from the core goes through the bridge
// module (engine_bridge.h) or, for the frontend state, a plain bool in the
// bridge, which survives the boundary because it is a static object.
//
// The engine owns its SDL, and the core does not link, include or reach it.
// Only one SDL may be live in a call path: the engine's event loop blocks in
// SDL_WaitEvent on its own queue, so a second copy would receive nothing. The
// core talks to SDL exclusively through the accessors below.

#ifndef ENGINE_CABI_H
#define ENGINE_CABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

   /* All functions take a handle returned by engine_create. */
   typedef void *EngineHandle;

   /* The engine's framebuffer, as the core needs it: dimensions and a pointer to
 * 32-bit pixels. Deliberately a plain struct rather than SDL_Surface* — the
 * core never includes SDL headers, so it cannot be confused by (or link
 * against) the frontend's SDL, and the engine keeps its SDL entirely private.
 * `pitch` is in bytes. The pixels belong to the engine and stay valid until the
 * engine is destroyed. */
   typedef struct EngineSurface
   {
      void *pixels;
      int width;
      int height;
      int pitch;
   } EngineSurface;

   /* Common interface — both engines implement all of these. */
   typedef struct
   {
      EngineHandle (*create)(void);
      void (*destroy)(EngineHandle);
      int (*init)(EngineHandle);
      void (*executeLabel)(EngineHandle);
      void (*eventLoop)(EngineHandle);
      void (*resetCommand)(EngineHandle);
      void (*setArchivePath)(EngineHandle, const char *);
      void (*setSavePath)(EngineHandle, const char *);
      void (*setKeyEXE)(EngineHandle, const char *);
      void (*setDebugLevel)(EngineHandle, int);
      void (*setTextSpeed)(EngineHandle, int);
      int (*getWidth)(EngineHandle);
      int (*getHeight)(EngineHandle);
      int (*isAutoMode)(EngineHandle);
      int (*isSkipMode)(EngineHandle);
      /* Optional: force the engine's internal rendering resolution (w/h in
     * pixels, 0/0 = native, i.e. whatever the game requests). May be NULL.
     * Must be called before init(). */
      void (*setResolution)(EngineHandle, int w, int h);

      /* --- runtime accessors -------------------------------------------------
     * The core reaches SDL only through these: the engine owns its SDL, so the
     * core never links or includes it. Each is valid after init() has returned;
     * a NULL return or a zeroed surface means "not up yet" and the core skips
     * the frame rather than presenting garbage. */

      /* Current framebuffer, or NULL before the video mode is set. */
      const EngineSurface *(*getSurface)(EngineHandle);

      /* Sample rate the engine negotiated with SDL; 0 before SDL_OpenAudio. */
      int (*getSampleRate)(EngineHandle);
      int (*isAudioPaused)(EngineHandle);
      void (*setAudioPaused)(EngineHandle, int paused);
      /* Pull mixed stereo frames from the engine's mixer. Returns frames
     * written; 0 when audio is closed or paused. */
      int (*getAudio)(EngineHandle, int16_t *out, int max_frames);

      /* Thread ownership. The engine spawns and owns its script thread, because
     * that thread calls into the engine's own SDL (SDL_WaitEvent, timers,
     * surface updates) and a thread created through the frontend's SDL would
     * touch the wrong library. The core only asks it to start and to stop. */
      int (*startThread)(EngineHandle);  /* 0 on success */
      void (*requestStop)(EngineHandle); /* post SDL_QUIT to the engine queue */
      int (*isRunning)(EngineHandle);    /* 0 once the thread has exited */
      void (*joinThread)(EngineHandle);  /* wait for it; safe to call twice */
      void (*killThread)(EngineHandle);  /* last resort when join would hang */

      /* Input injection: the core turns RetroPad/frontend input into the SDL
     * events the script expects, and hands them to the engine's own queue. */
      void (*pushKey)(EngineHandle, int sym, int down);
      void (*pumpEvents)(EngineHandle);

      /* Drain queued SDL_USEREVENTs (used after unpausing, so a burst of timer
     * events accumulated while paused does not replay at once). */
      void (*drainUserEvents)(EngineHandle);

      /* Show or hide the software cursor. */
      void (*showCursor)(EngineHandle, int show);

      /* Engine-owned timer diagnostics, for the "Timer state" log line. Either
     * pointer may be NULL if the engine does not expose them. */
      const int *(*timerStartedPtr)(EngineHandle);
      const int *(*timerRunningPtr)(EngineHandle);

      /* Take the Nth choice the current page offers, exactly as a click on that
     * label would (the development socket's inbound direction; see devsock.h and
     * the navigation section of engine_bridge.h). `ordinal` is 1-based, in page
     * order.
     *
     * Queued, not applied: the selector lives on the engine's own thread (the
     * live button set, which is what knows how many choices there are), so this
     * only hands the intent over. The engine takes it when its loop next runs
     * and reports the verdict through vnovel_publish_nav_result - a refusal
     * included, so a consumer learns "no such choice" rather than nothing.
     * May be NULL, and it is the last field so a vtable built before the slot
     * existed still lines up. */
      void (*pushSelect)(EngineHandle, int ordinal);
   } EngineVTable;

   extern const EngineVTable ons_engine_vtable;
   extern const EngineVTable pons_engine_vtable;

#ifdef __cplusplus
}
#endif

#endif
