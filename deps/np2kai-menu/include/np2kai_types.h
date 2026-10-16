/*---------------------------------------------------------------------------*/
/* Types and macros NP2kai's embed/ code expects.
 *
 * NP2kai's own `compiler_base.h` defines its integer types by hand:
 *
 *     typedef long               int32_t;
 *     typedef unsigned long      uint32_t;
 *
 * which is the platform's `long`, not a fixed width. On LP64 those are 64-bit,
 * so including NP2kai's header alongside the real <stdint.h> is a conflicting
 * redefinition of the standard names, and a silent width error besides.
 *
 * This header provides the same *NP2kai* type names (`UINT8`, `UINT32`,
 * `BOOL`, ...) mapped onto the real fixed-width types, and deliberately does NOT
 * define `int32_t` and friends - those come from <stdint.h>. The vendor sources
 * only ever use the UINTN/INTN spellings, so nothing is lost.
 *
 * Keeping this file small is the point: it is the entire adaptation layer
 * between NP2kai code and this tree, so the vendored sources stay as close to
 * upstream as possible. Anything that needs changing here is a fact about the
 * port worth knowing.
 */
/*---------------------------------------------------------------------------*/

#ifndef VNOVEL_NP2KAI_TYPES_H
#define VNOVEL_NP2KAI_TYPES_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* NP2kai's own scalar names, on real fixed-width types. */
typedef int                INT;
typedef int                SINT;
typedef unsigned int       UINT;
typedef int8_t             INT8;
typedef int8_t             SINT8;
typedef uint8_t            UINT8;
typedef int16_t            INT16;
typedef int16_t            SINT16;
typedef uint16_t           UINT16;
typedef int32_t            INT32;
typedef int32_t            SINT32;
typedef uint32_t           UINT32;
typedef int64_t            INT64;
typedef int64_t            SINT64;
typedef uint64_t           UINT64;

/* Variable-size and pointer-sized spellings, exactly as upstream names them. */
typedef size_t             SIZET;
typedef intptr_t           INTPTR;
typedef uintptr_t          UINTPTR;
typedef intptr_t           INT_PTR;
typedef uintptr_t          UINT_PTR;
typedef intmax_t           INTMAX;
typedef uintmax_t          UINTMAX;

/* Boolean and result conventions. NP2kai's code tests `BRESULT` against
 * SUCCESS/FAILURE and `BOOL` against TRUE/FALSE. */
typedef int                BOOL;
typedef int                BRESULT;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE 1
#endif

/* NP2kai guards some string helpers on this and reaches for its own copies;
 * the standard ones are already correct here. */
#ifndef _T
#define _T(s) s
#endif

/* Memory and arithmetic helpers the vendored files use. Upstream these come
 * from compiler_base.h and common/_memory.h; providing them here keeps the
 * vendored sources unmodified. The memory macros are upstream's untraced
 * variants (MEMTRACE off), which is what this tree wants - tracing is an
 * upstream debugging facility, not something to carry. */
#define ZeroMemory(d, z)        memset((d), 0, (z))
#define CopyMemory(d, s, z)     memcpy((d), (s), (z))
#define FillMemory(d, z, c)     memset((d), (c), (z))
#define _MEM_INIT()
#define _MALLOC(a, b)           malloc(a)
#define _MFREE(a)               free(a)
#define _HANDLE_ADD(a, b)
#define _HANDLE_REM(a)
#define _MEM_USED(a)

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* Trace hooks, compiled out. Upstream's sdl/trace.h defines them as no-ops
 * unless TRACE is defined, and the SDL target does not define it, so a release
 * build of upstream has exactly these. The vendored files use TRACEOUT for
 * unreachable-code diagnostics, which is where "not supported" belongs: it is
 * a programming error, not a runtime condition. */
#define TRACEINIT()
#define TRACETERM()
#define TRACEOUT(a)
#define VERBOSE(a)

#define MAX_PATH 4096
#define NELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

/* Upstream's `OEMCHAR` is plain char (compiler_base.h:268), with the string
 * helpers below it written in terms of it. All of these are upstream's own
 * definitions, so adopting them keeps the vendored sources unmodified. */
#define OEMCHAR char
#define OEMTEXT(string) string
#define STRCALL

#define OEMSTRLEN         strlen
#define OEMSTRNLEN(s, z)  strlen(s)
#define OEMSTRNLENS(s, z) strlen(s)
#define OEMSTRCMP         strcmp
#define OEMSTRNCMP        strncmp
#define OEMSTRCPY(s1, s2) strcpy((s1), (s2))
#define OEMSTRCAT(s1, s2) strcat((s1), (s2))
#define OEMSTRCHR         strchr
#define OEMSTRRCHR        strrchr
#define OEMSTRSTR         strstr
#define OEMSPRINTF        sprintf
#define OEMPRINTFSTR(s)   printf(OEMTEXT("%s"), (s))
#define OEMSNPRINTF       snprintf

/* ---------------------------------------------------------------------------
 * The milstr string functions, in their ANK (single-byte) form.
 *
 * Upstream picks an implementation by encoding macro - OSLANG_SJIS, _EUC or
 * _UTF8, defaulting to ANK - and the SJIS/EUC/UTF8 ones are multibyte-aware:
 * they refuse to split a character across a boundary. This port's menu text is
 * English, so ANK is the correct choice, and ANK's implementations are
 * byte-wise, which is why they are reproduced exactly here rather than
 * approximated.
 *
 * These are used on menu *text* (vrammix draws captions), not only file names,
 * so getting them right matters. Transcribed from upstream's common/milstr.c;
 * if this port ever carries a Japanese menu, the encoding must be switched by
 * vendoring the matching milstr table rather than editing these.
 * --------------------------------------------------------------------------- */
static inline int vnovel_np2kai_charsize(const char *str)
{
	return (str[0] != '\0') ? 1 : 0;
}

static inline void vnovel_np2kai_ncpy(char *dst, const char *src,
	unsigned int maxlen)
{
	int i;

	if (maxlen > 0)
	{
		maxlen--;
		for (i = 0; i < (int)maxlen && src[i]; i++)
			dst[i] = src[i];
		dst[i] = '\0';
	}
}

static inline int vnovel_np2kai_ncmp(const char *str, const char *cmp,
	unsigned int maxlen)
{
	int s, c;

	while (maxlen)
	{
		s = (unsigned char)*str;
		c = (unsigned char)*cmp;
		if (s != c)
			return s - c;
		if (s == 0)
			break;
		str++;
		cmp++;
		maxlen--;
	}
	return 0;
}

#define milstr_charsize(s)      vnovel_np2kai_charsize(s)
#define milstr_ncpy(d, s, l)    vnovel_np2kai_ncpy((d), (s), (l))
#define milstr_ncmp(s, c, n)    vnovel_np2kai_ncmp((s), (c), (n))

#endif /* VNOVEL_NP2KAI_TYPES_H */
