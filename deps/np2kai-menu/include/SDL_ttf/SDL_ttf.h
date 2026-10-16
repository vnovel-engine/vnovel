/* Forwarding header: the vendored fontmng.c asks for <SDL_ttf/SDL_ttf.h>
 * (NP2kai's SDL1 layout), while this tree keeps SDL_ttf.h in src/sdl-libretro.
 * A forwarder rather than an edit to fontmng.c, so the vendored source stays as
 * close to upstream as possible. */
#include <SDL_ttf.h>
