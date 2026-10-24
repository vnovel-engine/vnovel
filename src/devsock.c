// devsock.c — see devsock.h for what this is and why it is this small.
//
// One listener on loopback, at most one accepted consumer, two buffers: what
// the game produced but the peer has not read yet, and what the peer sent but
// the game has not consumed yet. Both are droppable. No threads are created and
// nothing blocks: the shell polls the consumer, and a sender (which is an
// engine's own script thread on KiriKiri) appends from wherever it is.
//
// One mutex, because those two really are different threads: the peer, the
// buffers and the listener are all behind it, so "send while the poll side is
// closing the peer" cannot happen. Nothing under the lock blocks (the fd is
// non-blocking), so a sender never waits longer than a memcpy.

/* The development wire protocol is a BUILD option (make DEVWIRE=1), not only a
 * runtime one: a build that ships a game should not carry a listener at all,
 * however politely it is off by default. Without VNOVEL_DEVWIRE this entire
 * translation unit is empty, so there is no socket code in the binary to reach
 * even by accident -- and the engines' publish calls are inline no-ops
 * (engine_bridge.h), so nothing calls in here. */
#ifdef VNOVEL_DEVWIRE

#include "devsock.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET dsock_t;
#define DSOCK_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int dsock_t;
#define DSOCK_BAD (-1)
#endif

static pthread_mutex_t ds_lock = PTHREAD_MUTEX_INITIALIZER;

/* The listener and the one consumer. A consumer is never replaced by another
 * one: it is dropped when it closes or errors, and the next poll accepts a new
 * connection only then. */
static dsock_t ds_listen = DSOCK_BAD;
static dsock_t ds_peer   = DSOCK_BAD;

/* Outbound: whole lines in, whole lines out. Records are kept whether or not a
 * consumer is attached: a client started by hand attaches after the game has
 * already published its opening pages, and the first page is usually the one
 * being worked on. The backlog is released on accept.
 *
 * When the buffer is full the OLDEST lines go, for the same reason the audio
 * buffer drops its oldest half: a stalled consumer must not grow this without
 * bound, and the newest text is the text the client is looking at. */
#define DS_OUT_CAP 65536
static char   ds_out[DS_OUT_CAP];
static size_t ds_out_len = 0;

/* Inbound. A line longer than DS_LINE_CAP is truncated rather than split, which
 * is the right failure for a development tool: the consumer gets one garbled
 * intent instead of two plausible ones. */
#define DS_IN_CAP   4096
static char   ds_in[DS_IN_CAP];
#define DS_LINE_CAP 1024
#define DS_LINES_PER_POLL 8
static char   ds_line[DS_LINE_CAP];
static size_t ds_line_len = 0;

static void ds_set_nonblock(dsock_t fd)
{
#if defined(_WIN32)
   u_long one = 1;
   ioctlsocket(fd, FIONBIO, &one);
#else
   int fl = fcntl(fd, F_GETFL, 0);
   if (fl >= 0)
      fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

static void ds_close(dsock_t fd)
{
   if (fd == DSOCK_BAD)
      return;
#if defined(_WIN32)
   closesocket(fd);
#else
   close(fd);
#endif
}

static int ds_would_block(void)
{
#if defined(_WIN32)
   return WSAGetLastError() == WSAEWOULDBLOCK;
#else
   return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

/* Callers hold ds_lock. The outbound backlog is deliberately NOT dropped here:
 * a client that reconnects should still get the text it missed, which is the
 * same reason it is buffered with no consumer at all. */
static void ds_disconnect(void)
{
   ds_close(ds_peer);
   ds_peer     = DSOCK_BAD;
   ds_line_len = 0;
}

int vnovel_dev_socket_open(int port)
{
   if (port <= 0)
      return -1;

#if defined(_WIN32)
   {
      static int wsa_ready = 0;
      if (!wsa_ready)
      {
         WSADATA w;
         if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
            return -1;
         wsa_ready = 1;
      }
   }
#endif

   pthread_mutex_lock(&ds_lock);
   if (ds_listen != DSOCK_BAD)
   {
      pthread_mutex_unlock(&ds_lock);
      return 0; /* already open: an address can only be bound once anyway */
   }

   dsock_t fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd == DSOCK_BAD)
   {
      pthread_mutex_unlock(&ds_lock);
      return -1;
   }

   {
      int one = 1;
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
   }

   struct sockaddr_in addr;
   memset(&addr, 0, sizeof addr);
   addr.sin_family      = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* loopback: never wider */
   addr.sin_port        = htons((unsigned short)port);

   if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
       listen(fd, 1) != 0)
   {
      ds_close(fd);
      pthread_mutex_unlock(&ds_lock);
      return -1;
   }

   ds_set_nonblock(fd);
   ds_listen = fd;
   pthread_mutex_unlock(&ds_lock);
   fprintf(stderr, "devsocket: listening on 127.0.0.1:%d\n", port);
   return 0;
}

void vnovel_dev_socket_close(void)
{
   pthread_mutex_lock(&ds_lock);
   ds_disconnect();
   ds_close(ds_listen);
   ds_listen  = DSOCK_BAD;
   ds_out_len = 0; /* no listener means nobody can ever read this */
   pthread_mutex_unlock(&ds_lock);
}

int vnovel_dev_socket_connected(void)
{
   pthread_mutex_lock(&ds_lock);
   const int up = ds_peer != DSOCK_BAD;
   pthread_mutex_unlock(&ds_lock);
   return up;
}

