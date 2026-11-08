/* stream -- a window's picture whose frames another program sends.
 *
 * Gitea #486, Blit ADR 0223. A program on the far side of the frames
 * module's listener (a game, through Blit's gamescope bridge) sends a
 * frame by descriptor -- a dmabuf, or a sealed memfd of RGB -- and the
 * console passes it on to Blit under the kitty id of the picture it placed
 * for that stream, without ever holding a pixel. What comes back, a
 * RELEASE or an ERROR, is put back in the producer's terms and handed to
 * whoever asked for the answers.
 *
 * Unheld, frames go on the console's own frame socket (cn_bfconn) and the
 * answers come back on it. Held, they go on a connection of their own to
 * hold's server (hf_api's open3), which counts every display's answer and
 * sends one per frame.
 *
 * And the console's wait services descriptors registered here (cn_fdhook*)
 * without returning to the script, so sixty frames a second and their
 * RELEASEs cost the desktop no frame of its own.
 */
#include "cn.h"
#include "../blitfr.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const hf_api *cn_hold(sh *s);

typedef struct cn_dst {
	unsigned sid;		/* the caller's number for it */
	unsigned cid;		/* the kitty id its picture has now, or 0 */
	/* The newest frame's header, a size the wire fixes (ADR 0223). */
	unsigned char h[BF_HDR3];
	size_t hn;
	int fds[BF_MAXFD];	/* and its descriptors, ours to close */
	int n;
} cn_dst;

/* Which stream a kitty id belonged to, kept after the picture has gone,
   since Blit releases its frames once the image is deleted. */
typedef struct cn_dmap {
	unsigned cid, sid;
} cn_dmap;

/* How many times one of a stream's frames went, under however many ids:
   a picture placed again under a new id is sent the newest frame again,
   and the old id's delete releases it too, so the producer is told once,
   when the last has come back. */
typedef struct cn_dout {
	unsigned sid;
	unsigned long long seq;
	int n;
} cn_dout;

typedef struct cn_hk {
	int fd;
	int (*fn)(void *ctx, int fd);
	void *ctx;
} cn_hk;

#ifndef CN_DMAPN
#define CN_DMAPN 64
#endif

static cn_dst *cn_dsts;
static size_t cn_ndst;
static cn_dmap *cn_dmaps;
static size_t cn_ndmap;
static cn_dout *cn_douts;
static size_t cn_ndout;
static void (*cn_dfn)(void *ctx, const char *line);
static void *cn_dctx;
static int cn_d3 = -1, cn_dbfhook = -1;
static cn_hk *cn_hks;
static size_t cn_nhk;

/* A stream's record, made when asked to be. */
cn_dst *cn_dget(unsigned sid, int make)
{
	size_t i;
	cn_dst *d;

	for (i = 0; i < cn_ndst; i++)
		if (cn_dsts[i].sid == sid)
			return &cn_dsts[i];
	if (!make)
		return 0;
	cn_dsts = xr(cn_dsts, (cn_ndst + 1) * sizeof *cn_dsts);
	d = &cn_dsts[cn_ndst++];
	memset(d, 0, sizeof *d);
	d->sid = sid;
	return d;
}

/* Let go of the descriptors a stream was keeping. */
void cn_dclose(cn_dst *d)
{
	int k;

	for (k = 0; k < d->n; k++)
		close(d->fds[k]);
	d->n = 0;
	d->hn = 0;
}

/* Remember which stream a kitty id is. */
void cn_dmapadd(unsigned cid, unsigned sid)
{
	size_t i;

	for (i = 0; i < cn_ndmap; i++)
		if (cn_dmaps[i].cid == cid) {
			cn_dmaps[i].sid = sid;
			return;
		}
	if (cn_ndmap == CN_DMAPN) {
		memmove(cn_dmaps, cn_dmaps + 1,
			(cn_ndmap - 1) * sizeof *cn_dmaps);
		cn_ndmap--;
	}
	cn_dmaps = xr(cn_dmaps, (cn_ndmap + 1) * sizeof *cn_dmaps);
	cn_dmaps[cn_ndmap].cid = cid;
	cn_dmaps[cn_ndmap].sid = sid;
	cn_ndmap++;
}

