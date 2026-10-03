# Code Review

_Started: 2026-10-03_

---

## `engine_kirikiri_cabi.cpp`

#### Line 427 (outdated)

isn't there a set of portable defines for that? i don't like guessing constants. moreover, X, L, R, L2 ... all share 0? really?

```cpp context
424:          * controller natively (KRKRSDL2_LIBRETRO), so the only way pad arrows
425:          * reach KAG menu navigation is this map. SDLK_UP/DOWN/LEFT/RIGHT are
426:          * 0x40000000|scancode (SDL_SCANCODE_TO_KEYCODE). */
427:         static const int KEY_SYM[16] = {
428:             /* B */ 27, /* Y */ 's', /* SELECT */ 'f', /* START */ 'a',
429:             /* UP */ 0x40000052, /* DOWN */ 0x40000051,
430:             /* LEFT */ 0x40000050, /* RIGHT */ 0x4000004F,
```

#### Line 440 (outdated)

*really* use defined constants. this is brittle and non-symbolic, it *will* break

```cpp context
437:         for (int i = 0; i < 16; i++) {
438:             int st = krkr_input_state(0, KRKR_RETRO_DEVICE_JOYPAD, 0, (unsigned)i);
439:             /* X = hide graphics: KAG does this on right-click. */
440:             if (i == 9 /*X*/) {
441:                 if (st != prev[i]) {
442:                     char ev[64]; memset(ev, 0, sizeof(ev));
443:                     *(unsigned int*)(ev+0) = st ? KRKR_SDL_MOUSEBUTTONDOWN : KRKR_SDL_MOUSEBUTTONUP;
```

---

## `libretro.cpp`

#### Line 28 (outdated)

it's good to have a crash handler. we'll keep it. however, the paths are not portable so let's make them portable. not everyone uses Linux.

```cpp context
25:   if (reentry) { signal(sig, SIG_DFL); raise(sig); return; }
26:   reentry = 1;
27: 
28:   int fd = open("/tmp/ons_crash.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
29:   if (fd >= 0) {
30:     char buf[256];
31:     struct timespec ts;
```

#### Line 138 (outdated)

lines 133 to 138 ->
we agree, i suppose, that this is actually remappable and that retroarch is gonna send us the proper axis/key that we set that corresponds to this *default assigmnent*.

For example, if i want to use the left analog stick for the virtual pointer, i should be able to do so.

if it's not the case, then we need to implement it so that we're not stuck with a stick that may or may not exist. also, virtual pointer support should be disable-able in the core options IMO

When we say retro device pointer, i suppose a mouse emulation with a joystick is actually something that retroarch considers. can we check the state of the art?

```cpp context
135:     int16_t ry = frontend_input_state_cb(0, RETRO_DEVICE_ANALOG,
136:                        RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
137:     int16_t r3 = frontend_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
138:                        RETRO_DEVICE_ID_JOYPAD_R3);
139:     int moved = 0;
140:     if (rx > 4000 || rx < -4000) { vlx_pos = (vlx_pos < 0 ? 0 : vlx_pos) + rx / 4096; moved = 1; }
141:     if (ry > 4000 || ry < -4000) { vly_pos = (vly_pos < 0 ? 0 : vly_pos) + ry / 4096; moved = 1; }
```

#### Line 495 (outdated)

so this is the input state handler for everything. i suppose it's misnamed if it's called pointer_synth_input_state and it handles everything. we probably need a composed function that calls this pointer_synth_input_state (if relevant) and the other input management callbacks in a good way. that way, our input handling is centralized but we can keep a well-organized structure.

```cpp context
492:    * engine (ONScripter drives it through the SDL1.2 overlay; KiriKiri through
493:    * krkr_set_input_callbacks). Installed here because it needs the game's
494:    * surface size to clamp the virtual pointer. */
495:   SDL_libretro_input_state_cb = pointer_synth_input_state;
496: 
497:   char archive_path[PATH_MAX_LENGTH];
498:   fill_pathname_basedir(archive_path, game->path, sizeof(archive_path));
```

#### Line 558 (outdated)

the comment is poorly worded. it should go something like: "Remember the last archive that was run to be able to perform comparisons later i.e. detect a restart vs. content change for example"

```cpp context
555:   vtable->setArchivePath(engine, archive_path);
556:   /* Remember it: retro_reset() recreates the engine and re-applies this.
557:    * (The KiriKiri branch above sets it too, but returns early.) */
558:   strncpy(last_archive_path, archive_path, sizeof(last_archive_path) - 1);
559:   last_archive_path[sizeof(last_archive_path) - 1] = '\0';
560: 
561:   // Per-game save directory (see above). Fall back to the game dir if
```

