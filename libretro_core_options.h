#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>
#include <libretro.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

struct retro_core_option_definition option_defs_us[] = {
   {
      "onscripter-en_frame_sync",
      "Frame Synchronization",
      "When enabled, every screen update is displayed exactly once for "
      "smooth animations and effects. Disable if you experience performance issues.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "onscripter-en_mode_indicators",
      "Mode Indicators",
      "Show on-screen badges for auto mode and skip mode "
      "(green and yellow dots in the top-right corner).",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "onscripter-en_debug_level",
      "Debug Level",
      "Log engine script parsing to RetroArch's log. 0=off, 3=verbose. "
      "Useful for reporting issues with specific visual novels.",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      "onscripter-en_software_cursor",
      "Software Cursor",
      "Draw a cursor directly on the screen. Enable for fullscreen "
 "where the host cursor is hidden. Disable for windowed mode.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "onscripter-en_text_speed",
      "Text Speed",
      "Override the engine's text display speed. 'default' uses the "
      "game's built-in speed. Slower speeds display characters one by "
      "one with a longer delay; faster speeds reduce the delay. "
      "Instant displays all text immediately.",
      {
         { "default",  NULL },
         { "slow",     NULL },
         { "normal",   NULL },
         { "fast",     NULL },
         { "instant",  NULL },
         { NULL, NULL },
      },
      "default"
   },
   {
      "onscripter-en_fastforward_speed",
      "Fast-Forward Speed",
      "Controls how fast the game advances during fast-forward. "
      "Higher values skip more time per frame, making text, "
      "animations and effects complete faster.",
      {
         { "2x",    NULL },
         { "4x",    NULL },
         { "8x",    NULL },
         { "16x",   NULL },
         { "32x",   NULL },
         { "max",   NULL },
         { NULL, NULL },
      },
      "8x"
   },
   {
      "onscripter-en_mouse_sensitivity",
      "Mouse Sensitivity",
      "Scales mouse movement for the in-game cursor. "
      "Adjust if the software cursor moves faster or slower "
      "than the real mouse.",
      {
         { "0.5",  NULL },
         { "0.75", NULL },
         { "1.0",  NULL },
         { "1.25", NULL },
         { "1.5",  NULL },
         { "2.0",  NULL },
         { NULL, NULL },
      },
      "1.0"
   },
   {
      "onscripter-en_key_lookback",
      "Lookback Key",
      "Key to open the text history / lookback mode.",
      { {"[", NULL}, {"h", NULL}, {"q", NULL}, {"b", NULL}, { NULL, NULL } },
      "["
   },
   {
      "onscripter-en_key_menu",
      "Menu Key",
      "Key to open the in-game right-click menu.",
      { {"backspace", NULL}, {"escape", NULL}, {"x", NULL}, { NULL, NULL } },
      "backspace"
   },
   {
      "onscripter-en_key_advance",
      "Advance Text Key",
      "Key to advance text / confirm.",
      { {"return", NULL}, {"space", NULL}, {"z", NULL}, { NULL, NULL } },
      "return"
   },
   {
      "onscripter-en_internal_resolution",
      "Internal Resolution",
      "Rendering resolution produced by the game engine. "
      "'native' uses whatever the game/script requests. A fixed size "
      "scales the engine's own framebuffer before it reaches RetroArch "
      "(RetroArch's scaling settings still apply on top).",
      {
         { "native",   NULL },
         { "640x480",  NULL },
         { "800x600",  NULL },
         { "960x720",  NULL },
         { "1024x768", NULL },
         { "1280x960", NULL },
         { "1600x1200",NULL },
         { NULL, NULL },
      },
      "native"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/* No translations for now */
struct retro_core_option_definition *option_defs_intl[RETRO_LANGUAGE_LAST] = {
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

   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version >= 1))
   {
      struct retro_core_options_intl core_options_intl;
      core_options_intl.us    = option_defs_us;
      core_options_intl.local = NULL;
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_intl);
   }
   else
   {
      /* Fallback for older frontends: build retro_variable array */
      size_t i, num_options = 0;
      struct retro_variable *variables = NULL;
      char **values_buf = NULL;

      for (i = 0; option_defs_us[i].key; i++) num_options++;

      variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
      values_buf = (char **)calloc(num_options, sizeof(char *));
      if (!variables || !values_buf) goto error;

      for (i = 0; i < num_options; i++) {
         const char *key           = option_defs_us[i].key;
         const char *desc          = option_defs_us[i].desc;
         const char *default_value = option_defs_us[i].default_value;
         struct retro_core_option_value *values = option_defs_us[i].values;
         size_t buf_len = 3, default_index = 0, num_values = 0, j;

         for (j = 0; values[j].value; j++) {
            if (default_value && strcmp(values[j].value, default_value) == 0)
               default_index = j;
            buf_len += strlen(values[j].value);
            num_values++;
         }

         if (num_values > 0 && desc) {
            buf_len += num_values - 1 + strlen(desc);
            values_buf[i] = (char *)calloc(buf_len, 1);
            if (!values_buf[i]) goto error;
            sprintf(values_buf[i], "%s; %s", desc, values[default_index].value);
            for (j = 0; j < num_values; j++)
               if (j != default_index) {
                  strcat(values_buf[i], "|");
                  strcat(values_buf[i], values[j].value);
               }
         }

         variables[i].key   = key;
         variables[i].value = values_buf[i];
      }

      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

error:
      if (values_buf) {
         for (i = 0; i < num_options; i++) free(values_buf[i]);
         free(values_buf);
      }
      free(variables);
   }
}

#ifdef __cplusplus
}
#endif

#endif
