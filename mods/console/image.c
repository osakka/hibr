/* image -- a picture on the cell grid, as pixels the terminal paints.
 *
 * A bitmap is not cells: it is laid over a rectangle, and the grid knows
 * nothing about it. Drawing one straight at the screen is what this module's
 * own README forbids -- it corrupts whatever the diff writes next -- so a
 * picture is a *region* the console owns, and the ordinary diff is what
 * keeps it honest:
 *
 *   - A region is placed by cn_image: the pixels are encoded once and the
 *     bytes kept, with a hash of the cells underneath it as it was placed.
 *   - A flush emits a region's bytes only when they are new, so a still
 *     picture costs nothing a frame.
 *   - A flush skips the cells a live region covers, and leaves the front
 *     grid's idea of them alone, so a window clearing its own face every
 *     frame (which every window does, before drawing the picture again)
 *     does not paint over it.
 *   - When the cells underneath stop matching that hash -- the window moved,
 *     closed, another window came over it -- the region is dropped and its
 *     cells are invalidated, so the next flush paints the text that belongs
 *     there. Nothing has to tell the console a picture has gone.
 *
 * There are two encoders, and the difference between them is the one thing
 * a reader has to hold on to. A sixel (sixel.c) is paint: once it has gone
 * out the terminal has forgotten where it came from, and writing over those
 * cells is what removes it. A kitty picture (kitty.c) is an object with an
 * id: it stays above the text until it is deleted, so every region carries
 * its id and every path that drops or forgets one owes the terminal a
 * delete, which is what cn_kdel collects and cn_imgdels sends -- before the
 * diff, so the text underneath is painted in the same frame.
 *
 * A backend that could place pixels itself (a framebuffer, Blit) would
 * implement the same dp_api entry and never come here.
 */
#include "cn.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cn_img {
	int row, col, h, w;	/* where it is, in screen cells, cropped to them */
	int wrow, wcol, wh, ww;	/* what the caller asked for, which may hang off the screen */
	unsigned sum;		/* of the pixels, so an unchanged frame is not re-encoded */
	unsigned under;		/* of the cells it covers, as they were when it was placed */
	int sent;		/* 0 until its bytes have gone out */
	int over;		/* text is drawn over this one, so it owns no cells */
	int live;		/* placed again this frame (an `over` one only) */
	unsigned above;		/* of the cells over it, as they were when sent */
	unsigned id;		/* the kitty image's own id; 0 for a sixel */
	str data;		/* the encoded picture */
} cn_img;

static cn_img *cn_imgs;
static size_t cn_nimg, cn_imgcap;
static str cn_kdel;

/* A kitty picture that has gone owes the terminal a delete. */
void cn_imgowe(unsigned id)
{
	if (!id)
		return;
	kt_del(&cn_kdel, id);
}

/* FNV-1a, for "is this the same picture" and "are the cells under it still
   the ones it was placed over". Nothing keeps one of these across processes,
   so what it mixes is free to change -- which is why the big case reads eight
   bytes at a time and the small one does not.
 *
 * A screenful of pixels at 232x71 with an 8x16 cell is 6.03 MB, and a byte at
 * a time under tcc is 14.9 ms of it; the 64-bit form over whole words is
 * 3.45 ms (Gitea #113). But the same change made a four-byte call 10.2 ns
 * instead of 13.1, and cn_imgunder makes one per field per cell -- 49,416 of
 * them a frame for a full-screen sixel wallpaper -- so a word at a time
 * everywhere would have bought 11 ms on a placement, which the keep has made
 * rare, and paid 0.15 ms on every frame, which is a fifth of the desktop's
 * whole frame budget. Hence CN_HASHWORD: under it, byte for byte the code
 * that was there. Measure the small calls as well as the big one before
 * touching this again. */
#define CN_HASHWORD 64

unsigned cn_hash(const void *p, size_t n, unsigned h)
{
	const unsigned char *b = p;
	unsigned long long g;

	if (n < CN_HASHWORD) {
		while (n--) {
			h ^= *b++;
			h *= 16777619u;
		}
		return h;
	}
	g = h;
	while ((uintptr_t)b & 7) {
		g ^= *b++;
		g *= 1099511628211ull;
		n--;
	}
	while (n >= 8) {
		g ^= *(const unsigned long long *)b;
		g *= 1099511628211ull;
		b += 8;
		n -= 8;
	}
	while (n--) {
		g ^= *b++;
		g *= 1099511628211ull;
	}
	return (unsigned)(g ^ (g >> 32));
}