#### Line 712 (outdated)

formatting, please. this scoping block deserves proper indentation i suppose. it IS going to confuse people otherwise.

```cpp context
709:    * SDL_systimer.c), which freezes text/animation exactly like a pause. */
710:   { struct timespec hb; clock_gettime(CLOCK_MONOTONIC, &hb);
711:     SDL_libretro_frontend_hb =
712:         (unsigned long long)hb.tv_sec * 1000 + hb.tv_nsec / 1000000; }
713: 
714:   // KiriKiri engine path: read from SDL_libretro_surface
715:   // Use void* to avoid SDL 1.2/SDL2 type conflict
```

#### Line 800 (outdated)

i suppose we could deal with a square 8192x8192 rendering target. for consistency, let's accept it. negative or gigantic numbers constitute the VAST majority of our potential garbage dimensions so it's fair to overshoot a bit.

```cpp context
797:        * 1920x240); giant numbers on both axes, and especially a vertical
798:        * resolution beyond any real mode, mean the surface is bogus. Drop the
799:        * frame rather than trust it. */
800:       if (w <= 0 || h <= 0 || w > 8192 || h > 2048 ||
801:           pitch < w * 2 || pitch > w * 8) {
802:         static int last_dbg = 0;
803:         if (last_dbg < 8) {
```

#### Line 870 (outdated)

this pointer as a whole is nice but could be nicer. the stem going from the arrow could be slightly offset so it resembles an old windows arrow.

```cpp context
867:               "#.#         ",
868:               "#..#        ",
869:               "#...#       ",
870:               "#....#      ",
871:               "#.....#     ",
872:               "#......#    ",
873:               "#.......#   ",
```

#### Line 885 (outdated)

i believe this pointer generation could be done in a preprocessor macro and the pointer could be saved as an xpm or something and loaded cleanly. it's not great to regenerate at each loop.

good bug catch otherwise.

```cpp context
882:               " #..#       ",
883:               "  ##        ",
884:             };
885:             for (int dy = 0; dy < 18 && cx >= 0 && cy >= 0; dy++) {
886:               for (int dx = 0; dx < 12; dx++) {
887:                 char c = arrow[dy][dx];
888:                 if (c == ' ') continue;
```

#### Line 1024 (outdated)

there's clearly a ratio between ff_mult and ff_speed. except for the first value, ff_mult and ff_speed double with each other. maybe we could align those aesthetically with couples like
(2, 12)
(4, 24)
(8, 48)
(16, 96)
(32, 192)
(64, 384)

to reach a more perfect form.

```cpp context
1021:     else if (strcmp(var.value, "8x") == 0)  { SDL_libretro_ff_mult = 8;  SDL_libretro_ff_speed = 50; }
1022:     else if (strcmp(var.value, "16x") == 0) { SDL_libretro_ff_mult = 16; SDL_libretro_ff_speed = 100; }
1023:     else if (strcmp(var.value, "32x") == 0) { SDL_libretro_ff_mult = 32; SDL_libretro_ff_speed = 200; }
1024:     else if (strcmp(var.value, "max") == 0) { SDL_libretro_ff_mult = 64; SDL_libretro_ff_speed = 400; }
1025:   }
1026: 
1027:   var.key = "onscripter-en_mouse_sensitivity";
```

#### Line 1357

yes, thanks.

```cpp context
1354:       int cx, cy;
1355:       SDL_libretro_get_mouse_pos(&cx, &cy);
1356:       /* Off-screen (negative) until real pointer input: draw nothing then. */
1357:       for (int dy = 0; dy < 12 && cx >= 0 && cy >= 0; dy++) {
1358:         for (int dx = 0; dx < 12; dx++) {
1359:           int sx = cx + dx, sy = cy + dy;
1360:           if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
```

---

## `sdl-libretro/video.c`

#### Line 175 (outdated)

this name might be a bit cryptic. it's okay, just document it in an ulterior documentation commit.

```c context
172: SDL_sem *SDL_libretro_wait_sem = NULL;
173: SDL_sem *SDL_libretro_rendered_sem = NULL;
174: bool SDL_libretro_paused = false;
175: volatile unsigned long long SDL_libretro_frontend_hb = 0;
176: bool SDL_libretro_fastforward = false;
177: int SDL_libretro_ff_speed = 50;
178: Uint32 SDL_libretro_ff_offset = 0;
```
