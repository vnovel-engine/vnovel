/* vnovel_debug.c — the shared debug mask. See vnovel_debug.h for the layout,
 * the environment syntax and why each module keeps its own slice.
 *
 * Each project compiles its own copy. Nothing here reaches outside <stdint.h>,
 * <stdio.h>, <stdlib.h> and <string.h>, so a standalone engine build has the
 * same behaviour as the core. */

#include "vnovel_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_word = 0;

uint64_t vnovel_debug_word(void)
{
   return g_word;
}

void vnovel_debug_set(uint64_t word)
{
   g_word = word;
}

void vnovel_debug_add(uint64_t bits)
{
   g_word |= bits;
}

/* Every bit of this slice, unshifted (the local bit space). */
static uint64_t slice_local_all(const VNovelDebugDesc *desc)
{
   if (desc->width >= 64) return UINT64_MAX;
   return (UINT64_C(1) << desc->width) - 1;
}

/* Trim leading and trailing blanks in place; returns the start. */
static char *trim(char *s)
{
   while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
   char *e = s + strlen(s);
   while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
   return s;
}

/* Skip the "prefix:" qualifier, if the item carries one. Returns 0 when the
 * item is qualified and the qualifier is not this module's, so the caller
 * ignores the item rather than warning about a name it was never given a table
 * for. */
static int strip_prefix(const VNovelDebugDesc *desc, char **item)
{
   const char *colon = strchr(*item, ':');
   if (!colon) return 1;
   size_t n = (size_t)(colon - *item);
   if (n != strlen(desc->prefix) || strncmp(*item, desc->prefix, n)) return 0;
   *item = (char *)colon + 1;
   return 1;
}

uint64_t vnovel_debug_parse(const VNovelDebugDesc *desc, const char *spec)
{
   if (!spec || !*spec) return 0;

   /* Names and "all" are this module's slice, accumulated here unshifted and
    * shifted once before returning. A raw number is not: it is the whole word,
    * which is the point of having one (a host sets several modules at once).
    * The two may be mixed, and the raw number is then ORed in as written. */
   uint64_t local = 0;
   uint64_t word  = 0;
   const char *p  = spec;
   while (*p)
   {
      char        buf[64];
      const char *item = p;
      while (*p && *p != ',') p++;
      size_t len = (size_t)(p - item);
      if (*p == ',') p++;
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      memcpy(buf, item, len);
      buf[len] = 0;

      char *s = trim(buf);
      if (!*s) continue;

      /* A qualified item is another module's unless it names this one. Done
       * before the keyword tests so "krkr:all" means this slice of "all". */
      if (!strip_prefix(desc, &s)) continue;
      s = trim(s);
      if (!*s) continue;

      /* Slice-local, so one VNOVEL_DEBUG=all works in a combined build. */
      if (!strcmp(s, "all"))
      {
         local = slice_local_all(desc);
         continue;
      }
      if (!strcmp(s, "none") || !strcmp(s, "off"))
      {
         local = 0;
         word  = 0;
         continue;
      }

      /* A raw word, for a host that already has one. */
      if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
      {
         word |= (uint64_t)strtoull(s + 2, NULL, 16);
         continue;
      }
      if (s[0] >= '0' && s[0] <= '9')
      {
         word |= (uint64_t)strtoull(s, NULL, 10);
         continue;
      }

      int matched = 0;
      for (unsigned i = 0; i < desc->bit_count; i++)
      {
         if (strcmp(s, desc->bits[i].name)) continue;
         local |= (uint64_t)desc->bits[i].bit;
         matched = 1;
         break;
      }
      if (!matched)
      {
         /* A name this module does not own is simply another module's: in a
          * combined build one VNOVEL_DEBUG drives every slice, so a module must
          * ignore what it cannot resolve without complaining about it. A
          * qualifier that names ANOTHER module is skipped the same way (see
          * strip_prefix). The start-up banner is what makes a typo visible:
          * it prints the resolved word and the bits it turned on. */
         continue;
      }
   }

   return (local << desc->shift) | word;
}

uint64_t vnovel_debug_env(const VNovelDebugDesc *desc)
{
   const char *spec = getenv(VNOVEL_DEBUG_ENV);
   uint64_t    bits = vnovel_debug_parse(desc, spec);
   g_word |= bits;
   return bits;
}

void vnovel_debug_describe(const VNovelDebugDesc *desc)
{
   uint64_t on = (g_word >> desc->shift) & slice_local_all(desc);

   for (unsigned i = 0; i < desc->bit_count; i++)
   {
      if (!(on & desc->bits[i].bit)) continue;
      fprintf(stderr, "(info) debug: %s: '%s' (%s)\n", desc->prefix,
              desc->bits[i].name, desc->bits[i].description);
   }
   fflush(stderr);
}

void vnovel_debug_describe_word(void)
{
   fprintf(stderr, "(info) debug: " VNOVEL_DEBUG_ENV "=0x%016llx%s\n",
           (unsigned long long)g_word,
           g_word ? " (bits listed above)" : "");
   fflush(stderr);
}
