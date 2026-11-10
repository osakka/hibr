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
#include <stdio.h>
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

/* Whether a placement at g (row, column, rows, columns) shows on a
   display at all: in part is enough, since each display is given its own
   slice of a picture that straddles two (Gitea #492). */
int hd_onclient(const struct hd_cli *cn, const int *g)
{
	return g[0] < cn->row + cn->rows && cn->row < g[0] + g[2] &&
	       g[1] < cn->col + cn->cols && cn->col < g[1] + g[3];
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
		if (!hd_onclient(cn, g))
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

/* Frames by dmabuf (Blit ADR 0223, version 3). Unlike a memfd, a dmabuf is
 * the producer's buffer and it wants it back, so each frame is kept as a
 * record until every display it went to has let it go, and only then is the
 * producer told, once. The newest of an image keeps its descriptors, for a
 * display that joins; an older one has only its list of who still holds it.
 * Nothing here maps a descriptor or reads a pixel.
 */
struct hd_f3 {
	unsigned id;
	unsigned long long seq;
	int prod, nfd, nv, old, placed;
	int fds[BF_MAXFD];
	int *views;
	unsigned char h[BF_HDR3];
};

static struct hd_f3 **hd_f3s;
static size_t hd_nf3;
/* Which producer each picture's input route was asked for by, the route it
   last asked for and whether it is on: a display whose frame socket opens
   after that -- one that joins mid-game, or one renewed onto new code,
   whose old connection took its routes with it -- is told both again
   (Gitea #491). */
static struct hd_in {
	unsigned id;
	int prod;
	str eis;
	int on;
	int hold;
	str mem;
} *hd_ins;
static size_t hd_nin;

/* Close a record's descriptors: the display that wants them has them. */
static void hd_f3shut(struct hd_f3 *f)
{
	int i;

	for (i = 0; i < f->nfd; i++)
		close(f->fds[i]);
	f->nfd = 0;
}

/* Tell the producer a frame is its own again, and forget it. */
static void hd_f3done(size_t i)
{
	struct hd_f3 *f = hd_f3s[i];
	str o;

	s_init(&o);
	s_cat(&o, "FRAME release ");
	s_num(&o, (long)f->id);
	s_ch(&o, ' ');
	s_num(&o, (long)f->seq);
	if (f->prod >= 0)
		hd_send(f->prod, HD_FRMANS, o.p, o.n);
	s_free(&o);
	hd_f3shut(f);
	free(f->views);
	free(f);
	hd_f3s[i] = hd_f3s[--hd_nf3];
}

/* A record nobody holds is finished once a newer frame has replaced it. */
static int hd_f3tidy(size_t i)
{
	if (hd_f3s[i]->old && !hd_f3s[i]->nv) {
		hd_f3done(i);
		return 1;
	}
	return 0;
}

/* Whether a frame by dmabuf is kept for an image. */
int hd_f3has(unsigned id)
{
	size_t i;

	for (i = 0; i < hd_nf3; i++)
		if (hd_f3s[i]->id == id && !hd_f3s[i]->old)
			return 1;
	return 0;
}

/* Keep a frame a producer sent, the descriptors the store's from here, and
   pass it to the displays that show its image. What it replaces is let go
   as the displays say so. */
void hd_f3put(vec *cls, int tid, int prod, const unsigned char *h,
	      const int *fds, int n)
{
	unsigned id = (unsigned)hd_frmnum(h + 4, 4);
	unsigned long long seq = hd_frmnum(h + 8, 8);
	unsigned planes = (unsigned)hd_frmnum(h + 28, 4);
	struct hd_f3 *f;
	size_t i, c;
	int g[4], k;

	if (memcmp(h, "FRM3", 4) || !id || planes < 1 || planes > 4 ||
	    (unsigned)n < planes || (unsigned)n > planes + 1) {
		for (k = 0; k < n; k++)
			close(fds[k]);
		hd_send(prod, HD_FRMANS, "FRAME error 0 EINVAL", 20);
		return;
	}
	for (i = hd_nf3; i--;)
		if (hd_f3s[i]->id == id && !hd_f3s[i]->old) {
			hd_f3s[i]->old = 1;
			hd_f3shut(hd_f3s[i]);
			hd_f3tidy(i);
		}
	f = xm(sizeof *f);
	memset(f, 0, sizeof *f);
	f->id = id;
	f->seq = seq;
	f->prod = prod;
	f->nfd = n;
	memcpy(f->fds, fds, sizeof(int) * (size_t)n);
	memcpy(f->h, h, BF_HDR3);
	hd_f3s = xr(hd_f3s, (hd_nf3 + 1) * sizeof *hd_f3s);
	hd_f3s[hd_nf3++] = f;
	hd_fgen++;
	if (!hd_frmplaced(tid, id, g))
		return;
	for (c = 0; c < cls->n; c++) {
		struct hd_cli *cn = cls->p[c];

		if (!hd_onclient(cn, g))
			continue;
		hd_f3send(cn, id);
	}
}

/* Send a display an image's newest frame, unless it has it already. A
   display that cannot import is the producer's to hear about: it falls back
   to memfds for the connection. */
void hd_f3send(struct hd_cli *cn, unsigned id)
{
	struct hd_f3 *f = 0;
	size_t i, j;
	int k;

	for (i = 0; i < hd_nf3; i++)
		if (hd_f3s[i]->id == id && !hd_f3s[i]->old)
			f = hd_f3s[i];
	if (!f || !f->nfd || !cn->fdok)
		return;
	if (!cn->dma) {
		str o;

		s_init(&o);
		s_cat(&o, "FRAME error ");
		s_num(&o, (long)id);
		s_cat(&o, " ENOTSUP");
		if (f->prod >= 0)
			hd_send(f->prod, HD_FRMANS, o.p, o.n);
		s_free(&o);
		return;
	}
	for (k = 0; k < f->nv; k++)
		if (f->views[k] == cn->fd)
			return;
	for (j = 0; j < cn->fsn; j++)
		if (cn->fsid[j] == id)
			break;
	if (!hd_sendfds(cn->fd, HD_FRM3, (const char *)f->h, BF_HDR3, f->fds, f->nfd)) {
		lg(HIBR_LDBG, "hold: a dmabuf frame did not reach a display");
		return;
	}
	f->views = xr(f->views, sizeof(int) * (size_t)(f->nv + 1));
	f->views[f->nv++] = cn->fd;
	if (j == cn->fsn) {
		cn->fsid = xr(cn->fsid, (cn->fsn + 1) * sizeof *cn->fsid);
		cn->fsseq = xr(cn->fsseq, (cn->fsn + 1) * sizeof *cn->fsseq);
		cn->fsid[j] = id;
		cn->fsn++;
	}
	cn->fsseq[j] = f->seq;
}

/* Take a display out of a record's list. */
static void hd_f3unview(struct hd_f3 *f, int cfd)
{
	int k;

	for (k = 0; k < f->nv; k++)
		if (f->views[k] == cfd) {
			f->views[k] = f->views[--f->nv];
			return;
		}
}

/* A display's Blit said something about a frame. An ERROR goes straight to
   the producer, whose buffer it is again from there. */
void hd_f3ans(int cfd, const char *line)
{
	unsigned long id = 0;
	unsigned long long seq = 0;
	size_t i;

	if (sscanf(line, "FRAME release %lu %llu", &id, &seq) == 2) {
		for (i = hd_nf3; i--;)
			if (hd_f3s[i]->id == id && hd_f3s[i]->seq == seq) {
				hd_f3unview(hd_f3s[i], cfd);
				hd_f3tidy(i);
			}
		return;
	}
	if (sscanf(line, "FRAME error %lu", &id) != 1)
		return;
	for (i = hd_nf3; i--;)
		if (hd_f3s[i]->id == id) {
			if (hd_f3s[i]->prod >= 0)
				hd_send(hd_f3s[i]->prod, HD_FRMANS, line, strlen(line));
			break;
		}
	for (i = hd_nf3; i--;)
		if (hd_f3s[i]->id == id) {
			hd_f3unview(hd_f3s[i], cfd);
			hd_f3tidy(i);
		}
}

/* A display went: nothing of the producer's is waiting on it any more. */
void hd_f3client(int cfd)
{
	size_t i;

	for (i = hd_nf3; i--;) {
		hd_f3unview(hd_f3s[i], cfd);
		hd_f3tidy(i);
	}
}

/* A producer went: its frames have nobody to go back to. */
void hd_f3prod(int fd)
{
	size_t i;

	for (i = hd_nin; i--;)
		if (hd_ins[i].prod == fd) {
			s_free(&hd_ins[i].eis);
			s_free(&hd_ins[i].mem);
			hd_ins[i] = hd_ins[--hd_nin];
		}
	for (i = hd_nf3; i--;)
		if (hd_f3s[i]->prod == fd) {
			hd_f3s[i]->prod = -1;
			hd_f3s[i]->old = 1;
			hd_f3shut(hd_f3s[i]);
			hd_f3tidy(i);
		}
}

/* The picture a line names: its third word, on every picture line of
   version 5. */
unsigned long hd_inid(const char *line, size_t n)
{
	size_t i = 0;
	int sp = 0;

	while (i < n && sp < 2)
		if (line[i++] == ' ')
			sp++;
	return i < n ? strtoul(line + i, 0, 10) : 0;
}

/* An input route's line from a producer (Blit 0.3.64, Gitea #486): who
   asked is remembered by picture, and the line goes to every display that
   can be sent frames, each saying it to its own Blit. With none, the
   producer is told at once, as Blit would tell it. */
void hd_inputput(vec *cls, int prod, const char *line, size_t n)
{
	unsigned long id = hd_inid(line, n);
	size_t i;
	int sent = 0;
	str o;

	if (!id || n < 9 || memcmp(line, "INPUT ", 6))
		return;
	/* A producer's own line comes after any replay, so whatever a
	   display's Blit has not answered of a replay by now it never will:
	   counted on, it would swallow the answer to this one. */
	for (i = 0; i < cls->n; i++)
		((struct hd_cli *)cls->p[i])->inrep = 0;
	for (i = 0; i < hd_nin && hd_ins[i].id != id; i++)
		;
	if (i == hd_nin) {
		hd_ins = xr(hd_ins, (hd_nin + 1) * sizeof *hd_ins);
		memset(&hd_ins[i], 0, sizeof hd_ins[i]);
		s_init(&hd_ins[i].eis);
		s_init(&hd_ins[i].mem);
		hd_nin++;
	}
	hd_ins[i].id = (unsigned)id;
	hd_ins[i].prod = prod;
	if ((n > 10 && !memcmp(line, "INPUT eis ", 10)) ||
	    (n > 11 && !memcmp(line, "INPUT sock ", 11))) {
		hd_ins[i].eis.n = 0;
		s_add(&hd_ins[i].eis, line, n);
	} else if (n > 9 && !memcmp(line, "INPUT on ", 9)) {
		hd_ins[i].on = 1;
	} else if (n > 10 && !memcmp(line, "INPUT off ", 10)) {
		hd_ins[i].on = 0;
	} else if (n > 13 && !memcmp(line, "INPUT member ", 13)) {
		hd_ins[i].mem.n = 0;
		s_add(&hd_ins[i].mem, line, n);
	} else if (n > 11 && !memcmp(line, "INPUT hold ", 11)) {
		hd_ins[i].hold = n >= 7 && !memcmp(line + n - 7, "confine", 7) ? 2 : 1;
	}
	for (i = 0; i < cls->n; i++) {
		struct hd_cli *cn = cls->p[i];

		if (cn->fdok && hd_send(cn->fd, HD_INPUT, line, n))
			sent = 1;
	}
	if (sent)
		return;
	s_init(&o);
	s_cat(&o, "INPUT error ");
	s_num(&o, (long)id);
	s_cat(&o, " ENOTSUP");
	hd_send(prod, HD_FRMANS, o.p, o.n);
	s_free(&o);
}

/* A display that can now be sent frames is told every route there is: the
   EIS socket, and on where the producer turned it on. Its Blit's answers to
   these are its own and go nowhere. */
void hd_inreplay(struct hd_cli *cn)
{
	size_t i;
	str o;

	if (!cn->fdok)
		return;
	s_init(&o);
	for (i = 0; i < hd_nin; i++) {
		if (!hd_ins[i].eis.n)
			continue;
		if (hd_send(cn->fd, HD_INPUT, hd_ins[i].eis.p, hd_ins[i].eis.n))
			cn->inrep++;
		if (!hd_ins[i].on)
			continue;
		o.n = 0;
		s_cat(&o, "INPUT on ");
		s_num(&o, (long)hd_ins[i].id);
		if (hd_send(cn->fd, HD_INPUT, o.p, o.n))
			cn->inrep++;
	}
	for (i = 0; i < hd_nin; i++) {
		if (!hd_ins[i].mem.n)
			continue;
		if (hd_send(cn->fd, HD_INPUT, hd_ins[i].mem.p, hd_ins[i].mem.n))
			cn->inrep++;
	}
	for (i = 0; i < hd_nin; i++) {
		if (!hd_ins[i].eis.n || !hd_ins[i].hold)
			continue;
		o.n = 0;
		s_cat(&o, "INPUT hold ");
		s_num(&o, (long)hd_ins[i].id);
		s_cat(&o, hd_ins[i].hold == 2 ? " confine" : " relative");
		if (hd_send(cn->fd, HD_INPUT, o.p, o.n))
			cn->inrep++;
	}
	if (cn->inrep)
		lg(HIBR_LDBG, "hold: a display was told %d input route line(s) again",
		   cn->inrep);
	s_free(&o);
}

/* What a display's Blit said of an input route. 1 when the line was one:
   the producer is told only the primary display's answer, since every
   display's Blit answers for its own route and only the focused session's
   is live; with no primary marked, each goes. */
int hd_inputans(vec *cls, int cfd, const char *line)
{
	struct hd_cli *pc;
	unsigned long id;
	size_t i, n = strlen(line);

	if (strncmp(line, "INPUT ok ", 9) && strncmp(line, "INPUT error ", 12))
		return 0;
	pc = hd_cfind(cls, cfd);
	if (pc && pc->inrep > 0) {
		pc->inrep--;
		lg(HIBR_LDBG, "hold: a replayed route's answer: %s", line);
		return 1;
	}
	pc = hd_cprimary(cls);
	if (pc && pc->fd != cfd) {
		lg(HIBR_LDBG, "hold: a display's input answer kept back: %s", line);
		return 1;
	}
	id = hd_inid(line, n);
	for (i = 0; i < hd_nin; i++)
		if (hd_ins[i].id == id) {
			hd_send(hd_ins[i].prod, HD_FRMANS, line, n);
			break;
		}
	return 1;
}

/* An image the emulator no longer holds takes its newest frame with it. */
void hd_f3sweep(int tid)
{
	int g[4];
	size_t i;

	for (i = hd_nf3; i--;) {
		struct hd_f3 *f = hd_f3s[i];

		if (hd_frmplaced(tid, f->id, g))
			f->placed = 1;
		else if (f->placed && !f->old) {
			f->old = 1;
			hd_f3shut(f);
			hd_f3tidy(i);
		}
	}
}

/* An image placed by descriptor, its size in pixels from its newest frame:
   both headers keep the width and height at the same place. */
int hd_frmpx(unsigned id, int *pw, int *ph)
{
	const unsigned char *h = 0;
	struct hd_frm *f;
	size_t i;

	for (i = hd_nf3; i--;)
		if (hd_f3s[i]->id == id && !hd_f3s[i]->old) {
			h = hd_f3s[i]->h;
			break;
		}
	if (!h && (f = hd_frmget(id)))
		h = f->h;
	if (!h)
		return 0;
	*pw = (int)hd_frmnum(h + 16, 4);
	*ph = (int)hd_frmnum(h + 20, 4);
	return *pw > 0 && *ph > 0;
}