/* The stream a kitty id is, or 0. */
unsigned cn_dmapsid(unsigned cid)
{
	size_t i;

	for (i = 0; cid && i < cn_ndmap; i++)
		if (cn_dmaps[i].cid == cid)
			return cn_dmaps[i].sid;
	return 0;
}

/* A frame of a stream gone once more. */
void cn_doutadd(unsigned sid, unsigned long long seq)
{
	size_t i;

	for (i = 0; i < cn_ndout; i++)
		if (cn_douts[i].sid == sid && cn_douts[i].seq == seq) {
			cn_douts[i].n++;
			return;
		}
	if (cn_ndout == CN_DMAPN) {
		memmove(cn_douts, cn_douts + 1, (cn_ndout - 1) * sizeof *cn_douts);
		cn_ndout--;
	}
	cn_douts = xr(cn_douts, (cn_ndout + 1) * sizeof *cn_douts);
	cn_douts[cn_ndout].sid = sid;
	cn_douts[cn_ndout].seq = seq;
	cn_douts[cn_ndout].n = 1;
	cn_ndout++;
}

/* One of them released: 1 when it was the last, and the producer is told. */
int cn_doutrel(unsigned sid, unsigned long long seq)
{
	size_t i;

	for (i = 0; i < cn_ndout; i++)
		if (cn_douts[i].sid == sid && cn_douts[i].seq == seq) {
			if (--cn_douts[i].n > 0)
				return 0;
			cn_douts[i] = cn_douts[cn_ndout - 1];
			cn_ndout--;
			return 1;
		}
	return 1;
}

/* One answer from a display's Blit, put in the stream's terms: the id is
   the console's and becomes the caller's, everything after it stays. */
void cn_dans(const char *line)
{
	const char *p, *rest;
	char *e;
	unsigned long cid;
	unsigned sid;
	str o;

	if (!strncmp(line, "RELEASE ", 8))
		p = line + 8;
	else if (!strncmp(line, "ERROR ", 6))
		p = line + 6;
	else
		return;
	cid = strtoul(p, &e, 10);
	rest = e;
	sid = cn_dmapsid((unsigned)cid);
	if (!sid) {
		lg(HIBR_LDBG, "frames: an answer for no stream: %s", line);
		return;
	}
	if (p == line + 8 &&
	    !cn_doutrel(sid, strtoull(rest, 0, 10))) {
		lg(HIBR_LDBG, "frames: %s, still out under another id", line);
		return;
	}
	if (!cn_dfn)
		return;
	s_init(&o);
	s_add(&o, line, (size_t)(p - line));
	s_num(&o, (long)sid);
	s_cat(&o, rest);
	while (o.n && (o.p[o.n - 1] == '\n' || o.p[o.n - 1] == '\r'))
		o.n--;
	o.p[o.n] = 0;
	cn_dfn(cn_dctx, o.p);
	s_free(&o);
}

/* Readable: the console's own frame socket, nothing between it and Blit. */
int cn_dbfread(void *ctx, int fd)
{
	(void)ctx;
	(void)fd;
	if (cn_bfdrain() < 0) {
		cn_fdunhook(cn_dbfhook);
		cn_dbfhook = -1;
		return 1;
	}
	return 0;
}

/* Readable: the connection to hold's server that frames by dmabuf go on. */
int cn_d3read(void *ctx, int fd)
{
	sh *s = ctx;
	const hf_api *hf = cn_hold(s);
	str o;
	int r;

	s_init(&o);
	while (hf && (r = hf->answer(fd, &o)) != 0) {
		if (r < 0) {
			lg(HIBR_LDBG, "frames: hold's frame connection closed");
			cn_fdunhook(fd);
			close(fd);
			cn_d3 = -1;
			s_free(&o);
			return 1;
		}
		if (o.p) {
			s_ch(&o, 0);
			cn_dans(o.p);
		}
		o.n = 0;
	}
	s_free(&o);
	return 0;
}