/* What the cells a region covers come to now. */
unsigned cn_imgunder(const cn_img *im)
{
	unsigned h = 2166136261u;
	int r, c;

	for (r = im->row; r < im->row + im->h && r < cn_back.rows; r++)
		for (c = im->col; c < im->col + im->w && c < cn_back.cols; c++) {
			cn_cell *k = &cn_back.c[r * cn_back.cols + c];
			unsigned v[3];

			/* The three in one call rather than one call each:
			   a cn_cell has a pointer between cp and fg, so they
			   are not a contiguous run to hash in place, and three
			   calls of four bytes cost 30.6 ns against this one
			   of twelve at 22. Per cell, every frame, for every
			   region that owns its cells. */
			v[0] = k->cp;
			v[1] = k->fg;
			v[2] = k->bg;
			h = cn_hash(v, sizeof v, h);
		}
	return h;
}

/* Make the front grid forget a region's cells, so the next flush paints the
   text that is really there. */
void cn_imgforget(const cn_img *im)
{
	int r, c;

	for (r = im->row; r < im->row + im->h && r < cn_front.rows; r++)
		for (c = im->col; c < im->col + im->w && c < cn_front.cols; c++)
			cn_front.c[r * cn_front.cols + c].cp = 0xFFFFFFFFu;
}

/* Drop region i. */
void cn_imgdrop(size_t i)
{
	if (i >= cn_nimg)
		return;
	cn_imgforget(&cn_imgs[i]);
	cn_imgowe(cn_imgs[i].id);
	s_free(&cn_imgs[i].data);
	cn_imgs[i] = cn_imgs[cn_nimg - 1];
	cn_nimg--;
}

/* Drop every region: the screen is being left, or cleared outright. */
void cn_imgclear(void)
{
	while (cn_nimg)
		cn_imgdrop(cn_nimg - 1);
}

/* Whether a cell is inside a region that owns it, so the diff leaves it
   alone. A picture drawn under text owns nothing: the text has to go out. */
int cn_imgat(int row, int col)
{
	size_t i;

	for (i = 0; i < cn_nimg; i++)
		if (!cn_imgs[i].over &&
		    row >= cn_imgs[i].row && row < cn_imgs[i].row + cn_imgs[i].h &&
		    col >= cn_imgs[i].col && col < cn_imgs[i].col + cn_imgs[i].w)
			return 1;
	return 0;
}

/* Box-filter a rectangle of a bitmap into w by h pixels. iw is the whole
   bitmap's width, which is its stride, and rx,ry,rw,rh is the part to take:
   the source rectangle is what lets a picture be *cropped* to the screen
   rather than squashed into it, which is what a zoom or a center wallpaper
   means (Gitea #117). The backend scales, not the caller: only the backend
   knows what a cell measures, and four modules had otherwise each grown a
   scaler of their own. */
void cn_imgscalesrc(const unsigned char *in, int iw, int ih,
		    int rx, int ry, int rw, int rh,
		    unsigned char *out, int w, int h)
{
	int x, y, c;

	(void)ih;
	for (y = 0; y < h; y++) {
		int y0 = ry + (int)((long long)y * rh / h);
		int y1 = ry + (int)((long long)(y + 1) * rh / h);

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > ry + rh)
			y1 = ry + rh;
		for (x = 0; x < w; x++) {
			int x0 = rx + (int)((long long)x * rw / w);
			int x1 = rx + (int)((long long)(x + 1) * rw / w);
			unsigned long sum[3] = { 0, 0, 0 }, n = 0;
			int sx, sy;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > rx + rw)
				x1 = rx + rw;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++) {
					const unsigned char *p = in + ((size_t)sy * iw + sx) * 3;

					for (c = 0; c < 3; c++)
						sum[c] += p[c];
					n++;
				}
			for (c = 0; c < 3; c++)
				out[((size_t)y * w + x) * 3 + c] =
					(unsigned char)(n ? sum[c] / n : 0);
		}
	}
}

