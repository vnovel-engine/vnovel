#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>
#include <libretro.h>
#include <retro_inline.h>

/* `vnovel_font_face`'s value list is built AT RUNTIME (see its definition
 * below and build_font_face_options in libretro.cpp): the core lists the face
 * directory when it starts and offers every face it finds, so a user's own
 * .ttf/.otf becomes a choice without recompiling. The definition below carries
 * no values of its own — libretro.cpp fills them at registration, before
 * RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL is called. */

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
                {"enabled", NULL},
                {"disabled", NULL},
                {NULL, NULL},
          },
          "enabled"},
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
         {"vnovel_font_face",
          "Font",
          "Which typeface to draw dialogue with. 'game' uses the game's own "
          "font (or the engine's fallback when the game has none) and changes "
          "nothing. 'OpenDyslexic' weights the bottoms of letters to stop them "
          "swapping places, for readers with dyslexia. 'Atkinson Hyperlegible' "
          "is from the Braille Institute: its letters are made distinct from "
          "each other rather than weighted, which is what helps low vision. "
          "'Comic Neue' is a comic-book face. 'EB Garamond' is a reading "
          "serif, comfortable over long passages. 'BIZ UDPGothic' is a "
          "Japanese universal-design face and the only bundled choice with "
          "Japanese glyphs; Japanese text ignores the Latin-only faces and "
          "always falls back, so pick this one for a Japanese game. All "
          "bundled faces are under the SIL Open Font License. Any other "
          ".ttf/.otf dropped into the core's system directory (bios/vnovel, "
          "beside overrides.txt) is offered here too, under its filename. "
          "TAKES EFFECT AFTER A RESTART OF THE GAME: the engine opens its "
          "typeface once, when the game starts, so changing this mid-game "
          "does not redraw anything already on screen — close the game and "
          "load it again to see the new face. (Font Size, Font Letter "
          "Spacing, Font Word Spacing and Font Tint apply without a restart.)",
          /* Filled at runtime by build_font_face_options(): the bundled faces
           * plus whatever the face directory holds. See this header's top. */
          {{0, 0}},
          "game"},
         {"vnovel_font_size",
          "Font Size",
          "Scale the game's own font size, for text that is too small to read "
          "comfortably. Applied to the size the game's script asked for, so the "
          "text box, line spacing and line wrapping all grow with the text "
          "instead of the text outgrowing them. 100% leaves the game's size "
          "alone.",
          {
                {"50", NULL}, {"55", NULL}, {"60", NULL}, {"65", NULL}, {"70", NULL}, {"75", NULL},
                {"80", NULL}, {"85", NULL}, {"90", NULL}, {"95", NULL}, {"100", NULL}, {"105", NULL},
                {"110", NULL}, {"115", NULL}, {"120", NULL}, {"125", NULL}, {"130", NULL}, {"135", NULL},
                {"140", NULL}, {"145", NULL}, {"150", NULL}, {"155", NULL}, {"160", NULL}, {"165", NULL},
                {"170", NULL}, {"175", NULL}, {"180", NULL}, {"185", NULL}, {"190", NULL}, {"195", NULL},
                {"200", NULL}, {"205", NULL}, {"210", NULL}, {"215", NULL}, {"220", NULL}, {"225", NULL},
                {"230", NULL}, {"235", NULL}, {"240", NULL}, {"245", NULL}, {"250", NULL}, {"255", NULL},
                {"260", NULL}, {"265", NULL}, {"270", NULL}, {"275", NULL}, {"280", NULL}, {"285", NULL},
                {"290", NULL}, {"295", NULL}, {"300", NULL},
          },
          "100"},
         {"vnovel_font_tint",
          "Font Tint (Contrast)",
          "Adjust the colour of dialogue text to make it stand out against "
          "whatever is behind it. A percentage per channel: 'white' is the "
          "game's own colour unchanged, 'brighter' and 'pure white' push a "
          "dim or tinted text colour up toward full white, 'dimmer' pulls it "
          "down toward the background. Use this when the game's text colour is "
          "low-contrast against its text box.",
          {
                {"dimmer", NULL},
                {"normal", NULL},
                {"brighter", NULL},
                {"pure white", NULL},
                {NULL, NULL},
          },
          "normal"},
         {"vnovel_font_tint_custom",
          "Font Tint - Custom RGB",
          "Set the text tint as three comma-separated percentages (R,G,B), "
          "e.g. '100,100,80' to warm the text slightly. Only used when Font "
          "Tint (Contrast) is set to 'normal'; the other settings override "
          "this. Each value is 0-200, where 100 leaves that channel alone.",
          {
                {"off", NULL},
                {"50,50,50", NULL},
                {"80,80,80", NULL},
                {"100,100,100", NULL},
                {"100,100,80", NULL},
                {"120,120,120", NULL},
                {"150,150,150", NULL},
                {NULL, NULL},
          },
          "off"},
         {"vnovel_font_letter_spacing",
          "Font Letter Spacing",
          "Add space between individual characters, as a fraction of the font's "
          "own size, to open up dense or cramped text. Negative values tighten "
          "the letters instead, which can help when a face is too loose. This "
          "also affects where lines wrap, exactly as the wider letters do, so "
          "text is never cut off. Applies to ONScripter and PONScripter text; "
          "KiriKiri lays its text out itself and ignores it.",
          {
                {"-1.00", NULL}, {"-0.90", NULL}, {"-0.80", NULL}, {"-0.70", NULL}, {"-0.60", NULL}, {"-0.50", NULL},
                {"-0.40", NULL}, {"-0.30", NULL}, {"-0.25", NULL}, {"-0.20", NULL}, {"-0.19", NULL}, {"-0.18", NULL},
                {"-0.17", NULL}, {"-0.16", NULL}, {"-0.15", NULL}, {"-0.14", NULL}, {"-0.13", NULL}, {"-0.12", NULL},
                {"-0.11", NULL}, {"-0.10", NULL}, {"-0.09", NULL}, {"-0.08", NULL}, {"-0.07", NULL}, {"-0.06", NULL},
                {"-0.05", NULL}, {"-0.04", NULL}, {"-0.03", NULL}, {"-0.02", NULL}, {"-0.01", NULL}, {"0.00", NULL},
                {"0.01", NULL}, {"0.02", NULL}, {"0.03", NULL}, {"0.04", NULL}, {"0.05", NULL}, {"0.06", NULL},
                {"0.07", NULL}, {"0.08", NULL}, {"0.09", NULL}, {"0.10", NULL}, {"0.11", NULL}, {"0.12", NULL},
                {"0.13", NULL}, {"0.14", NULL}, {"0.15", NULL}, {"0.16", NULL}, {"0.17", NULL}, {"0.18", NULL},
                {"0.19", NULL}, {"0.20", NULL}, {"0.21", NULL}, {"0.22", NULL}, {"0.23", NULL}, {"0.24", NULL},
                {"0.25", NULL}, {"0.26", NULL}, {"0.27", NULL}, {"0.28", NULL}, {"0.29", NULL}, {"0.30", NULL},
                {"0.31", NULL}, {"0.32", NULL}, {"0.33", NULL}, {"0.34", NULL}, {"0.35", NULL}, {"0.36", NULL},
                {"0.37", NULL}, {"0.38", NULL}, {"0.39", NULL}, {"0.40", NULL}, {"0.41", NULL}, {"0.42", NULL},
                {"0.43", NULL}, {"0.44", NULL}, {"0.45", NULL}, {"0.46", NULL}, {"0.47", NULL}, {"0.48", NULL},
                {"0.49", NULL}, {"0.50", NULL},
          },
          "0.00"},
         {"vnovel_font_word_spacing",
          "Font Word Spacing",
          "Add space at each word gap, as a fraction of the font's own size, on "
          "top of the letter spacing. Useful with a face whose word gaps are too "
          "tight to tell apart, or after raising Font Letter Spacing and wanting "
          "the words to separate again. Applies to ONScripter and PONScripter "
          "text; KiriKiri lays its text out itself and ignores it.",
          {
                {"-1.00", NULL}, {"-0.90", NULL}, {"-0.80", NULL}, {"-0.70", NULL}, {"-0.60", NULL}, {"-0.50", NULL},
                {"-0.40", NULL}, {"-0.30", NULL}, {"-0.25", NULL}, {"-0.20", NULL}, {"-0.19", NULL}, {"-0.18", NULL},
                {"-0.17", NULL}, {"-0.16", NULL}, {"-0.15", NULL}, {"-0.14", NULL}, {"-0.13", NULL}, {"-0.12", NULL},
                {"-0.11", NULL}, {"-0.10", NULL}, {"-0.09", NULL}, {"-0.08", NULL}, {"-0.07", NULL}, {"-0.06", NULL},
                {"-0.05", NULL}, {"-0.04", NULL}, {"-0.03", NULL}, {"-0.02", NULL}, {"-0.01", NULL}, {"0.00", NULL},
                {"0.01", NULL}, {"0.02", NULL}, {"0.03", NULL}, {"0.04", NULL}, {"0.05", NULL}, {"0.06", NULL},
                {"0.07", NULL}, {"0.08", NULL}, {"0.09", NULL}, {"0.10", NULL}, {"0.11", NULL}, {"0.12", NULL},
                {"0.13", NULL}, {"0.14", NULL}, {"0.15", NULL}, {"0.16", NULL}, {"0.17", NULL}, {"0.18", NULL},
                {"0.19", NULL}, {"0.20", NULL}, {"0.21", NULL}, {"0.22", NULL}, {"0.23", NULL}, {"0.24", NULL},
                {"0.25", NULL}, {"0.26", NULL}, {"0.27", NULL}, {"0.28", NULL}, {"0.29", NULL}, {"0.30", NULL},
                {"0.31", NULL}, {"0.32", NULL}, {"0.33", NULL}, {"0.34", NULL}, {"0.35", NULL}, {"0.36", NULL},
                {"0.37", NULL}, {"0.38", NULL}, {"0.39", NULL}, {"0.40", NULL}, {"0.41", NULL}, {"0.42", NULL},
                {"0.43", NULL}, {"0.44", NULL}, {"0.45", NULL}, {"0.46", NULL}, {"0.47", NULL}, {"0.48", NULL},
                {"0.49", NULL}, {"0.50", NULL},
          },
          "0.00"},
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
