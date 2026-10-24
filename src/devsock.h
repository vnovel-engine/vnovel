// devsock.h — the development socket: the text stream out, navigation in.
//
// A development surface for the text seam (engine_bridge.h): the same records
// the text-run log writes, delivered live on a TCP socket instead of a tmpfs
// file the launcher only flushes at exit, plus the reverse direction — a
// navigation intent from the consumer back into the running game.
//
// Deliberately not a protocol with a future: ONE consumer at a time, lines are
// best-effort in both directions (a stalled peer drops records rather than
// pacing the game), and there is no framing beyond '\n'. Everything a real
// consumer would need — reconnection, acks, back-pressure — is exactly what a
// development tool does not.
//
// Off unless VNOVEL_DEV_PORT names a port; opening binds 127.0.0.1 only.
//
// Why the socket is in the core and not the bridge: the bridge is linked into
// every module (a standalone engine build included) and must stay free of
// transport concerns; only the libretro shell has a place to poll it.

#ifndef VNOVEL_DEVSOCK_H
#define VNOVEL_DEVSOCK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

   /* --- the record vocabulary --------------------------------------------
    *
    * What a consumer of this socket sees, defined once so it is not spelled as
    * literals through the sink and every client.
    *
    * THE CLIENT PROTOCOL IS THE SUPERSET. Every event the ENGINE ABI can publish
    * (engine_bridge.h, VNOVEL_EV_*) appears here under the SAME NAME, plus the
    * two records that only the shell can produce: a page's text (`PAGE`) and a
    * drawn run (`run`). A client therefore implements one vocabulary, and the
    * engine speaks its event subset of it -- rather than two lists that have to
    * be kept in step by hand, which is how VNOVEL_FLOW_LP and VNOVEL_EDGE_START
    * came to share the value 0 and mean different things.
    *
    * One record per line, and the WHICH is the leading token:
    *
    *   PAGE(<n>) <text>      one page from the look-ahead walk, numbered from 1
    *                          in publication order. Newlines inside the text are
    *                          escaped as `\n`, so a page is always one line. The
    *                          number is STRUCTURE: it ties the events and choices
    *                          that belong to a page to that page.
    *   run <n> draw ...       one drawn glyph (position and style).
    *   CHOICE(<n>) <label>    WHAT choice <n> IS, for a menu the page text
    *                          cannot describe (a sprite-button menu has no
    *                          labels in the text at all). Sent after the pause
    *                          that arms the menu, one per choice; <n> is the
    *                          ordinal `select` takes.
    *
    * and the five page events, spelled as brief tokens so they can never be
    * mistaken for the `<pause>` / `<link:...>` marks that are part of a page's
    * TEXT (which is also why a page's marks carry a sentinel byte: see
    * ONScripterLookAhead.h's LA_MARK):
    *
    *   VNOVEL_EV_PAGE_START  <<<ps>>>   a page began. Materialised as a rule.
    *   VNOVEL_EV_PAGE_END    <<<pe>>>   a page ended. Structural; not drawn.
    *   VNOVEL_EV_LINE_PAUSE  <<<lp>>>   an in-page wait: the same page continues.
    *   VNOVEL_EV_PAGE_PAUSE  <<<pp>>>   a wait at a page's END.
    *   VNOVEL_EV_RESUME      <go>       the reader acted; the engine is running.
    *
    * The first two are BOUNDARIES and the last three are WAITS, and a boundary
    * does not imply a wait: a game that runs one page into the next pauses
    * nowhere and still has both. */
#define VNOVEL_REC_PAGE   "PAGE("
#define VNOVEL_REC_RUN    "run "
#define VNOVEL_REC_CHOICE "CHOICE("

/* The five page events, as wire tokens. The NAMES match the engine ABI's
 * (engine_bridge.h) so the two vocabularies are one list; these are the
 * spellings this socket puts on the wire. */
#define VNOVEL_REC_PAGE_START "<<<ps>>>"
#define VNOVEL_REC_PAGE_END   "<<<pe>>>"
#define VNOVEL_REC_LINE_PAUSE "<<<lp>>>"
#define VNOVEL_REC_PAGE_PAUSE "<<<pp>>>"
#define VNOVEL_REC_RESUME     "<go>"

   /* Bind and listen on 127.0.0.1:`port`. Returns 0 on success, -1 when the
    * socket cannot be opened. Calling it twice is harmless. Nothing is
    * accepted until the first poll: a consumer connects when it wants to.
    *
    * Built only for `make DEVWIRE=1`; without it these are INLINE NO-OPS (see
    * below), so a caller never needs its own #ifdef. */
#ifdef VNOVEL_DEVWIRE
   int vnovel_dev_socket_open(int port);

   /* Stop listening and drop the current consumer. */
   void vnovel_dev_socket_close(void);

   /* True while a consumer is connected, so a caller can skip formatting a
    * record nobody is listening for. */
   int vnovel_dev_socket_connected(void);

   /* True once the listener exists, whether or not anybody has attached. This
    * is the gate a PRODUCER should use: records sent with no consumer are kept
    * and released when one attaches (see the note in devsock.c), so "no
    * consumer yet" is a reason to buffer, not a reason to skip the work. */
   int vnovel_dev_socket_listening(void);

   /* Send one record. `data`/`n` need not be NUL-terminated; a '\n' is
    * appended here, so callers pass one line without its newline.
    *
    * Best-effort and non-blocking: a peer that is not reading drops this
    * record rather than stalling the caller, which may be an engine thread
    * mid-frame. A closed or errored peer disconnects. */
   void vnovel_dev_socket_send(const char *data, size_t n);

   /* Read whatever has arrived and hand each complete line (without its '\n')
    * to `on_line`. Returns the number of lines delivered. Lines longer than the
    * internal buffer are truncated, not split. */
   int vnovel_dev_socket_poll(void (*on_line)(const char *line, void *userdata),
                              void *userdata);

#else /* !VNOVEL_DEVWIRE -- the wire protocol is compiled out */
   /* No-ops, so the shell calls these unconditionally and a non-wire build simply
    * has no socket. */
   static inline int  vnovel_dev_socket_open(int port) { (void)port; return -1; }
   static inline void vnovel_dev_socket_close(void) {}
   static inline int  vnovel_dev_socket_connected(void) { return 0; }
   static inline int  vnovel_dev_socket_listening(void) { return 0; }
   static inline void vnovel_dev_socket_send(const char *data, size_t n)
   { (void)data; (void)n; }
   static inline int  vnovel_dev_socket_poll(
      void (*on_line)(const char *line, void *userdata), void *userdata)
   { (void)on_line; (void)userdata; return 0; }
#endif /* VNOVEL_DEVWIRE */

#ifdef __cplusplus
}
#endif

#endif /* VNOVEL_DEVSOCK_H */