/* The whole bitmap into w by h pixels. */
void cn_imgscale(const unsigned char *in, int iw, int ih,
		 unsigned char *out, int w, int h)
{
	cn_imgscalesrc(in, iw, ih, 0, 0, iw, ih, out, w, h);
}

/* Place a picture: w by h cells at row, col, from iw by ih pixels of RGB.
   With a pane, row and col are the pane's own, and anything past its edge is
   refused rather than drawn outside it -- the same discipline cn_pput gives
   text. 0 when this terminal cannot paint pixels, so the caller falls back
   to its own cells; 1 once the region is the console's to keep. */
int cn_image(sh *s, const char *pane, int row, int col, int h, int w,
	     const unsigned char *rgb, int iw, int ih, unsigned flags)
{
	int prow = 0, pcol = 0, ph = 0, pw = 0, cw = 0, chh = 0, tw, th, k;
	int wrow, wcol, wh, ww, vr0, vc0, vr1, vc1, sx, sy, sw, sh;
	unsigned char *px = 0;
	unsigned sum, reuse = 0;
	size_t i;
	cn_img *im;

	if (!cn_isopen() || !rgb || h < 1 || w < 1 || iw < 1 || ih < 1)
		return 0;
	k = cn_gfx(s);
	if (k != CN_GFX_SIXEL && k != CN_GFX_KITTY)
		return 0;
	cn_cellpx(&cw, &chh);
	if (cw < 2 || chh < 2)
		return 0;
	if (pane) {
		if (!cn_prect(pane, &prow, &pcol, &ph, &pw))
			return 0;
		if (row < 0 || col < 0 || row + h > ph || col + w > pw)
			return 0;
		row += prow;
		col += pcol;
	}
	/* What the caller asked for, before the screen's edges are allowed to
	   crop it: that rectangle is what names this region from one frame to
	   the next, so the identity checks below and cn_imgkeep compare it
	   rather than where the picture actually ended up. Comparing the
	   cropped one instead would make a zoom wallpaper a new region every
	   frame -- a fresh kitty id, a delete owed for the old one, and all
	   6.3 MB of it hashed and encoded again. */
	wrow = row;
	wcol = col;
	wh = h;
	ww = w;
	/* The screen's own edges crop rather than refuse. A zoom or a center
	   wallpaper is *meant* to hang off the screen -- dt_wallfit gives a
	   negative row or column for it on purpose -- and until 0.99.95 this
	   returned 0, so img fell back to half-block cells and neither mode
	   had ever been pixels on any terminal (Gitea #117). Cropping is what
	   those two modes mean; squashing the picture into the visible
	   rectangle is the other thing that could be done here and is not
	   what they promise. A *pane's* edge still refuses, which is a
	   different contract: a window's picture must not spill, and a caller
	   computing a rectangle larger than its own pane has a bug worth
	   seeing as a fallback to cells. */
	vr0 = row < 0 ? 0 : row;
	vc0 = col < 0 ? 0 : col;
	vr1 = row + h > cn_back.rows ? cn_back.rows : row + h;
	vc1 = col + w > cn_back.cols ? cn_back.cols : col + w;
	if (vr1 <= vr0 || vc1 <= vc0)
		return 0;
	/* Which part of the bitmap that leaves, as the difference of two
	   rounded offsets rather than a product of the visible width: the
	   difference cannot land past the end of the buffer where iw is not a
	   whole multiple of w, and a product can. */
	sx = (int)((long long)(vc0 - col) * iw / w);
	sy = (int)((long long)(vr0 - row) * ih / h);
	sw = (int)((long long)(vc1 - col) * iw / w) - sx;
	sh = (int)((long long)(vr1 - row) * ih / h) - sy;
	if (sw < 1)
		sw = 1;
	if (sh < 1)
		sh = 1;
	if (sx + sw > iw)
		sw = iw - sx;
	if (sy + sh > ih)
		sh = ih - sy;
	if (sw < 1 || sh < 1)
		return 0;
	row = vr0;
	col = vc0;
	h = vr1 - vr0;
	w = vc1 - vc0;
	sum = cn_hash(rgb, (size_t)iw * ih * 3, 2166136261u);
	sum = cn_hash(&flags, sizeof flags, sum);
	for (i = 0; i < cn_nimg; i++) {
		im = &cn_imgs[i];
		if (im->wrow != wrow || im->wcol != wcol || im->wh != wh ||
		    im->ww != ww)
			continue;
		if (im->sum == sum) {
			/* The same picture in the same place: it is already on
			   screen. One that owns its cells takes whatever this
			   frame has drawn as its own again; one drawn under
			   text is simply still wanted, which is what keeps it
			   (a caller that stops placing it is what makes it
			   go). */
			if (im->over)
				im->live = 1;
			else
				im->under = cn_imgunder(im);
			return 1;
		}
		/* A different picture in the same rectangle keeps the id: a
		   kitty transmission with an id that is already taken replaces
		   what was there, which is one escape rather than a delete and
		   a place -- and it is what a film does every frame. */
		reuse = im->id;
		im->id = 0;
		cn_imgdrop(i);
		break;
	}
	if (cn_nimg == cn_imgcap) {
		size_t nc = cn_imgcap ? cn_imgcap * 2 : 4;
		cn_img *na = xm(nc * sizeof *na);

		memset(na, 0, nc * sizeof *na);
		if (cn_nimg)
			memcpy(na, cn_imgs, cn_nimg * sizeof *na);
		free(cn_imgs);
		cn_imgs = na;
		cn_imgcap = nc;
	}
	im = &cn_imgs[cn_nimg++];
	memset(im, 0, sizeof *im);
	im->row = row;
	im->col = col;
	im->h = h;
	im->w = w;
	im->wrow = wrow;
	im->wcol = wcol;
	im->wh = wh;
	im->ww = ww;
	im->sum = sum;
	im->over = (flags & DP_IMG_UNDER) ? 1 : 0;
	im->live = im->over;
	im->id = k == CN_GFX_KITTY ? (reuse ? reuse : kt_id()) : 0;
	s_init(&im->data);
	/* The rectangle in pixels is what a sixel will paint 1:1, so the bitmap
	   is scaled to exactly that: a cell short either way and the picture
	   spills into its neighbours or leaves a gap. The kitty protocol is
	   given the rectangle in cells and scales for itself, which is what
	   makes a film affordable there: its cost is bytes, not encoding -- a
	   frame of 100 by 34 cells is half a megabyte of base64 at full
	   resolution -- so a picture that is not a still (no palette chosen
	   from it, which is what a film asks for) is sent at half the pixels
	   in each direction and the terminal scales it back up. */
	tw = w * cw;
	th = h * chh;
	if (im->id && !(flags & DP_IMG_CHOSEN) && tw > 2 && th > 2) {
		tw /= 2;
		th /= 2;
	}
	if (sx || sy || sw != iw || sh != ih || tw != sw || th != sh) {
		px = xm((size_t)tw * th * 3);
		if (tw == sw && th == sh) {
			/* A crop that lands on a cell boundary -- which is
			   every picture img hands over, having already scaled
			   it to exactly w*cw by h*chh -- is a sub-rectangle at
			   the resolution it is wanted at, so it is a copy per
			   row and not a box filter over every output pixel.
			   The first version of this went through the filter
			   regardless and paid 81 ms for a screenful at 232x71
			   to resample a picture into itself. */
			int y;

			for (y = 0; y < th; y++)
				memcpy(px + (size_t)y * tw * 3,
				       rgb + ((size_t)(sy + y) * iw + sx) * 3,
				       (size_t)tw * 3);
		} else {
			cn_imgscalesrc(rgb, iw, ih, sx, sy, sw, sh,
				       px, tw, th);
		}
	}
	if (im->id)
		kt_encode(px ? px : rgb, tw, th, w, h, im->id, im->over,
			  &im->data);
	else
		six_encode(px ? px : rgb, tw, th,
			   (flags & DP_IMG_CHOSEN) ? 1 : 0, &im->data);
	free(px);
	im->under = cn_imgunder(im);
	im->sent = 0;
	lg(HIBR_LDBG, "image: %s, %dx%d cells at %d,%d from %dx%d of %dx%d "
	   "pixels, %zu bytes%s%s%s", cn_gfxname(k), w, h, row, col, sw, sh,
	   iw, ih, im->data.n,
	   (flags & DP_IMG_CHOSEN) ? ", chosen palette" : "",
	   im->over ? ", under text" : "",
	   (wh != h || ww != w) ? ", cropped to the screen" : "");
	return 1;
}

