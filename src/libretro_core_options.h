#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>
#include <libretro.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C"
{
#endif

   struct retro_core_option_definition option_defs_us[] = {
         {"vnovel_frame_sync",
          "Frame Synchronization",
          "When enabled, every screen update is displayed exactly once for "
          "smooth animations and effects. Disable if you experience "
          "performance issues.",
          {
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
         {"vnovel_mode_indicators",
          "Mode Indicators",
          "Show on-screen badges for auto mode and skip mode "
          "(green and yellow dots in the top-right corner).",
          {
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
         {"vnovel_english_word_wrap",
          "English Word Wrap",
          "In English mode, never break a line in the middle of a word: when a "
          "word does not fit on the remaining line, move the whole word to the "
          "next line. Disable for the classic per-character wrapping.",
          {
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
         {"vnovel_fit_oversize_banners",
          "Fit Oversize Banners",
          "Some English-patch title cards were exported much wider than the "
          "game's screen (e.g. 1920px on a 640px screen), so the engine draws "
          "them oversized and crops the sides. The patch authors assumed the "
          "engine resized them; it does not. Enable to scale such a wide, "
          "short "
          "image down to fit the screen.",
          {
                {"disabled", NULL},
                {"enabled", NULL},
                {NULL, NULL},
          },
          "disabled"},
         {"vnovel_wrap_total_fit",
          "Total-Fit Wrapping",
          "Plan every line break in a passage at once, instead of choosing one "
          "word at a time. A passage runs to the next page break, hard newline, "
          "prompt or jump; a clickwait pause is inside it, so the wrap can see "
          "what follows a pause. Balances lines against each other and prefers "
          "word boundaries. Off keeps the classic greedy wrap.",
          {
                {"disabled", NULL},
                {"enabled", NULL},
                {NULL, NULL},
          },
          "disabled"},
         {"vnovel_textbox_opacity",
          "Textbox Backdrop Opacity",
          "Multiplier applied to the text-window backdrop image's alpha "
          "channel. "
          "100% = unchanged; raise it to make a backdrop cover the background "
          "more "
          "solidly, lower it to let more of the scene show through. Only "
          "affects "
          "windows whose backdrop is an image (a setwindow with a filename); "
          "colour backdrops (#rrggbb, the common case) are unaffected. "
          "100=normal, 200=double, 0=fully transparent.",
          {
                {"0", NULL},
                {"25", NULL},
                {"50", NULL},
                {"75", NULL},
                {"100", NULL},
                {"125", NULL},
                {"150", NULL},
                {"175", NULL},
                {"200", NULL},
                {NULL, NULL},
          },
          "100"},
         {"vnovel_debug_level",
          "Debug Level",
          "Log engine script parsing to RetroArch's log. 0=off, 3=verbose. "
          "Useful for reporting issues with specific visual novels.",
          {
                {"0", NULL},
                {"1", NULL},
                {"2", NULL},
                {"3", NULL},
                {NULL, NULL},
          },
          "0"},
         {"vnovel_software_cursor",
          "Software Cursor",
          "Draw a cursor directly on the screen. Enable for fullscreen "
          "where the host cursor is hidden. Disable for windowed mode.",
          {
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
         {"vnovel_pointer_enable",
          "Virtual Pointer",
          "When the frontend reports no real mouse/touch device, drive an "
          "on-screen pointer from a gamepad so pointer-only UIs (modal Yes/No "
          "dialogs) are reachable. Enable this only where the pad would "
          "otherwise be the only input.",
          {
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
         {"vnovel_pointer_source",
          "Virtual Pointer Stick",
          "Which analog stick moves the virtual pointer. The other stick stays "
          "free for the frontend. Click is always the matching stick button "
          "(L3/R3).",
          {
                {"right", NULL},
                {"left", NULL},
                {NULL, NULL},
          },
          "right"},
         {"vnovel_fastforward_speed",
          "Fast-Forward Speed",
          "Controls how fast the game advances during fast-forward. "
          "Higher values skip more time per frame, making text, "
          "animations and effects complete faster.",
          {
                {"2x", NULL},
                {"4x", NULL},
                {"8x", NULL},
                {"16x", NULL},
                {"32x", NULL},
                {"max", NULL},
                {NULL, NULL},
          },
          "8x"},
         {"vnovel_mouse_sensitivity",
          "Mouse Sensitivity",
          "Scales mouse movement for the in-game cursor. "
          "Adjust if the software cursor moves faster or slower "
          "than the real mouse.",
          {
                {"0.5", NULL},
                {"0.75", NULL},
                {"1.0", NULL},
                {"1.25", NULL},
                {"1.5", NULL},
                {"2.0", NULL},
                {NULL, NULL},
          },
          "1.0"},
         {"vnovel_key_lookback",
          "Lookback Key",
          "Key to open the text history / lookback mode.",
          {{"[", NULL}, {"h", NULL}, {"q", NULL}, {"b", NULL}, {NULL, NULL}},
          "["},
         {"vnovel_key_menu",
          "Menu Key",
          "Key to open the in-game right-click menu.",
          {{"backspace", NULL}, {"escape", NULL}, {"x", NULL}, {NULL, NULL}},
          "backspace"},
         {"vnovel_key_advance",
          "Advance Text Key",
          "Key to advance text / confirm.",
          {{"return", NULL}, {"space", NULL}, {"z", NULL}, {NULL, NULL}},
          "return"},
         {"vnovel_internal_resolution",
          "Internal Resolution",
          "Resolution the game engine is told the frontend expects. 'native' "
          "uses "
          "the host display's own size. A fixed size is reported to the engine "
          "as "
          "System.screenWidth/screenHeight so an engine/script that sizes "
          "itself "
          "from the screen renders natively at that resolution; the finished "
          "frame "
          "is then scaled by RetroArch like any other output. RetroArch's own "
          "scaling settings still apply.",
          {
                {"native", NULL},
                {"640x480", NULL},
                {"800x600", NULL},
                {"960x720", NULL},
                {"1024x768", NULL},
                {"1280x960", NULL},
                {"1600x1200", NULL},
                {NULL, NULL},
          },
          "native"},
         {"vnovel_text_speed",
          "Text Speed",
          "Override the engine's text display speed. 'default' uses the "
          "game's built-in speed. Slower speeds display characters one by "
          "one with a longer delay; faster speeds reduce the delay. "
          "Instant displays all text immediately.",
          {
                {"default", NULL},
                {"slow", NULL},
                {"normal", NULL},
                {"fast", NULL},
                {"instant", NULL},
                {NULL, NULL},
          },
          "default"},
         {"vnovel_ctrl_tap_frames",
          "Y Button (Ctrl Skip) Duration",
          "The Y button sends Ctrl, which both engines treat as hold-to-skip / "
          "fast-forward. Held Ctrl skips instantly and is hard to control, so "
          "Y is "
          "delivered as a bounded tap: press skips for this many frames, then "
          "Ctrl "
          "releases automatically. 0 = legacy behaviour (Ctrl held while Y is "
          "held).",
          {
                {"0", NULL},
                {"5", NULL},
                {"10", NULL},
                {"15", NULL},
                {"20", NULL},
                {"30", NULL},
                {"45", NULL},
                {"60", NULL},
                {NULL, NULL},
          },
          "20"},
         {"vnovel_kirikiri_compat",
          "KiriKiri Engine Era (data.xp3)",
          "Which KiriKiri behaviour the engine imitates, for games built against "
          "an older engine, including how the engine composites its layers. "
          "'auto' lets the engine decide: it looks the title up by its data.xp3, "
          "in a table of per-game eras, and falls back to this build's defaults "
          "for a title it does not know. Set '2.19' or 'modern' explicitly to "
          "override the table for this session. The two eras composite by "
          "different algorithms, so this is not a cosmetic preference. Only "
          "affects the KiriKiri path.",
          {
                {"auto", NULL},
                {"2.19", NULL},
                {"modern", NULL},
                {NULL, NULL},
          },
          "auto"},
         {"vnovel_kirikiri_debug",
          "KiriKiri Diagnostic Probes",
          "Extra diagnostic output from the KiriKiri engine, for investigating a "
          "rendering or input problem. 'none' is silent. The other entries are "
          "probes: 'province' outlines each province (hit-test) rectangle on "
          "screen and logs it, which is the way to see where a clickable region "
          "actually is; 'fill' logs each fill with its rectangle, colour and "
          "face; 'script' traces every scenario tag; 'input' logs per-poll input "
          "state; 'draw' traces the compositing branch each draw takes. 'all' "
          "enables every probe. Probes only observe; none of them changes what "
          "the engine draws. Only affects the KiriKiri path.",
          {
                {"none", NULL},
                {"all", NULL},
                {"province", NULL},
                {"fill", NULL},
                {"script", NULL},
                {"input", NULL},
                {"draw", NULL},
                {"province,fill", NULL},
                {"province,fill,draw", NULL},
                {NULL, NULL},
          },
          "none"},
         {"vnovel_split_view",
          "Split View (experimental)",
          "Development aid for the text-plane work: compose the frame from "
          "artwork only, then present it beside the captured text as one wide "
          "image - graphics on the left, text on the right, each at the game's "
          "own size (so a 640x480 game is presented as 1280x480). 'off' "
          "presents the game normally. It is not a normal way to play: it "
          "exists to show that the two are captured separately and can be "
          "served at a different resolution. Only affects the KiriKiri path.",
          {
                {"off", NULL},
                {"enabled", NULL},
                {NULL, NULL},
          },
          "off"},
         {"vnovel_text_debug_view",
          "Text Debug View",
          "Development aid for the text-plane capture. 'off' presents the frame "
          "normally. 'pip' overlays a translucent picture-in-picture in the "
          "bottom-right showing the text the core has captured and accumulated "
          "into a page - the dialogue alone, without the artwork it was peeled "
          "out of. Use it to check what was captured. It changes nothing about "
          "capture or drawing.",
          {
                {"off", NULL},
                {"pip", NULL},
                {NULL, NULL},
          },
          "off"},
         {"vnovel_menu_hotkey",
          "Menu Hotkey",
          "Which key toggles the core's own menu bar. Bound to F12 by default. "
          "The bar is where a game's customized menu (resetmenu/insertmenu) "
          "appears, and it is also reachable by clicking the bottom-right corner "
          "of the frame, so it is never unreachable without a key. This acts "
          "independently of a game that shows a menu of its own, and it always "
          "works, so it is the way to reach the menu when a title's own request "
          "is unavailable.",
          {
                {"none", NULL},
                {"F12", NULL},
                {"F11", NULL},
                {"F10", NULL},
                {"Tab", NULL},
                {"Backquote", NULL},
                {NULL, NULL},
          },
          "F12"},
         {NULL, NULL, NULL, {{0}}, NULL},
   };

   /* No translations for now */
   struct retro_core_option_definition *option_defs_intl[RETRO_LANGUAGE_LAST] =
         {
               option_defs_us,
   };

   static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
                                                bool *frame_sync,
                                                bool *indicators,
                                                int *debug_level)
   {
      unsigned version = 0;

      if (!environ_cb)
         return;

      if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) &&
          (version >= 1))
      {
         struct retro_core_options_intl core_options_intl;
         core_options_intl.us    = option_defs_us;
         core_options_intl.local = NULL;
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL,
                    &core_options_intl);
      }
      else
      {
         /* Fallback for older frontends: build retro_variable array */
         size_t i, num_options = 0;
         struct retro_variable *variables = NULL;
         char **values_buf                = NULL;

         for (i = 0; option_defs_us[i].key; i++)
            num_options++;

         variables = (struct retro_variable *)calloc(
               num_options + 1, sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));
         if (!variables || !values_buf)
            goto error;

         for (i = 0; i < num_options; i++)
         {
            const char *key           = option_defs_us[i].key;
            const char *desc          = option_defs_us[i].desc;
            const char *default_value = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len = 3, default_index = 0, num_values = 0, j;

            for (j = 0; values[j].value; j++)
            {
               if (default_value && strcmp(values[j].value, default_value) == 0)
                  default_index = j;
               buf_len += strlen(values[j].value);
               num_values++;
            }

            if (num_values > 0 && desc)
            {
               buf_len += num_values - 1 + strlen(desc);
               values_buf[i] = (char *)calloc(buf_len, 1);
               if (!values_buf[i])
                  goto error;
               sprintf(values_buf[i],
                       "%s; %s",
                       desc,
                       values[default_index].value);
               for (j = 0; j < num_values; j++)
                  if (j != default_index)
                  {
                     strcat(values_buf[i], "|");
                     strcat(values_buf[i], values[j].value);
                  }
            }

            variables[i].key   = key;
            variables[i].value = values_buf[i];
         }

         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

      error:
         if (values_buf)
         {
            for (i = 0; i < num_options; i++)
               free(values_buf[i]);
            free(values_buf);
         }
         free(variables);
      }
   }

#ifdef __cplusplus
}
#endif

#endif
