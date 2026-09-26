// engine_ons_cabi.cpp — C ABI wrapper for ONScripter-EN.
// Compiled normally (no namespace), linked into the main .so.
// The ONS engine sources are compiled as a separate static library
// with -fvisibility=hidden so their symbols don't collide with PONScripter.

#include "engine_cabi.h"
#include <stdio.h>
#include "ONScripterLabel.h"

static EngineHandle ons_create(void) {
    return new ONScripterLabel();
}
static void ons_destroy(EngineHandle h) {
    delete (ONScripterLabel*)h;
}
static int ons_init(EngineHandle h) {
    return ((ONScripterLabel*)h)->init();
}
static void ons_executeLabel(EngineHandle h) {
    ((ONScripterLabel*)h)->executeLabel();
}
static void ons_eventLoop(EngineHandle h) {
    // ONScripter-EN doesn't have eventLoop; executeLabel loops internally
    ((ONScripterLabel*)h)->executeLabel();
}
static void ons_resetCommand(EngineHandle h) {
    ((ONScripterLabel*)h)->resetCommand();
}
static void ons_setArchivePath(EngineHandle h, const char* p) {
    ((ONScripterLabel*)h)->setArchivePath(p);
}
static void ons_setSavePath(EngineHandle h, const char* p) {
    ((ONScripterLabel*)h)->setSavePath(p);
}
static void ons_setKeyEXE(EngineHandle h, const char* p) {
    ((ONScripterLabel*)h)->setKeyEXE(p);
}
static void ons_setDebugLevel(EngineHandle h, int l) {
    ((ONScripterLabel*)h)->setDebugLevel(l);
}
static void ons_setTextSpeed(EngineHandle h, int s) {
    /* Inject a key event to change text speed, same as pressing 0/1/2/3 */
    ONScripterLabel *ons = (ONScripterLabel*)h;
    SDL_keysym sym;
    sym.sym = SDLK_1 + s; /* 1=slow, 2=normal, 3=fast */
    sym.scancode = sym.sym;
    sym.mod = 0;
    SDL_Event ev;
    ev.type = SDL_KEYUP;
    ev.key.keysym = sym;
    SDL_PushEvent(&ev);
}
static int ons_getWidth(EngineHandle h) {
    return ((ONScripterLabel*)h)->getWidth();
}
static int ons_getHeight(EngineHandle h) {
    return ((ONScripterLabel*)h)->getHeight();
}
static int ons_isAutoMode(EngineHandle h) {
    return ((ONScripterLabel*)h)->isAutoMode();
}
static int ons_isSkipMode(EngineHandle h) {
    return ((ONScripterLabel*)h)->isSkipMode();
}
static void ons_dumpState(EngineHandle h, void* f) {
    ((ONScripterLabel*)h)->dumpState((FILE*)f);
}

extern "C" const EngineVTable ons_engine_vtable = {
    ons_create, ons_destroy, ons_init, ons_executeLabel, ons_eventLoop, ons_resetCommand,
    ons_setArchivePath, ons_setSavePath, ons_setKeyEXE, ons_setDebugLevel, ons_setTextSpeed,
    ons_getWidth, ons_getHeight, ons_isAutoMode, ons_isSkipMode, ons_dumpState
};
