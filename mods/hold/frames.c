#define _GNU_SOURCE
/* frames -- picture frames by descriptor, kept by the server (Blit ADR 0202).
 *
 * A held desktop's console hands its server each window picture's newest
 * frame as a sealed memfd, one connection a frame. The server keeps the
 * newest per image and passes it to every display whose client has Blit's
 * frame socket open; the placement itself stays in the program's own
 * output, an a=p the emulator keeps like any other picture. So the pixels
 * have one keeper (here) and the placement another (the emulator), which
 * is the same split Blit has: socket and terminal.
 *
 * A frame is kept until the emulator, having had its image, no longer
 * does -- the program deleted it, or the screen was cleared -- and a frame
 * whose placement never comes is let go once HD_FRMMAX are kept.
 */
#include "hd.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef HD_FRMMAX
#define HD_FRMMAX 32
#endif

static struct hd_frm *hd_frms;
static size_t hd_nfrm, hd_frmcap;
unsigned hd_fgen;

/* A little-endian number out of a header. */
unsigned long long hd_frmnum(const unsigned char *p, int n)
{
	unsigned long long v = 0;

	while (n--)
		v = (v << 8) | p[n];
	return v;
}

/* Let one kept frame go. */
void hd_frmdrop(size_t i)
{
	if (i >= hd_nfrm)
		return;
	close(hd_frms[i].fd);
	hd_frms[i] = hd_frms[hd_nfrm - 1];
	hd_nfrm--;
}

/* Keep a frame a program handed over: the newest for its image, a stale
   one closed and forgotten. The descriptor is the store's from here. */
void hd_frmput(const unsigned char *h, int fd)
{
	unsigned id = (unsigned)hd_frmnum(h + 4, 4);
	unsigned long long seq = hd_frmnum(h + 8, 8);
	size_t i;

	if (memcmp(h, "FRM1", 4) || !id) {
		close(fd);
		return;
	}
	for (i = 0; i < hd_nfrm; i++)
		if (hd_frms[i].id == id)
			break;
	if (i < hd_nfrm) {
		if (seq <= hd_frms[i].seq) {
			close(fd);
			return;
		}
		close(hd_frms[i].fd);
	} else {
		if (hd_nfrm >= HD_FRMMAX) {
			lg(HIBR_LDBG, "hold: %u frames kept, one never placed let go",
			   (unsigned)hd_nfrm);
			hd_frmdrop(0);
		}
		if (hd_nfrm == hd_frmcap) {
			size_t nc = hd_frmcap ? hd_frmcap * 2 : 8;
			struct hd_frm *na = xm(nc * sizeof *na);

			if (hd_nfrm)
				memcpy(na, hd_frms, hd_nfrm * sizeof *na);
			free(hd_frms);
			hd_frms = na;
			hd_frmcap = nc;
		}
		i = hd_nfrm++;
		memset(&hd_frms[i], 0, sizeof hd_frms[i]);
	}
	hd_frms[i].id = id;
	hd_frms[i].seq = seq;
	hd_frms[i].fd = fd;
	memcpy(hd_frms[i].h, h, BF_HDR);
	hd_fgen++;
}

/* The frame kept for an image, or none. */
struct hd_frm *hd_frmget(unsigned id)
{
	size_t i;

	for (i = 0; i < hd_nfrm; i++)
		if (hd_frms[i].id == id)
			return &hd_frms[i];
	return 0;
}

/* Let go of every frame whose image the emulator had and no longer has. */
void hd_frmsweep(int tid)
{
	str pic;
	size_t i = 0;
	int k, n, r, c, rows, cols, there;

	if (!hd_nfrm || !hd_tm->images)
		return;
	n = hd_tm->images(tid);
	s_init(&pic);
	while (i < hd_nfrm) {
		there = 0;
		for (k = 0; k < n && !there; k++) {
			pic.n = 0;
			if (hd_tm->image(tid, k, &r, &c, &rows, &cols, &pic) &&
			    hd_imgid(pic.p, pic.n) == hd_frms[i].id)
				there = 1;
		}
		if (there) {
			hd_frms[i].seen = 1;
		} else if (hd_frms[i].seen) {
			hd_frmdrop(i);
			continue;
		}
		i++;
	}
	s_free(&pic);
}