/* Send a stream's newest frame under its picture's id. 1 when it went. */
int cn_dsend(sh *s, cn_dst *d)
{
	/* A header, a size the wire fixes (ADR 0223). */
	unsigned char h[BF_HDR3];
	const hf_api *hf;
	int fd, ok = 0;

	if (!d->cid || !d->hn || !d->n)
		return 0;
	memcpy(h, d->h, d->hn);
	bf_put32(h + 4, d->cid);
	if (cn_imgfd == 1) {
		hf = cn_hold(s);
		if (!hf)
			return 0;
		if (d->hn == BF_HDR)
			return hf->frame(s, h, d->fds[0]);
		if (cn_d3 < 0) {
			cn_d3 = hf->open3(s);
			if (cn_d3 < 0)
				return 0;
			cn_fdhook(cn_d3, cn_d3read, s);
		}
		ok = hf->frame3(cn_d3, h, d->fds, d->n);
	} else {
		fd = cn_bfconn();
		if (fd < 0)
			return 0;
		bf_answer = cn_dans;
		if (cn_dbfhook != fd) {
			cn_fdhook(fd, cn_dbfread, s);
			cn_dbfhook = fd;
		}
		if (d->hn == BF_HDR)
			ok = bf_send(fd, h, d->fds[0]);
		else
			ok = bf_send3(fd, h, d->fds, d->n);
	}
	if (ok && d->hn == BF_HDR3)
		cn_doutadd(d->sid, (unsigned long long)d->h[8] |
			   (unsigned long long)d->h[9] << 8 |
			   (unsigned long long)d->h[10] << 16 |
			   (unsigned long long)d->h[11] << 24 |
			   (unsigned long long)d->h[12] << 32 |
			   (unsigned long long)d->h[13] << 40 |
			   (unsigned long long)d->h[14] << 48 |
			   (unsigned long long)d->h[15] << 56);
	if (!ok)
		lg(HIBR_LDBG, "frames: stream %u's frame did not go", d->sid);
	return ok;
}

/* Whether frames by descriptor can go now: 2 with dmabufs, the formats
   every display takes in fmts and whether every one waits on a fence; 1
   with memfds only; 0 not at all. */
int cn_dcan(sh *s, str *fmts, int *fence)
{
	const hf_api *hf;

	*fence = 0;
	if (fmts)
		fmts->n = 0;
	if (!cn_fdon(s))
		return 0;
	if (cn_imgfd == 1) {
		str o;
		int r;

		hf = cn_hold(s);
		if (!hf)
			return 0;
		s_init(&o);
		r = hf->dmabuf(s, &o);
		if (r && fmts && o.p)
			s_add(fmts, o.p, o.n);
		s_free(&o);
		return r ? 2 : 1;
	}
	if (cn_bfconn() < 0)
		return 0;
	if (!bf_dmabuf)
		return 1;
	if (fmts)
		s_cat(fmts, bf_formats);
	*fence = bf_fence;
	return 2;
}

/* Keep a stream's picture in a pane's rectangle, from the window's own
   drawing; a picture that has a new id now is sent the newest frame. */
int cn_dplace(sh *s, const char *pane, int row, int col, int h, int w,
	      unsigned sid)
{
	unsigned cid = 0;
	cn_dst *d;

	if (!cn_imgdma(s, pane, row, col, h, w, sid, &cid))
		return 0;
	d = cn_dget(sid, 1);
	if (d->cid != cid) {
		d->cid = cid;
		cn_dmapadd(cid, sid);
		cn_dsend(s, d);
	}
	return 1;
}

/* Take a frame for a stream: kept as its newest, and sent at once when its
   picture is placed. 0 when frames by descriptor cannot go. */
int cn_dframe(sh *s, unsigned sid, const unsigned char *h, size_t hn,
	      const int *fds, int n)
{
	cn_dst *d;
	int k;

	if (!sid || !h || !fds)
		return 0;
	if (!(hn == BF_HDR3 && !memcmp(h, "FRM3", 4) && n >= 1 &&
	      n <= BF_MAXFD) &&
	    !(hn == BF_HDR && !memcmp(h, "FRM1", 4) && n == 1))
		return 0;
	if (!cn_fdon(s))
		return 0;
	d = cn_dget(sid, 1);
	cn_dclose(d);
	for (k = 0; k < n; k++) {
		d->fds[k] = fcntl(fds[k], F_DUPFD_CLOEXEC, 3);
		if (d->fds[k] < 0) {
			d->n = k;
			cn_dclose(d);
			return 0;
		}
	}
	d->n = n;
	memcpy(d->h, h, hn);
	d->hn = hn;
	/* Only to the picture as it is now: one dropped since (its pane drew
	   without it) has been deleted from Blit, and a frame under that id
	   would make an image there nothing deletes. The next place gives it
	   a new id and sends this frame then. */
	if (d->cid && cn_imgdmaid(sid) != d->cid)
		d->cid = 0;
	if (d->cid)
		cn_dsend(s, d);
	return 1;
}

