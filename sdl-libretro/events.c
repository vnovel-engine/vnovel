#include <SDL_libretro.h>
#include <sdl/src/events/SDL_events_c.h>
#include <sdl/src/video/dummy/SDL_nullevents_c.h>
#include <stdlib.h>
#include "../../key_remap.h"
/* Which RetroPad button generates a right-click (-1 = none) */
int SDL_libretro_rightclick_button = RIGHTCLICK_NONE;
int SDL_libretro_leftclick_button  = RIGHTCLICK_NONE;

/* ---- Runtime-tunable input knobs ----
 * These used to be function-local statics initialised from the environment on
 * first use. They live at module scope instead so the frontend (or a test) can
 * flip them at runtime through the setters below, and so the environment is
 * consulted once, not on every poll. -1 = "not yet resolved". */
static int ons_input_debug    = -1;
static int ons_ctrl_tap_frames = -1;  /* configured tap length, frames */
static int ons_ctrl_tap_left   = 0;   /* live countdown for the current tap */

void SDL_libretro_set_input_debug(int on) { ons_input_debug = on ? 1 : 0; }

/* Set the Y→ctrl tap length directly. Also re-arms the countdown so a change
 * takes effect on the next press rather than mid-tap. */
void SDL_libretro_set_ctrl_tap_frames(int frames)
{
  ons_ctrl_tap_frames = frames < 0 ? 0 : frames;
  ons_ctrl_tap_left = 0;
}

/* Semantic mouse-button codes. These travel through the `pressed` state latch
 * into to_button(); they are deliberately distinct from the pointer-device
 * *index* used to probe RETRO_DEVICE_ID_POINTER_PRESSED. */
#define MOUSE_BTN_NONE  0
#define MOUSE_BTN_LEFT  1
#define MOUSE_BTN_RIGHT 2

/* RetroPad button labels, for debug output only. */
static const char *const pad_name[16] = {
  "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",
  "A", "X", "L", "R", "L2", "R2", "L3", "R3"
};

/* Input diagnostics live in one place so call sites stay clean and the
 * environment is read at most once. Enable with ONS_INPUT_DEBUG=1 or
 * SDL_libretro_set_input_debug(1). */
static int input_debug_enabled(void)
{
  if (ons_input_debug < 0) {
    const char *e = getenv("ONS_INPUT_DEBUG");
    ons_input_debug = e && *e && *e != '0';
  }
  return ons_input_debug;
}

static void input_debug_kbd(int in_sym, int engine_sym, int down)
{
  if (!input_debug_enabled()) return;
  fprintf(stderr, "IPROBE kbd sym=%d -> engine SDLK=%d %s\n",
          in_sym, engine_sym, down ? "DOWN" : "UP");
}

/* `tap_left` >= 0 prints the remaining tap length; pass -1 for plain edges. */
static void input_debug_pad(const char *what, int idx, int engine_sym, int tap_left)
{
  if (!input_debug_enabled()) return;
  if (tap_left >= 0)
    fprintf(stderr, "IPROBE pad %s (id=%d) -> SDLK=%d %s (%d frames)\n",
            pad_name[idx], idx, engine_sym, what, tap_left);
  else
    fprintf(stderr, "IPROBE pad %s (id=%d) -> SDLK=%d %s\n",
            pad_name[idx], idx, engine_sym, what);
}

/* Resolve the Y→ctrl tap length once: ONS_CTRL_TAP_FRAMES overrides the core
 * option onscripter_en_ctrl_tap_frames (default 20, 0 = legacy held ctrl). */