/* Let go of every frame: the session is ending. */
void hd_frmclear(void)
{
	while (hd_nfrm)
		hd_frmdrop(hd_nfrm - 1);
}

/* Whether the far end of a connection is this server's own user. A frame
   is a descriptor handed into this process, so a connection from anyone
   else -- which the private socket directory already refuses -- is
   refused here as well. */
int hd_frmpeer(int c)
{
#ifdef SO_PEERCRED
	struct ucred u;
	socklen_t l = sizeof u;

	if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &u, &l) < 0)
		return 0;
	return u.uid == getuid();
#else
	(void)c;
	return 1;
#endif
}

/* Whether the emulator holds a placement by descriptor for this image --
   an a=p it kept, which is what a window picture sent by frames is -- and
   where: g gets its row, column, rows and columns. */
int hd_frmplaced(int tid, unsigned id, int *g)
{
	str pic;
	int k, n, yes = 0;

	if (!hd_tm->images)
		return 0;
	n = hd_tm->images(tid);
	s_init(&pic);
	for (k = 0; k < n && !yes; k++) {
		pic.n = 0;
		if (hd_tm->image(tid, k, &g[0], &g[1], &g[2], &g[3], &pic) &&
		    pic.n > 5 && !memcmp(pic.p, "\033_Ga=p", 6) &&
		    hd_imgid(pic.p, pic.n) == id)
			yes = 1;
	}
	s_free(&pic);
	return yes;
}

/* A frame has just been kept: every display that can take one and shows
   the picture has it at once if its placement is already out -- an image
   whose pixels change while it stays where it is writes nothing on the
   terminal, so nothing else would send it. A display the picture is not
   on is sent neither the frame nor the placement, by the same rule the
   render pass keeps (render.c), so a video on one display costs the
   others nothing. One not placed yet goes with its placement. */
void hd_frmpush(vec *cls, int tid, const unsigned char *h)
{
	unsigned id = (unsigned)hd_frmnum(h + 4, 4);
	int g[4];
	size_t i;

	if (!hd_frmplaced(tid, id, g))
		return;
	for (i = 0; i < cls->n; i++) {
		struct hd_cli *cn = cls->p[i];

		/* A frame is a kitty picture's pixels: a display that draws
		   another kind, or none, is not sent one, as the render pass's
		   hd_canshow would not send it the placement either. */
		if (cn->gfx != GX_KITTY && cn->gfx != GX_UNASKED)
			continue;
		if (g[0] < cn->row || g[1] < cn->col ||
		    g[0] + g[2] > cn->row + cn->rows ||
		    g[1] + g[3] > cn->col + cn->cols)
			continue;
		hd_frmsend(cn, id);
	}
}

/* Send a display the frames it has not had, for the images this frame of
   the session holds that are placed by descriptor: before the placement
   goes out, so Blit has the pixels when the a=p arrives. Only to a display
   whose client said it can take them. */
void hd_frmsend(struct hd_cli *cn, unsigned id)
{
	struct hd_frm *f = hd_frmget(id);
	size_t j;

	if (!f || !cn->fdok)
		return;
	for (j = 0; j < cn->fsn; j++)
		if (cn->fsid[j] == id)
			break;
	if (j < cn->fsn && cn->fsseq[j] >= f->seq)
		return;
	if (!hd_sendfd(cn->fd, HD_FRAME, (const char *)f->h, BF_HDR, f->fd)) {
		lg(HIBR_LDBG, "hold: a frame did not reach a display");
		return;
	}
	if (j == cn->fsn) {
		cn->fsid = xr(cn->fsid, (cn->fsn + 1) * sizeof *cn->fsid);
		cn->fsseq = xr(cn->fsseq, (cn->fsn + 1) * sizeof *cn->fsseq);
		cn->fsid[j] = id;
		cn->fsn++;
	}
	cn->fsseq[j] = f->seq;
}
