// engine_cabi_impl.h — shared delegators for the ONS/PONS C ABI wrappers.
//
// ONScripter-EN and PONScripter are forks of the same engine, so their C ABI
// wrappers are identical except for: the class name, the function prefix, the
// init/event-loop/reset method names, and the select-event constant. This
// header is #included by each cabi .cpp with those parameters bound, and emits
// the delegators and the thread body (ENGINE_CABI_DELEGATORS) plus the select
// injector and vtable (ENGINE_CABI_FINISH).
//
// Requires, from the including file:
//   engine_cabi.h already included (EngineSurface, EngineHandle, EngineVTable)
//   <stdio.h> already included (snprintf)
//   the engine label header already included (ONScripterLabel.h / PonscripterLabel.h)
//   SDL.h already reachable (SDL_keysym, SDL_Event, SDL_PushEvent, SDL_ShowCursor)
//
// ENGINE_CABI_DELEGATORS expands before #include "engine_runtime_accessors.h";
// ENGINE_CABI_FINISH expands after it (the vtable names the runtime accessors).
//
// The functions are static; defining them in two translation units is fine and
// each module keeps them private.

#ifndef ENGINE_CABI_IMPL_H
#define ENGINE_CABI_IMPL_H

#include "engine_cabi.h"

/* Class          — ONScripterLabel / PonscripterLabel
 * prefix          — ons / pons
 * INIT            — method invocation for init, e.g. ->init() / ->init(NULL)
 * RUN             — method invocation for the main loop, e.g. ->executeLabel() / ->eventLoop()
 * RESET           — method invocation for reset, e.g. ->resetCommand() / ->reset()
 *
 * INIT and RUN are each used twice: in the matching delegator and in
 * engine_do_run (the thread body), which is what the runtime accessors header
 * calls as the script thread's entry point. */
#define ENGINE_CABI_DELEGATORS(Class, prefix, INIT, RUN, RESET) \
static EngineHandle prefix##_create(void) \
{ \
   return new Class(); \
} \
static void prefix##_destroy(EngineHandle h) \
{ \
   delete (Class *)h; \
} \
static int prefix##_init(EngineHandle h) \
{ \
   return ((Class *)h)INIT; \
} \
static void prefix##_executeLabel(EngineHandle h) \
{ \
   ((Class *)h)->executeLabel(); \
} \
static void prefix##_eventLoop(EngineHandle h) \
{ \
   ((Class *)h)RUN; \
} \
static void prefix##_resetCommand(EngineHandle h) \
{ \
   ((Class *)h)RESET; \
} \
static void prefix##_setArchivePath(EngineHandle h, const char *p) \
{ \
   ((Class *)h)->setArchivePath(p); \
} \
static void prefix##_setSavePath(EngineHandle h, const char *p) \
{ \
   ((Class *)h)->setSavePath(p); \
} \
static void prefix##_setKeyEXE(EngineHandle h, const char *p) \
{ \
   ((Class *)h)->setKeyEXE(p); \
} \
static void prefix##_setDebugLevel(EngineHandle h, int l) \
{ \
   ((Class *)h)->setDebugLevel(l); \
} \
static void prefix##_setTextSpeed(EngineHandle h, int s) \
{ \
   /* Inject a key event to change text speed, same as pressing 0/1/2/3. \
    * The casts are int-to-enum conversions for the same reason as in \
    * engine_runtime_accessors.h's engine_pushKey: gcc's -fpermissive hides \
    * them, clang does not honour that flag and rejects them. */ \
   (void)h; \
   SDL_keysym sym; \
   sym.sym      = (SDLKey)(SDLK_1 + s); /* 1=slow, 2=normal, 3=fast */ \
   sym.scancode = (uint8_t)sym.sym; \
   sym.mod      = (SDLMod)0; \
   SDL_Event ev; \
   ev.type       = SDL_KEYUP; \
   ev.key.keysym = sym; \
   SDL_PushEvent(&ev); \
} \
static int prefix##_getWidth(EngineHandle h) \
{ \
   return ((Class *)h)->getWidth(); \
} \
static int prefix##_getHeight(EngineHandle h) \
{ \
   return ((Class *)h)->getHeight(); \
} \
static int prefix##_isAutoMode(EngineHandle h) \
{ \
   return ((Class *)h)->isAutoMode(); \
} \
static int prefix##_isSkipMode(EngineHandle h) \
{ \
   return ((Class *)h)->isSkipMode(); \
} \
static void prefix##_setResolution(EngineHandle h, int w, int hh) \
{ \
   if (w <= 0 || hh <= 0) \
      return; /* 0/0 = native */ \
   /* setPreferredWidth() is the engine's own rescale path (the -window-width \
    * CLI option): it sets screen_ratio1/2 so the whole render is produced at \
    * the requested width, height following the script's aspect. */ \
   char buf[32]; \
   snprintf(buf, sizeof(buf), "%d", w); \
   ((Class *)h)->setPreferredWidth(buf); \
} \
/* The script thread's body: init the engine, then run its loop; the loop \
 * returns when the engine sees SDL_QUIT. Errors thrown during either call are \
 * caught by the setjmp in engine_thread_main, which surrounds this. */ \
