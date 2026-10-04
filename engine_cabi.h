// engine_cabi.h — C ABI for engine backends.
//
// Each engine builds as its own module (see the module layout note in engine_cabi.h), so the
// identical C++ class names in ONScripter-EN and PONScripter cannot collide in
// one symbol table. Everything the core drives in an engine goes through this
// vtable; everything the engine needs from the core goes through the bridge
// module (engine_bridge.h) or, for the frontend state, a plain bool in the
// bridge, which survives the boundary because it is a static object.
//
// The engine owns its SDL. Two engines must never host two live SDLs in one
// call path (the engine's event loop blocks in SDL_WaitEvent on its own queue,
// so a second copy would receive nothing), and the core must not reach an SDL
// the frontend can shadow: RetroArch loads SDL2/SDL3 globally, and our SDL 1.2
// symbols would otherwise resolve to those.

#ifndef ENGINE_CABI_H
#define ENGINE_CABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All functions take a handle returned by engine_create. */
typedef void* EngineHandle;

/* The engine's framebuffer, as the core needs it: dimensions and a pointer to
 * 32-bit pixels. Deliberately a plain struct rather than SDL_Surface* — the
 * core never includes SDL headers, so it cannot be confused by (or link
 * against) the frontend's SDL, and the engine keeps its SDL entirely private.
 * `pitch` is in bytes. The pixels belong to the engine and stay valid until the
 * engine is destroyed. */
typedef struct EngineSurface {
    void  *pixels;
    int    width;
    int    height;
    int    pitch;
} EngineSurface;

/* Common interface — both engines implement all of these. */
typedef struct {
    EngineHandle (*create)(void);
    void         (*destroy)(EngineHandle);
    int          (*init)(EngineHandle);
    void         (*executeLabel)(EngineHandle);
    void         (*eventLoop)(EngineHandle);
    void         (*resetCommand)(EngineHandle);
    void         (*setArchivePath)(EngineHandle, const char*);
    void         (*setSavePath)(EngineHandle, const char*);
    void         (*setKeyEXE)(EngineHandle, const char*);
    void         (*setDebugLevel)(EngineHandle, int);
    void         (*setTextSpeed)(EngineHandle, int);
    int          (*getWidth)(EngineHandle);
    int          (*getHeight)(EngineHandle);
    int          (*isAutoMode)(EngineHandle);
    int          (*isSkipMode)(EngineHandle);
    void         (*dumpState)(EngineHandle, void*);  /* FILE* */
    /* Optional: force the engine's internal rendering resolution (w/h in
     * pixels, 0/0 = native, i.e. whatever the game requests). May be NULL.
     * Must be called before init(). */
    void         (*setResolution)(EngineHandle, int w, int h);

    /* --- runtime accessors -------------------------------------------------
     * The core used to call SDL directly for these; now the engine owns SDL
     * and answers instead. Each is valid only after init() has returned; a
     * NULL return or a zeroed surface means "not up yet" and the core skips
     * the frame rather than presenting garbage. */

    /* Current framebuffer, or NULL before the video mode is set. */
    const EngineSurface *(*getSurface)(EngineHandle);

    /* Audio the engine has negotiated with SDL: sample rate, and whether audio
     * is currently paused. rate is 0 until SDL_OpenAudio has happened. */
    int  (*getSampleRate)(EngineHandle);
    int  (*isAudioPaused)(EngineHandle);
    void (*setAudioPaused)(EngineHandle, int paused);

    /* Thread ownership. The engine spawns and owns its script thread, because
     * that thread calls into the engine's own SDL (SDL_WaitEvent, timers,
     * surface updates) and a thread created through the frontend's SDL would
     * touch the wrong library. The core only asks it to start and to stop. */
    int  (*startThread)(EngineHandle);   /* 0 on success */
    void (*requestStop)(EngineHandle);   /* post SDL_QUIT to the engine queue */
    int  (*isRunning)(EngineHandle);     /* 0 once the thread has exited */
    void (*joinThread)(EngineHandle);    /* wait for it; safe to call twice */
    void (*killThread)(EngineHandle);    /* last resort when join would hang */

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
} EngineVTable;

extern const EngineVTable ons_engine_vtable;
extern const EngineVTable pons_engine_vtable;

#ifdef __cplusplus
}
#endif

#endif
