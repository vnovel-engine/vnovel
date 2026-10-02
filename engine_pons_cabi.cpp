// engine_pons_cabi.cpp — C ABI wrapper for PONScripter.
// Same pattern as engine_ons_cabi.cpp.

#include "engine_cabi.h"
#include <stdio.h>
#include "PonscripterLabel.h"

static EngineHandle pons_create(void) {
    return new PonscripterLabel();
}
static void pons_destroy(EngineHandle h) {
    delete (PonscripterLabel*)h;
}
static int pons_init(EngineHandle h) {
    return ((PonscripterLabel*)h)->init(NULL);
}
static void pons_executeLabel(EngineHandle h) {
    ((PonscripterLabel*)h)->executeLabel();
}
static void pons_eventLoop(EngineHandle h) {
    ((PonscripterLabel*)h)->eventLoop();
}
static void pons_resetCommand(EngineHandle h) {
    ((PonscripterLabel*)h)->reset();
}
static void pons_setArchivePath(EngineHandle h, const char* p) {
    ((PonscripterLabel*)h)->setArchivePath(p);
}
static void pons_setSavePath(EngineHandle h, const char* p) {
    ((PonscripterLabel*)h)->setSavePath(p);
}
static void pons_setKeyEXE(EngineHandle h, const char* p) {
    ((PonscripterLabel*)h)->setKeyEXE(p);
}
static void pons_setDebugLevel(EngineHandle h, int l) {
    ((PonscripterLabel*)h)->setDebugLevel(l);
}
static void pons_setTextSpeed(EngineHandle h, int s) {
    PonscripterLabel *pons = (PonscripterLabel*)h;
    SDL_keysym sym;
    sym.sym = SDLK_1 + s;
    sym.scancode = sym.sym;
    sym.mod = 0;
    SDL_Event ev;
    ev.type = SDL_KEYUP;
    ev.key.keysym = sym;
    SDL_PushEvent(&ev);
}
static int pons_getWidth(EngineHandle h) {
    return ((PonscripterLabel*)h)->getWidth();
}
static int pons_getHeight(EngineHandle h) {
    return ((PonscripterLabel*)h)->getHeight();
}
static int pons_isAutoMode(EngineHandle h) {
    return ((PonscripterLabel*)h)->isAutoMode();
}
static int pons_isSkipMode(EngineHandle h) {
    return ((PonscripterLabel*)h)->isSkipMode();
}
static void pons_dumpState(EngineHandle h, void* f) {
    ((PonscripterLabel*)h)->dumpState((FILE*)f);
}
static void pons_setResolution(EngineHandle h, int w, int hh) {
    if (w <= 0 || hh <= 0) return; /* 0/0 = native */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", w);
    ((PonscripterLabel*)h)->setPreferredWidth(buf);
}

extern "C" const EngineVTable pons_engine_vtable = {
    pons_create, pons_destroy, pons_init, pons_executeLabel, pons_eventLoop, pons_resetCommand,
    pons_setArchivePath, pons_setSavePath, pons_setKeyEXE, pons_setDebugLevel, pons_setTextSpeed,
    pons_getWidth, pons_getHeight, pons_isAutoMode, pons_isSkipMode, pons_dumpState,
    pons_setResolution
};