/* A stream has ended: its picture goes, and what it kept. */
void cn_ddrop(sh *s, unsigned sid)
{
	cn_dst *d = cn_dget(sid, 0);
	size_t i;

	(void)s;
	cn_imgdmadrop(sid);
	if (!d)
		return;
	cn_dclose(d);
	i = (size_t)(d - cn_dsts);
	cn_dsts[i] = cn_dsts[cn_ndst - 1];
	cn_ndst--;
}

/* Where the answers go. */
void cn_danswers(void (*fn)(void *ctx, const char *line), void *ctx)
{
	cn_dfn = fn;
	cn_dctx = ctx;
}

/* Service a descriptor in the console's own wait. 1 when it was added. */
int cn_fdhook(int fd, int (*fn)(void *ctx, int fd), void *ctx)
{
	size_t i;

	if (fd < 0 || !fn)
		return 0;
	for (i = 0; i < cn_nhk; i++)
		if (cn_hks[i].fd == fd) {
			cn_hks[i].fn = fn;
			cn_hks[i].ctx = ctx;
			return 1;
		}
	cn_hks = xr(cn_hks, (cn_nhk + 1) * sizeof *cn_hks);
	cn_hks[cn_nhk].fd = fd;
	cn_hks[cn_nhk].fn = fn;
	cn_hks[cn_nhk].ctx = ctx;
	cn_nhk++;
	return 1;
}

/* And stop. */
void cn_fdunhook(int fd)
{
	size_t i;

	for (i = 0; i < cn_nhk; i++)
		if (cn_hks[i].fd == fd) {
			cn_hks[i] = cn_hks[cn_nhk - 1];
			cn_nhk--;
			return;
		}
}

/* Put every hooked descriptor in a wait's set; the highest there now. */
int cn_fdhookset(fd_set *r, int mx)
{
	size_t i;

	for (i = 0; i < cn_nhk; i++) {
		FD_SET(cn_hks[i].fd, r);
		if (cn_hks[i].fd > mx)
			mx = cn_hks[i].fd;
	}
	return mx;
}

/* Call each hook whose descriptor is readable; 1 when one asked for the
   script to wake. A hook may take itself or another away, so the list is
   walked from a copy of the descriptors that were ready. */
int cn_fdhookrun(fd_set *r)
{
	size_t i, n = 0, j;
	int *ready, wake = 0;

	if (!cn_nhk)
		return 0;
	ready = xm(cn_nhk * sizeof *ready);
	for (i = 0; i < cn_nhk; i++)
		if (FD_ISSET(cn_hks[i].fd, r))
			ready[n++] = cn_hks[i].fd;
	for (j = 0; j < n; j++)
		for (i = 0; i < cn_nhk; i++)
			if (cn_hks[i].fd == ready[j]) {
				if (cn_hks[i].fn(cn_hks[i].ctx, ready[j]))
					wake = 1;
				break;
			}
	free(ready);
	return wake;
}

/* Take away every hook whose descriptor has been closed under it. 1 when
   one was. */
int cn_fdhookprune(void)
{
	size_t i = 0;
	int any = 0;

	while (i < cn_nhk) {
		if (fcntl(cn_hks[i].fd, F_GETFD) < 0 && errno == EBADF) {
			lg(HIBR_LDBG, "console: hooked descriptor %d was closed",
			   cn_hks[i].fd);
			cn_hks[i] = cn_hks[cn_nhk - 1];
			cn_nhk--;
			any = 1;
			continue;
		}
		i++;
	}
	return any;
}

/* Whether anything is hooked, so a wait knows to loop. */
size_t cn_fdhookn(void)
{
	return cn_nhk;
}
