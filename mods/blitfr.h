#ifndef HIBR_BLITFR_H
#define HIBR_BLITFR_H

#include "hibr.h"

/* Picture frames handed to Blit by descriptor rather than written through
   the terminal (Blit ADR 0202, wire version 1). One copy of the protocol,
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
#define HF_API_VER 1
#endif
typedef struct hf_api {
	int (*frame)(sh *s, const unsigned char *h, int mfd);
} hf_api;

/* A connection to the frame socket of the session this environment names,
   after its hello was taken: a descriptor, close-on-exec, or -1 when there
   is no such session, it refused the token, or this is not Linux. */
int bf_open(void);

/* The same, for a socket and token given rather than read from the
   environment. */
int bf_dial(const char *sock, const char *tok);

/* A sealed memfd holding n bytes, or -1. */
int bf_memfd(const unsigned char *p, size_t n);

/* A whole frame's header: id's sequence-th picture, w by h pixels of RGB. */
void bf_hdr(unsigned char *h, unsigned id, unsigned long long seq, int w,
	    int ht);

/* Send one frame, a header and its memfd. 1 when it went. */
int bf_send(int fd, const unsigned char *h, int mfd);

/* Read whatever the far side has said since -- errors only, a line each --
   into the log, without waiting. -1 when the connection has gone. */
int bf_drain(int fd);

#endif