static int ctrl_tap_frames(void)
{
  if (ons_ctrl_tap_frames < 0) {
    const char *e = getenv("ONS_CTRL_TAP_FRAMES");
    if (e) {
      ons_ctrl_tap_frames = atoi(e);
    } else {
      ons_ctrl_tap_frames = 20;
      extern retro_environment_t environ_cb_global;
      struct retro_variable v = {0};
      v.key = "onscripter_en_ctrl_tap_frames";
      if (environ_cb_global && environ_cb_global(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value)
        ons_ctrl_tap_frames = atoi(v.value);
    }
    if (ons_ctrl_tap_frames < 0) ons_ctrl_tap_frames = 0;
  }
  return ons_ctrl_tap_frames;
}

/* ---- Test auto-advance ----
 * Set ONS_TEST_AUTODVANCE=<frames> to inject a RETURN keypress every
 * N frames. This lets automated tests advance through text without a
 * human pressing buttons. Zero or unset = disabled.
 */
static int test_autoadvance_frames = -1;
static int test_autoadvance_counter = 0;

static int get_test_autoadvance(void)
{
  if (test_autoadvance_frames < 0) {
    const char *env = getenv("ONS_TEST_AUTODVANCE");
    test_autoadvance_frames = env ? atoi(env) : 0;
  }
  return test_autoadvance_frames;
}

static void maybe_inject_autoclick(void)
{
  int n = get_test_autoadvance();
  if (n <= 0) return;
  fprintf(stderr, "AA: counter=%d/%d\n", test_autoadvance_counter, n);
  if (++test_autoadvance_counter >= n) {
    test_autoadvance_counter = 0;
    fprintf(stderr, "AA: INJECTING SPACE\n");
    static SDL_keysym sp_sym = { SDLK_SPACE, SDLK_SPACE, 0 };
    SDL_PrivateKeyboard(SDL_PRESSED, &sp_sym);
    SDL_PrivateKeyboard(SDL_RELEASED, &sp_sym);
  }
}

/* RetroPad → ONScripter key mapping. There is no ONScripter-side way to set
 * the pad map (the game owns its key semantics), so this is chosen to match
 * the same portability layer used by the KiriKiri engine where the games
 * share key semantics ('s'=skip toggle, 'a'=auto toggle, 'o'=one-page, 'v'=
 * volume editor, Return=advance, Escape=menu, PageUp/Down=history scroll).
 * Directional d-pad is REMAPPED by the ONScripter engine into cursor keys, so
 * we pass UP/DOWN/LEFT/RIGHT straight through.
 *
 * Canonical semantics (kept identical across engines where games allow):
 *   Start->auto  Select->skip  A->advance/confirm  B->cancel/menu
 *   X->hide graphics  Y->quicksave  L/R->scroll  R3->pointer click
 * Title-specific keys ('s','a','o','v') are the engines' portable set; pad
 * buttons are wired to those. */
ons_key_t SDL_libretro_pad_mapping[16] = {
  [RETRO_DEVICE_ID_JOYPAD_B]      = ONS_KEY_SPACE,   /* B  cancel/back */
  [RETRO_DEVICE_ID_JOYPAD_Y]      = ONS_KEY_RCTRL,   /* Y  quicksave (ONS RCtrl = save) */
  [RETRO_DEVICE_ID_JOYPAD_SELECT] = ONS_KEY_s,       /* Select  skip toggle ('s') */
  [RETRO_DEVICE_ID_JOYPAD_START]  = ONS_KEY_a,       /* Start   auto toggle ('a') */
  [RETRO_DEVICE_ID_JOYPAD_UP]     = ONS_KEY_UP,
  [RETRO_DEVICE_ID_JOYPAD_DOWN]   = ONS_KEY_DOWN,
  [RETRO_DEVICE_ID_JOYPAD_LEFT]   = ONS_KEY_LEFT,
  [RETRO_DEVICE_ID_JOYPAD_RIGHT]  = ONS_KEY_RIGHT,
  [RETRO_DEVICE_ID_JOYPAD_A]      = ONS_KEY_RETURN,  /* A  advance/confirm */
  [RETRO_DEVICE_ID_JOYPAD_X]      = ONS_KEY_ESCAPE,  /* X  cancel/menu (hide) */
  [RETRO_DEVICE_ID_JOYPAD_L]      = ONS_KEY_PAGEUP,  /* L  scroll up   (history) */
  [RETRO_DEVICE_ID_JOYPAD_R]      = ONS_KEY_PAGEDOWN,/* R  scroll down (history) */
  [RETRO_DEVICE_ID_JOYPAD_L2]     = ONS_KEY_PAGEUP,
  [RETRO_DEVICE_ID_JOYPAD_R2]     = ONS_KEY_PAGEDOWN,
  [RETRO_DEVICE_ID_JOYPAD_L3]     = ONS_KEY_NONE,
  [RETRO_DEVICE_ID_JOYPAD_R3]     = ONS_KEY_NONE,    /* R3 = pointer click, handled in the core */
};

int SDL_libretro_key_from_ons_key(ons_key_t k)
{
  switch (k) {
    case ONS_KEY_SPACE:     return SDLK_SPACE;
    case ONS_KEY_RETURN:    return SDLK_RETURN;
    case ONS_KEY_ESCAPE:    return SDLK_ESCAPE;
    case ONS_KEY_RCTRL:     return SDLK_RCTRL;
    case ONS_KEY_a:         return SDLK_a;
    case ONS_KEY_o:         return SDLK_o;
    case ONS_KEY_s:         return SDLK_s;
    case ONS_KEY_q:         return SDLK_q;
    case ONS_KEY_0:         return SDLK_0;
    case ONS_KEY_TAB:      return SDLK_TAB;
    case ONS_KEY_PAGEUP:    return SDLK_PAGEUP;
    case ONS_KEY_PAGEDOWN:  return SDLK_PAGEDOWN;
    case ONS_KEY_UP:        return SDLK_UP;
    case ONS_KEY_DOWN:      return SDLK_DOWN;
    case ONS_KEY_LEFT:      return SDLK_LEFT;
    case ONS_KEY_RIGHT:     return SDLK_RIGHT;
    default:                return 0;
  }
}

/* Y emits ctrl (the engines' fast-forward modifier). Held ctrl is an instant,
 * unmanageable skip, so Y is delivered as a bounded ctrl TAP: press skips for
 * tap_len frames, then ctrl is released automatically even if Y is still held.
 * A fresh press restarts the tap. Returns true when `key` is the tap button and
 * the press has been handled (the caller then skips normal edge handling). */
static bool handle_ctrl_tap(int i, int16_t state, int key, int tap_len,
                            int16_t *buttons, SDL_keysym *sym)
{
  if (!(tap_len > 0 && key == SDLK_RCTRL)) return false;
  if (state && !buttons[i]) {
    /* Rising edge: (re)start the tap. */
    buttons[i] = 1;
    ons_ctrl_tap_left = tap_len;
    input_debug_pad("TAP DOWN", i, key, ons_ctrl_tap_left);
    sym->scancode = key; sym->sym = key;
    SDL_PrivateKeyboard(SDL_PRESSED, sym);
  } else if (ons_ctrl_tap_left > 0) {
    /* Tap running: count down; on the last frame, release ctrl. This else
     * branch is reached whenever the tap has not expired yet — including while
     * Y stays held — and is skipped once the countdown hits zero. */
    if (--ons_ctrl_tap_left == 0) {
      input_debug_pad("TAP UP", i, key, -1);
      sym->scancode = key; sym->sym = key;
      SDL_PrivateKeyboard(SDL_RELEASED, sym);
    }
  }
  /* Held past the tap: ignore until Y is released (button re-arms below). */
  if (!state) buttons[i] = 0;
  return true;
}

static void PumpKeyboardEvents(void)
{
  static int16_t keys[RETROK_LAST] = {0};
  static SDL_keysym sym;
  for (int i = 0; i < RETROK_LAST; ++i) {
    int16_t state = SDL_libretro_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, i);
    if (keys[i] != state) {
      keys[i] = state;
      int sdl_key = i;
      /* Remap: if this key is a remapped function key, translate it
       * to the engine's expected key. */
      for (int r = 0; r < REMAP_COUNT; r++) {
        if (i == remap_keys[r] && remap_keys[r] != engine_keys[r]) {
          sdl_key = engine_keys[r];
          break;
        }
      }
      input_debug_kbd(i, sdl_key, state);
      /* Don't forward the fullscreen key to the engine — it changes
       * the SDL video mode resolution, conflicting with RetroArch's
       * own fullscreen. RetroArch handles fullscreen natively. */
      if (sdl_key == engine_keys[REMAP_FULLSCREEN])
        continue;
      SDL_PrivateKeyboard(state ? SDL_PRESSED : SDL_RELEASED, &sym);
    }
  }

  static int16_t buttons[16] = {0};
  int tap_len = ctrl_tap_frames();
  for (int i = 0; i < 16; ++i) {
    int16_t state = SDL_libretro_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i);
    int k = SDL_libretro_key_from_ons_key(SDL_libretro_pad_mapping[i]);
    if (!k) continue;
    if (handle_ctrl_tap(i, state, k, tap_len, buttons, &sym))
      continue;
    if (buttons[i] != state) {
      buttons[i] = state;
      input_debug_pad(state ? "DOWN" : "UP", i, k, -1);
      sym.scancode = k;
      sym.sym = k;
      SDL_PrivateKeyboard(state ? SDL_PRESSED : SDL_RELEASED, &sym);
    }
  }
}