/* An under-text picture already placed at exactly this rectangle is still
   wanted, without being handed its pixels again: 1 when there was one. What
   keeps such a picture is its caller placing it, and a placement hashes every
   pixel to learn whether it is the one already there -- 6.3 MB and 19.6 ms for
   a screenful -- which a caller that knows the answer should not pay. A frame
   that paints nothing can therefore keep a wallpaper on screen, and a frame
   that paints can place the same one for nothing. */
int cn_imgkeep(sh *s, int row, int col, int h, int w)
{
	size_t i;

	(void)s;
	for (i = 0; i < cn_nimg; i++) {
		if (!cn_imgs[i].over || cn_imgs[i].wrow != row ||
		    cn_imgs[i].wcol != col || cn_imgs[i].wh != h ||
		    cn_imgs[i].ww != w)
			continue;
		cn_imgs[i].live = 1;
		return 1;
	}
	return 0;
}

/* Before the diff: a region whose cells are no longer the ones it was placed
   over has been drawn through, so it goes and they are painted again. */
void cn_imgcheck(void)
{
	size_t i = 0;

	while (i < cn_nimg) {
		/* One drawn under text claims no cells, so the hash below says
		   nothing about it. What keeps it is the caller placing it
		   again: a frame that did not is a caller that has stopped
		   wanting it (the wallpaper turned off, a window closed), and
		   then it goes -- with a delete, for a protocol that keeps
		   pictures. A sixel is paint, so any cell written over it has
		   destroyed that much of it and the bytes go again; a kitty
		   picture is below the text (z=-1) and the terminal composites
		   it, so nothing has to be re-sent at all. That difference is
		   the whole cost of a wallpaper: one escape when it changes,
		   against the bitmap again whenever anything above it moves. */
		if (cn_imgs[i].over) {
			if (!cn_imgs[i].live) {
				cn_imgowe(cn_imgs[i].id);
				s_free(&cn_imgs[i].data);
				cn_imgs[i] = cn_imgs[cn_nimg - 1];
				cn_nimg--;
				continue;
			}
			if (!cn_imgs[i].id && cn_imgs[i].sent &&
			    cn_imgunder(&cn_imgs[i]) != cn_imgs[i].above)
				cn_imgs[i].sent = 0;
			cn_imgs[i].live = 0;
			i++;
			continue;
		}
		if (cn_imgunder(&cn_imgs[i]) != cn_imgs[i].under) {
			lg(HIBR_LDBG, "image: dropped, its cells were drawn through");
			cn_imgdrop(i);
			continue;
		}
		i++;
	}
}

