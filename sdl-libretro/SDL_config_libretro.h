#ifndef _SDL_config_libretro_h
#define _SDL_config_libretro_h

#include "SDL_platform.h"

/* C datatypes */
#define SDL_HAS_64BIT_TYPE 1

/* Endianness */
#ifndef SDL_BYTEORDER
#define SDL_BYTEORDER 1234
#endif

/* Useful headers */
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDARG_H 1
#define HAVE_STRING_H 1
#define HAVE_STDINT_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MATH_H 1

/* C library functions */
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_ALLOCA 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRDUP 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOD 1
#define HAVE_SSCANF 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_NANOSLEEP 1

/* POSIX features — available on both Linux and mingw (winpthreads) */
#ifndef _WIN32
#define HAVE_SIGNAL_H 1
#define HAVE_SIGACTION 1
#define HAVE_SETJMP 1
#endif
#define HAVE_OPENDIR 1
#define HAVE_READDIR 1
#define HAVE_CLOSEDIR 1

/* Audio: dummy only */
#define SDL_AUDIO_DRIVER_DUMMY 1

/* Video: dummy only */
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_VIDEO_DRIVER_NEGATE_MASKS 1

/* CD-ROM: dummy */
#define SDL_CDROM_DUMMY 1

/* Joystick: dummy */
#define SDL_JOYSTICK_DUMMY 1

/* Loadso: dummy */
#define SDL_LOADSO_DUMMY 1

/* Threads: pthread (works on Linux + mingw/winpthreads) */
#define SDL_THREAD_PTHREAD 1
#define SDL_THREAD_PTHREAD_RECURSIVE_MUTEX 1

/* Timer: must match the platform's systimer source, because each
 * timer/<platform>/SDL_systimer.c is wrapped in its own #ifdef and compiles to
 * nothing otherwise. deps/sdl/include/SDL_config.h hardcodes SDL_TIMER_UNIX (it
 * was generated on Linux), so on mingw the win32 source was being preprocessed
 * away entirely — leaving SDL_GetTicks undefined at link time. This file is
 * force-included with -include, so defining the right one here wins. */
#ifdef _WIN32
#define SDL_TIMER_WIN32 1
#else
#define SDL_TIMER_UNIX 1
#endif

#endif /* _SDL_config_libretro_h */
