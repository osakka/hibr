#ifndef HIBR_BLITFR_H
#define HIBR_BLITFR_H

#include "hibr.h"

/* Picture frames handed to Blit by descriptor rather than written through
   the terminal (Blit ADR 0202, wire version 2 where Blit speaks it, 1
   where it does not). One copy of the protocol,
   for the two places that speak it: the console, when it draws straight
   onto a Blit session, and hold's attaching client, which forwards the
   frames a held desktop gives its server.

   The pixels go as a memfd sealed against shrinking, growing and writing,
   over an abstract-namespace SOCK_SEQPACKET socket named by the session's
   BLIT_FRAME_SOCKET and opened with its BLIT_FRAME_TOKEN. Placement,
   layering and deletion stay ordinary kitty escapes under the same image
   id, so the terminal stream still says where a picture is and Blit only
   learns its pixels this way. No name is ever opened by Blit.

   A frame's header is 48 bytes, little-endian:

     0 "FRM1"   4 id   8 sequence (u64, rising per id)
     16 width  20 height  24 format (24 RGB)  28 stride
     32 x  36 y  40 w  44 h   (w = h = 0: the whole image)

   Linux only: memfd and SCM_RIGHTS over this socket are what it is, and
   elsewhere every call says no and everything stays on the terminal. */

#ifndef BF_HDR
#define BF_HDR 48
#endif

/* What hold offers a program inside it, as "holdframes": hand the session's
   server one frame, which it keeps and passes to each display's client.
   1 when it went. */
#ifndef HF_API_VER
#define HF_API_VER 4
#endif
/* The header of a frame by dmabuf (Blit ADR 0223, version 3), and how many
   descriptors one may carry: four planes and an acquire fence. */
#ifndef BF_HDR3
#define BF_HDR3 72
#endif
#ifndef BF_MAXFD
#define BF_MAXFD 5
#endif
typedef struct hf_api {
	int (*frame)(sh *s, const unsigned char *h, int mfd);
	/* Version 2 (Blit ADR 0203): hand the server the panel's status, the
	   n bytes at p, a line each, for every display whose Blit asked; and
	   whether any display's Blit is asking, 1 or 0. */
	int (*status)(sh *s, const char *p, size_t n);
	int (*hud)(sh *s);
	/* Version 3 (Blit ADR 0223): a connection of its own to the session's
	   server for frames by dmabuf, or -1, read for what comes back, each
	   "RELEASE <id> <seq>" or "ERROR <id> <code>" by answer; one frame on
	   it, its header and n descriptors, which stay the caller's; the next
	   line that came back into o, 1 with one, 0 with none yet, -1 when the
	   connection has gone; and whether every display can import, 1 with
	   the formats they all take in o. */
	int (*open3)(sh *s);
	int (*frame3)(int fd, const unsigned char *h, const int *fds, int n);
	int (*answer)(int fd, str *o);
	int (*dmabuf)(sh *s, str *o);
	/* Version 4 (Blit 0.3.64, Gitea #486): an input route's line on the
	   same connection, to every display's Blit; its answer comes back by
	   answer, the primary display's alone. */
	int (*input)(int fd, const char *line);
} hf_api;

/* The performance panel's status (Blit ADR 0203): at most this many lines
   of at most this many bytes each. */
#ifndef BF_STLINES
#define BF_STLINES 8
#endif
#ifndef BF_STBYTES
#define BF_STBYTES 64
#endif

/* A connection to the frame socket of the session this environment names,
   after its hello was taken: a descriptor, close-on-exec, or -1 when there
   is no such session, it refused the token, or this is not Linux. */
int bf_open(void);

/* The same, for a socket and token given rather than read from the
   environment. */
int bf_dial(const char *sock, const char *tok);

/* What the last hello was answered about dmabufs: 1 when the far side
   imports them, with its list of formats in bf_formats, and whether it
   waits on a fence. */
extern int bf_dmabuf, bf_fence;
extern char bf_formats[];

/* What a RELEASE or an ERROR said is handed here when it is set, and
   otherwise logged. */
extern void (*bf_answer)(const char *line);

/* One frame by dmabuf: a header of BF_HDR3 and its descriptors. 1 when it
   went, or was dropped on a full socket. */
int bf_send3(int fd, const unsigned char *h, const int *fds, int n);

/* One line of the wire's text, an INPUT request (Blit 0.3.64), as a
   message of its own. 1 when it went. */
int bf_say(int fd, const char *line);

/* A 32-bit number into four bytes, little-endian, as every field of the
   wire is. */
void bf_put32(unsigned char *p, unsigned long v);

/* A sealed memfd holding n bytes, or -1. */
int bf_memfd(const unsigned char *p, size_t n);

/* A whole frame's header: id's sequence-th picture, w by h pixels of RGB. */
void bf_hdr(unsigned char *h, unsigned id, unsigned long long seq, int w,
	    int ht);

/* Send one frame, a header and its memfd. 1 when it went. */
int bf_send(int fd, const unsigned char *h, int mfd);

/* Read whatever the far side has said since, a line each, without
   waiting: errors into the log, and on wire version 2 Blit's "HUD1 on" or
   "HUD1 off" -- whether its performance panel wants status -- into *hud
   as 1 or 0 when hud is not null. -1 when the connection has gone, or Blit
   said EACCES: the person it was opened for has signed out. */
int bf_drain(int fd, int *hud);

/* Send the panel its status, the lines of the n bytes at p (see
   bf_status in blitfr.c for what is cut). 1 when it went. */
int bf_status(int fd, const char *p, size_t n);

#endif