/* The deletes owed for pictures that have gone: a kitty placement stays until
   it is told to go, so this is what makes a window that closed, moved or was
   covered stop showing its picture. Sent before the diff, which then paints
   the text that was underneath. Nothing is owed on a terminal drawing sixel,
   where writing over the cells is what removes the paint. */
size_t cn_imgdels(str *b)
{
	size_t n = cn_kdel.n;

	if (!n)
		return 0;
	s_add(b, cn_kdel.p, n);
	cn_kdel.n = 0;
	return n;
}

/* The bytes of every region that has not had them yet, each at its own
   corner: with over set, the ones text is about to be drawn over, which go
   out before the diff; otherwise the ones that own their cells, which go out
   after it. Returns how many bytes that was. A picture moves the cursor
   itself, which is why the caller forgets where it was. */
size_t cn_imgsend(str *b, int over)
{
	size_t i, n = 0;

	for (i = 0; i < cn_nimg; i++) {
		if (cn_imgs[i].sent || !cn_imgs[i].data.n)
			continue;
		if (!cn_imgs[i].over != !over)
			continue;
		s_cat(b, "\033[");
		s_num(b, cn_imgs[i].row + 1);
		s_ch(b, ';');
		s_num(b, cn_imgs[i].col + 1);
		s_ch(b, 'H');
		s_add(b, cn_imgs[i].data.p, cn_imgs[i].data.n);
		cn_imgs[i].sent = 1;
		if (cn_imgs[i].over)
			cn_imgs[i].above = cn_imgunder(&cn_imgs[i]);
		n += cn_imgs[i].data.n;
	}
	return n;
}

/* Whether any region is being kept, for `console gfx`. */
size_t cn_imgn(void)
{
	return cn_nimg;
}