static Uint8 to_button(int16_t pressed)
{
  if (pressed == MOUSE_BTN_LEFT)
    return SDL_BUTTON_LEFT;
  if (pressed == MOUSE_BTN_RIGHT)
    return SDL_BUTTON_RIGHT;
  return 0;
}

static Sint16 mouse_x = -1;
static Sint16 mouse_y = -1;

void SDL_libretro_get_mouse_pos(int *x, int *y)
{
  if (x) *x = mouse_x;
  if (y) *y = mouse_y;
}

static void PumpMouseEvents(void)
{
  static Uint8 btn = 0;
  static int16_t pressed = 0;
  static bool have_mouse = false;

  SDL_Surface *screen = SDL_GetVideoSurface();
  if (!screen) return;

  extern float opt_mouse_sensitivity;
  int16_t mx = SDL_libretro_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
  int16_t my = SDL_libretro_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

  /* Absolute pointer (lightgun / touchscreen / pointer device): RetroArch
   * reports X/Y as [-32768..32767] over the whole screen. Map that onto the
   * engine's *current* surface size — which the game or the internal-resolution
   * option decides — instead of assuming 640x480. */
  int16_t ax = SDL_libretro_input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
  int16_t ay = SDL_libretro_input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
  if (ax != 0 || ay != 0) {
    mouse_x = (Sint16)((int)(ax + 32768) * screen->w / 65536);
    mouse_y = (Sint16)((int)(ay + 32768) * screen->h / 65536);
    if (mouse_x < 0) mouse_x = 0; else if (mouse_x >= screen->w) mouse_x = screen->w - 1;
    if (mouse_y < 0) mouse_y = 0; else if (mouse_y >= screen->h) mouse_y = screen->h - 1;
    have_mouse = true;
  }
  if (mx != 0 || my != 0) {
    mouse_x += (int16_t)(mx * opt_mouse_sensitivity);
    mouse_y += (int16_t)(my * opt_mouse_sensitivity);
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen->w) mouse_x = screen->w - 1;
    if (mouse_y >= screen->h) mouse_y = screen->h - 1;
    have_mouse = true;
  }

  /* Pointer sources currently pressed (touchscreen/lightgun). Counted — this
   * is a device *index*, consumed immediately — then the click source is
   * chosen from mouse/joypad buttons. Keep the two concepts separate: an
   * index walks RETRO_DEVICE_ID_POINTER_PRESSED; a button code is what
   * to_button() turns into SDL_BUTTON_*. */
  int16_t pointer_pressed = 0;
  while (SDL_libretro_input_state_cb(0, RETRO_DEVICE_POINTER, pointer_pressed, RETRO_DEVICE_ID_POINTER_PRESSED)) {
    pointer_pressed += 1;
  }

  int16_t click = pointer_pressed ? MOUSE_BTN_LEFT : MOUSE_BTN_NONE;
  if (SDL_libretro_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)) {
    click = MOUSE_BTN_LEFT;
  }
  if (SDL_libretro_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)) {
    click = MOUSE_BTN_RIGHT;
  }
  // RetroPad button mapped to right-click (cancel/back / rmenu)
  if (SDL_libretro_rightclick_button >= 0 &&
      SDL_libretro_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, SDL_libretro_rightclick_button)) {
    click = MOUSE_BTN_RIGHT;
  }
  // RetroPad button mapped to left-click (primary act: confirm/advance)
  if (SDL_libretro_leftclick_button >= 0 &&
      SDL_libretro_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, SDL_libretro_leftclick_button)) {
    click = MOUSE_BTN_LEFT;
  }

  if (pressed) {
    if (!click) {
      btn = to_button(pressed);
      SDL_PrivateMouseButton(SDL_RELEASED, btn, mouse_x, mouse_y);
      pressed = 0;
    }
  } else {
    if (click) {
      btn = to_button(click);
      SDL_PrivateMouseButton(SDL_PRESSED, btn, mouse_x, mouse_y);
      pressed = click;
    }
  }
}

void DUMMY_PumpEvents(_THIS)
{
  /* Push a motion event only when position changed since last push.
   * This avoids flooding the event queue with duplicate motion
   * events that cause the engine to spin at 100% CPU. */
  static Sint16 last_pushed_x = -1, last_pushed_y = -1;
  if (mouse_x != last_pushed_x || mouse_y != last_pushed_y) {
    SDL_PrivateMouseMotion(0, 0, mouse_x, mouse_y);
    last_pushed_x = mouse_x;
    last_pushed_y = mouse_y;
  }
}
void SDL_libretro_PumpEvents(void)
{
  PumpKeyboardEvents();
  PumpMouseEvents();
  maybe_inject_autoclick();
}

void DUMMY_InitOSKeymap(_THIS)
{
}
