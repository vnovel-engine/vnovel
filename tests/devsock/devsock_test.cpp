// Unit harness for the development socket's outbound backlog (src/devsock.c).
//
// WHY THIS EXISTS, and why it is not a live run: the property under test is what
// the 64 KiB ring DROPS when it overflows. A live capture cannot exercise it --
// the kernel's send buffer swallows a whole page's worth of records before the
// ring ever grows, so a stalled-consumer experiment drains nothing and proves
// nothing (measured: a 45 s stall on Fate produced 1527 run lines and all 15
// pages, with an empty ring). Here the ring is filled DETERMINISTICALLY by never
// accepting a consumer: with no peer, vnovel_dev_socket_send has nowhere to
// flush, so every record lands in the ring and the eviction path is the only
// thing that can run.
//
// THE PROPERTY. The stream is deliberately uneven -- on KiriKiri the per-glyph
// `run` records are ~65x the PAGE bytes -- so an oldest-first eviction spends the
// ring on glyphs and can take the sparse records (PAGE, the flow events, CHOICE)
// with it. Those are the ones a consumer cannot reconstruct: they pair a page
// with the pause it belongs to, and the whole protocol rests on that pairing.
// So the assertion is asymmetric, exactly as the ring's rule is:
//
//   * every PAGE ordinal survives, in order, with no gaps; and
//   * glyph bulk WAS dropped (otherwise the run proves nothing).
//
// The second half is what stops this being a tautology: a test that only checked
// "the pages are here" would pass against a ring with no eviction at all.

#include "devsock.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
   else
      printf("ok:   %s\n", what);
}

/* Read from `fd` until it is quiet for `quiet_ms`, or `budget_ms` elapses.
 * Appends complete lines to `out`, ACCUMULATING across calls (`*lenp` is the
 * running length): the ring flushes over several polls, so a reader that reset
 * the buffer each time would keep only the last, empty read. */
static void drain(int fd, char *out, size_t cap, size_t *lenp,
                  int budget_ms, int quiet_ms)
{
   struct timespec t0, now;
   clock_gettime(CLOCK_MONOTONIC, &t0);
   size_t len = *lenp;
   int idle = 0;
   for (;;)
   {
      clock_gettime(CLOCK_MONOTONIC, &now);
      const long elapsed = (now.tv_sec - t0.tv_sec) * 1000 +
                           (now.tv_nsec - t0.tv_nsec) / 1000000;
      if (elapsed > budget_ms || idle > quiet_ms)
         break;

      char buf[8192];
      const ssize_t n = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
      if (n > 0)
      {
         idle = 0;
         if (len + (size_t)n < cap)
         {
            memcpy(out + len, buf, (size_t)n);
            len += (size_t)n;
         }
         continue;
      }
      idle += 2;
      struct timespec nap = {0, 2000000};
      nanosleep(&nap, 0);
   }
   out[len] = 0;
   *lenp = len;
}

static int connect_to(int port)
{
   const int fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0)
      return -1;
   struct sockaddr_in a;
   memset(&a, 0, sizeof a);
   a.sin_family = AF_INET;
   a.sin_port = htons((unsigned short)port);
   a.sin_addr.s_addr = inet_addr("127.0.0.1");
   if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0)
   {
      close(fd);
      return -1;
   }
   return fd;
}

int main(void)
{
   /* A port unlikely to be taken; try a few rather than fail the suite for a
    * busy machine. */
   static const int ports[] = {58871, 58872, 58873, 58874};
   int port = 0;
   for (size_t i = 0; i < sizeof ports / sizeof ports[0]; i++)
   {
      if (vnovel_dev_socket_open(ports[i]) == 0) { port = ports[i]; break; }
   }
   if (!port)
   {
      printf("SKIP: could not bind a test port (all in use)\n");
      return 0;
   }

   /* Fill the ring with NO consumer attached: send a PAGE, then a fan of long
    * run records, and repeat. 400 pages and 120 KB of glyphs against a 64 KiB
    * ring, so eviction is certain and the pages are a small minority of the
    * bytes -- the shape the real stream has. */
   enum { PAGES = 400, RUNS_PER_PAGE = 40 };
   char line[512];
   for (int p = 1; p <= PAGES; p++)
   {
      const int n = snprintf(line, sizeof line,
                             "PAGE(%d) page number %d of the flood", p, p);
      vnovel_dev_socket_send(line, (size_t)n);
      for (int r = 0; r < RUNS_PER_PAGE; r++)
      {
         const int m = snprintf(line, sizeof line,
                                "run %d draw local=(%d,%d) text=x color=FFFFFF "
                                "opacity=255 aa=1 shadow=1024/000000+0+0 "
                                "pad=................................................................",
                                p * 1000 + r, r, p % 30);
         vnovel_dev_socket_send(line, (size_t)m);
      }
   }

   /* Now accept a consumer and let it read what survived. */
   const int fd = connect_to(port);
   check(fd >= 0, "a consumer can connect");
   if (fd < 0)
   {
      vnovel_dev_socket_close();
      return 1;
   }

   static char got[1 << 20];
   got[0] = 0;
   size_t gotlen = 0;
   /* Poll once per idle slice so the accept happens and the ring flushes; the
    * ring may take several rounds to empty, so the drained bytes accumulate. */
   for (int i = 0; i < 40; i++)
   {
      vnovel_dev_socket_poll(NULL, NULL);
      drain(fd, got, sizeof got - 256, &gotlen, 60, 15);
   }

   const bool connected = vnovel_dev_socket_connected() != 0;
   check(connected, "the consumer is accepted");

   /* the assertions */

   int pages = 0, runs = 0, last_ordinal = 0, gaps = 0, dup = 0;
   bool seen[PAGES + 2];
   memset(seen, 0, sizeof seen);
   for (const char *s = got; *s; )
   {
      const char *e = strchr(s, '\n');
      if (!e)
         break;
      if (!strncmp(s, "PAGE(", 5))
      {
         const int n = atoi(s + 5);
         if (n <= 0 || n > PAGES) { s = e + 1; continue; }
         if (seen[n]) dup++;
         else { seen[n] = true; pages++; }
         if (n <= last_ordinal) gaps++;     /* out of order: also a defect */
         last_ordinal = n > last_ordinal ? n : last_ordinal;
      }
      else if (!strncmp(s, "run ", 4))
         runs++;
      s = e + 1;
   }

   const int runs_sent = PAGES * RUNS_PER_PAGE;

   printf("     pages survived %d/%d, run lines survived %d/%d\n",
          pages, PAGES, runs, runs_sent);

   check(runs_sent > runs, "glyph bulk WAS dropped (the ring really overflowed)");
   check(dup == 0, "no PAGE ordinal is delivered twice");
   check(gaps == 0, "PAGE ordinals stay in order");
   /* Every page must survive: they are ~1/70th of the bytes, so a ring that
    * sheds glyphs first has room for all of them. */
   check(pages == PAGES, "every PAGE record survived the overflow");

   close(fd);
   vnovel_dev_socket_close();

   printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",
          failures);
   return failures ? 1 : 0;
}