static void engine_do_run(EngineHandle h) \
{ \
   Class *engine = (Class *)h; \
   if (engine INIT) \
      return; \
   SDL_ShowCursor(SDL_DISABLE); \
   engine RUN; \
}

/* prefix          — ons / pons
 * SELECT_EVENT    — the private SDL user-event type the event loop handles
 *                   (ONS_SELECT_EVENT / PONS_SELECT_EVENT)
 *
 * The vtable is ABI: the slot order matches engine_cabi.h field-for-field, and
 * pushSelect is the LAST slot (engine_cabi.h documents this so a vtable built
 * before the slot existed still lines up). */
#define ENGINE_CABI_FINISH(prefix, SELECT_EVENT) \
/* Deliver an ordinal `select N` to the engine's script thread. Pushes the \
 * private SDL user event the event loop handles; the ordinal rides in \
 * event.user.code. Queued, not applied: the engine takes it when its loop \
 * next runs and reports the verdict through vnovel_publish_nav_result. */ \
static void prefix##_pushSelect(EngineHandle h, int ordinal) \
{ \
   (void)h; \
   SDL_Event event; \
   SDL_memset(&event, 0, sizeof(event)); \
   event.type = SELECT_EVENT; \
   event.user.code = ordinal; \
   SDL_PushEvent(&event); \
} \
extern "C" const EngineVTable prefix##_engine_vtable = { \
      prefix##_create, \
      prefix##_destroy, \
      prefix##_init, \
      prefix##_executeLabel, \
      prefix##_eventLoop, \
      prefix##_resetCommand, \
      prefix##_setArchivePath, \
      prefix##_setSavePath, \
      prefix##_setKeyEXE, \
      prefix##_setDebugLevel, \
      prefix##_setTextSpeed, \
      prefix##_getWidth, \
      prefix##_getHeight, \
      prefix##_isAutoMode, \
      prefix##_isSkipMode, \
      prefix##_setResolution, \
      /* runtime accessors */ \
      engine_getSurface, \
      engine_getSampleRate, \
      engine_isAudioPaused, \
      engine_setAudioPaused, \
      engine_getAudio, \
      /* thread: the engine owns the script thread, because that thread runs on \
       * the engine's own SDL and its setjmp must be on the same thread as its longjmp */ \
      engine_startThread, \
      engine_requestStop, \
      engine_isRunning, \
      engine_joinThread, \
      engine_killThread, \
      engine_pushKey, \
      engine_pumpEvents, \
      engine_drainUserEvents, \
      engine_showCursor, \
      engine_timerStartedPtr, \
      engine_timerRunningPtr, \
      prefix##_pushSelect}; /* last slot: NULL here means the engine lacks select-from-socket */

#endif /* ENGINE_CABI_IMPL_H */