int vnovel_dev_socket_listening(void)
{
   pthread_mutex_lock(&ds_lock);
   const int up = ds_listen != DSOCK_BAD;
   pthread_mutex_unlock(&ds_lock);
   return up;
}

/* Drop the oldest whole lines until `need` bytes of room exist, so the newest
 * records are the ones that survive. Callers hold ds_lock. */
static void ds_make_room(size_t need)
{
   if (need >= DS_OUT_CAP)
   {
      ds_out_len = 0; /* one record larger than the buffer: keep nothing */
      return;
   }
   if (ds_out_len + need <= DS_OUT_CAP)
      return;

   const size_t excess = ds_out_len + need - DS_OUT_CAP;
   size_t cut = 0;
   while (cut < excess && cut < ds_out_len)
   {
      const char *nl = (const char *)memchr(ds_out + cut, '\n', ds_out_len - cut);
      if (!nl)
      {
         cut = ds_out_len; /* no line boundary left: drop everything */
         break;
      }
      cut = (size_t)(nl - ds_out) + 1;
   }
   memmove(ds_out, ds_out + cut, ds_out_len - cut);
   ds_out_len -= cut;
}

/* Push as much of ds_out as the peer will take without blocking, keeping
 * whatever it could not send so the next append or accept tries again. */
static void ds_flush_locked(void)
{
   if (ds_peer == DSOCK_BAD || ds_out_len == 0)
      return;

   int flags = 0;
#if defined(MSG_NOSIGNAL) && !defined(_WIN32)
   flags = MSG_NOSIGNAL; /* deliverable elsewhere */
#endif
   ssize_t n = send(ds_peer, ds_out, (int)ds_out_len, flags);
   if (n > 0)
   {
      memmove(ds_out, ds_out + n, ds_out_len - (size_t)n);
      ds_out_len -= (size_t)n;
   }
   else if (n < 0 && !ds_would_block())
   {
      ds_disconnect();
   }
}

void vnovel_dev_socket_send(const char *data, size_t n)
{
   if (!data || n == 0 || n + 1 >= DS_OUT_CAP)
      return;

   pthread_mutex_lock(&ds_lock);

   /* No peer is not a reason to drop: the record is kept and released when one
    * attaches (see the note on ds_out above). Only a closed LISTENER means
    * nobody will ever read it, and then there is nothing to buffer for. */
   if (ds_listen == DSOCK_BAD)
   {
      pthread_mutex_unlock(&ds_lock);
      return;
   }

   /* Flush what the peer will take, then make room by dropping the OLDEST
    * lines, so the newest text survives a backlog. */
   ds_flush_locked();
   ds_make_room(n + 1);

   memcpy(ds_out + ds_out_len, data, n);
   ds_out_len += n;
   ds_out[ds_out_len++] = '\n';
   ds_flush_locked();
   pthread_mutex_unlock(&ds_lock);
}

int vnovel_dev_socket_poll(void (*on_line)(const char *line, void *userdata),
                           void *userdata)
{
   char     lines[DS_LINES_PER_POLL][DS_LINE_CAP];
   int      nlines = 0;
   int      closed = 0;

   pthread_mutex_lock(&ds_lock);
   if (ds_listen == DSOCK_BAD)
   {
      pthread_mutex_unlock(&ds_lock);
      return 0;
   }

   if (ds_peer == DSOCK_BAD)
   {
      dsock_t fd = accept(ds_listen, NULL, NULL);
      if (fd != DSOCK_BAD)
      {
         ds_set_nonblock(fd);
#if defined(SO_NOSIGPIPE)
         {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one,
                       sizeof one);
         }
#endif
         ds_peer = fd;
         /* Release whatever accumulated while nobody was attached. */
         ds_flush_locked();
         fprintf(stderr, "devsocket: consumer connected\n");
      }
   }

   while (ds_peer != DSOCK_BAD && nlines < DS_LINES_PER_POLL)
   {
      ssize_t n = recv(ds_peer, ds_in, (int)sizeof ds_in, 0);
      if (n == 0)
      {
         closed = 1;
         break;
      }
      if (n < 0)
      {
         if (!ds_would_block())
            closed = 2;
         break;
      }

      for (ssize_t i = 0; i < n; i++)
      {
         const char c = ds_in[i];
         if (c == '\n')
         {
            if (ds_line_len && on_line && nlines < DS_LINES_PER_POLL)
            {
               ds_line[ds_line_len] = 0;
               memcpy(lines[nlines], ds_line, ds_line_len + 1);
               nlines++;
            }
            ds_line_len = 0;
            continue;
         }
         if (c == '\r')
            continue; /* a consumer that speaks CRLF is not a special case */
         if (ds_line_len < DS_LINE_CAP - 1)
            ds_line[ds_line_len++] = c;
      }
   }

   if (closed)
   {
      fprintf(stderr, "devsocket: consumer %s\n",
              closed == 1 ? "closed" : "dropped");
      ds_disconnect();
   }
   pthread_mutex_unlock(&ds_lock);

   /* Hand the lines over OUTSIDE the lock: a handler that answers on the same
    * socket (or calls anything that does) must not deadlock against it. */
   for (int i = 0; i < nlines; i++)
      on_line(lines[i], userdata);
   return nlines;
}

#endif /* VNOVEL_DEVWIRE */
