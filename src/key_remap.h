// Key remapping tables for libretro core.
// Engine functions can be triggered by configurable keys that don't
// conflict with RetroArch's default hotkey bindings.
//
// RetroArch default hotkeys that conflict with engine keys:
//   h=reset, l=hold_fast_forward, f=fullscreen, k=frame_advance,
//   space=toggle_fast_forward, escape=exit, p=pause, m=shader_next,
//   n=shader_prev, i=netplay, u=cheats, t/y=cheat_index
//
// We pick defaults that avoid all of these.

#ifndef KEY_REMAP_H
#define KEY_REMAP_H

#include <libretro.h>
#include <SDL_keysym.h>

/* Engine functions that can be remapped */
typedef enum {
    REMAP_ADVANCE = 0,      // Advance text / confirm
    REMAP_MENU,             // Right-click / menu
    REMAP_SKIP,             // Toggle skip mode
    REMAP_AUTO,             // Toggle auto mode
    REMAP_LOOKBACK,         // Page back / history
    REMAP_PAGE_FORWARD,     // Page forward
    REMAP_TEXT_SPEED,       // Cycle text speed
    REMAP_FULLSCREEN,       // Toggle fullscreen
    REMAP_COUNT
} remap_function_t;

/* Default key bindings — chosen to avoid RetroArch hotkey conflicts */
static const int default_remap_keys[REMAP_COUNT] = {
    [REMAP_ADVANCE]       = SDLK_RETURN,   // Enter (not Space — RA uses Space for FF)
    [REMAP_MENU]          = SDLK_BACKSPACE, // Backspace (not Escape — RA uses Escape for exit)
    [REMAP_SKIP]          = SDLK_s,         // s (no RA conflict)
    [REMAP_AUTO]          = SDLK_a,         // a (no RA conflict)
    [REMAP_LOOKBACK]      = SDLK_LEFTBRACKET, // [ (not h — RA uses h for reset)
    [REMAP_PAGE_FORWARD]  = SDLK_RIGHTBRACKET, // ] (not l — RA uses l for hold FF)
    [REMAP_TEXT_SPEED]    = SDLK_0,         // 0 (no RA conflict)
    [REMAP_FULLSCREEN]    = SDLK_F11,       // F11 (matches RA grab_mouse_toggle, but RA's F is fullscreen)
};

/* What SDL key the engine expects for each function */
static const int engine_keys[REMAP_COUNT] = {
    [REMAP_ADVANCE]       = SDLK_RETURN,
    [REMAP_MENU]          = SDLK_ESCAPE,    // Engine uses Escape for menu/right-click
    [REMAP_SKIP]          = SDLK_s,
    [REMAP_AUTO]          = SDLK_a,
    [REMAP_LOOKBACK]      = SDLK_h,         // Engine uses h for lookback
    [REMAP_PAGE_FORWARD]  = SDLK_l,         // Engine uses l for page forward
    [REMAP_TEXT_SPEED]    = SDLK_0,         // Engine uses 0 for text speed cycle
    [REMAP_FULLSCREEN]    = SDLK_f,         // Engine uses f for fullscreen
};

/* What key the *KiriKiri/KAG* engine expects for the same functions. KAG's
 * accelerators differ from ONScripter's (notably skip is 'f', not 's'), so the
 * key-remap options must be translated per engine or they silently do the
 * wrong thing. Backlog has no universal KAG key ("r" is the common convention,
 * and what The House in Fata Morgana uses); it stays game-dependent. */
static const int kk_engine_keys[REMAP_COUNT] = {
    [REMAP_ADVANCE]       = SDLK_RETURN,
    [REMAP_MENU]          = SDLK_ESCAPE,
    [REMAP_SKIP]          = SDLK_f,         // KAG skip accelerator
    [REMAP_AUTO]          = SDLK_a,         // KAG auto accelerator
    [REMAP_LOOKBACK]      = SDLK_r,         // common KAG backlog key (game-dependent)
    [REMAP_PAGE_FORWARD]  = SDLK_l,
    [REMAP_TEXT_SPEED]    = SDLK_0,
    [REMAP_FULLSCREEN]    = SDLK_f,
};

/* Current remapped keys (set from core options in libretro.cpp,
 * read by events.c). Defined as extern here, instantiated in libretro.cpp. */
extern int remap_keys[REMAP_COUNT];

#endif
