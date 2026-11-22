#ifndef HIBR_FDMSG_H
#define HIBR_FDMSG_H

#include "hibr.h"

/* Messages with descriptors between two processes of one desktop (ADR
   0050, the zygote): a Unix stream socket pair, each message a header --
   its length and how many descriptors ride with it -- and its bytes, the
   descriptors attached to the header's first byte as SCM_RIGHTS. Shared by
   the pty module (a terminal's program the desktop owns, its master handed
   to the app's process) and the zygote module (a fresh app process asked
   for, its pty channel handed with the request), compiled into both as
   procs.c is, and modelled on hold's wire.

   A stream rather than SOCK_SEQPACKET on purpose: a sequenced-packet
   socket closed with unread data resets its peer, whose next read fails
   before it reaches what is still queued (CLAUDE.md, Gitea #560), where a
   stream's reader takes every byte first and the end after; and macOS has
   no SOCK_SEQPACKET for a Unix socket at all. Descriptors received are
   close-on-exec, so nothing a process starts inherits them. */

/* The most descriptors one message carries: the header's count is one
   byte, and no message here needs more than one. */
#ifndef FM_MAXFD
#define FM_MAXFD 4
#endif

typedef struct fm_ch fm_ch;
struct fm_ch {
	int fd;
	/* Bytes read and not yet a whole message. */
	str in;
	/* Descriptors received and not yet handed with their message, in
	   the order they came. */
	vec fq;
	int gone;
};

/* A connected pair, both ends close-on-exec. 1, or 0 with errno set. */
int fm_pair(int *a, int *b);
/* One message: n bytes and nf descriptors, waiting at most ms for room
   when the peer has not read. 1 when it went, 0 when it did not (the peer
   gone, or no room in ms), never a signal. */
int fm_send(int fd, const char *t, size_t n, const int *fds, int nf, int ms);
/* A channel's reader, and its end. */
fm_ch *fm_open(int fd);
void fm_close(fm_ch *c, int closefd);
/* Whatever has arrived, never waiting: 1 when bytes came, 0 none, -1
   when the peer has gone (whatever arrived before is still taken). */
int fm_take(fm_ch *c);
/* The next whole message, its descriptors into fds (at most FM_MAXFD,
   *nf set): 1 when there was one, 0 when none is whole yet. A message
   whose descriptors did not arrive is handed with none. */
int fm_next(fm_ch *c, str *out, int *fds, int *nf);
/* Wait at most ms for something to read: 1 readable, 0 not in ms. */
int fm_wait(fm_ch *c, int ms);
/* Make a descriptor close-on-exec. */
void fm_cloexec(int fd);
/* Milliseconds on a clock no one sets. */
long fm_ms(void);
/* Close every descriptor above 2 but keep (n of them), with no
   allocation: for a child just forked from a process that may hold
   threads, where malloc may be unusable. */
void fm_closeall(const int *keep, int n);

#endif
