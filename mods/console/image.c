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
#include <stdlib.h>
#include <string.h>

typedef struct cn_img {
	int row, col, h, w;	/* where it is, in screen cells */
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

/* FNV-1a over bytes, for "is this the same picture" and "are the cells under
   it still the ones it was placed over". */
unsigned cn_hash(const void *p, size_t n, unsigned h)
{
	const unsigned char *b = p;

	while (n--) {
		h ^= *b++;
		h *= 16777619u;
	}
	return h;
}

/* What the cells a region covers come to now. */
unsigned cn_imgunder(const cn_img *im)
{
	unsigned h = 2166136261u;
	int r, c;

	for (r = im->row; r < im->row + im->h && r < cn_back.rows; r++)
		for (c = im->col; c < im->col + im->w && c < cn_back.cols; c++) {
			cn_cell *k = &cn_back.c[r * cn_back.cols + c];

			h = cn_hash(&k->cp, sizeof k->cp, h);
			h = cn_hash(&k->fg, sizeof k->fg, h);
			h = cn_hash(&k->bg, sizeof k->bg, h);
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

/* Box-filter a bitmap of any size into w by h pixels. The backend scales,
   not the caller: only the backend knows what a cell measures, and four
   modules had otherwise each grown a scaler of their own. */
void cn_imgscale(const unsigned char *in, int iw, int ih,
		 unsigned char *out, int w, int h)
{
	int x, y, c;

	for (y = 0; y < h; y++) {
		int y0 = (int)((long long)y * ih / h), y1 = (int)((long long)(y + 1) * ih / h);

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > ih)
			y1 = ih;
		for (x = 0; x < w; x++) {
			int x0 = (int)((long long)x * iw / w);
			int x1 = (int)((long long)(x + 1) * iw / w);
			unsigned long sum[3] = { 0, 0, 0 }, n = 0;
			int sx, sy;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > iw)
				x1 = iw;
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

/* Place a picture: w by h cells at row, col, from iw by ih pixels of RGB.
   With a pane, row and col are the pane's own, and anything past its edge is
   refused rather than drawn outside it -- the same discipline cn_pput gives
   text. 0 when this terminal cannot paint pixels, so the caller falls back
   to its own cells; 1 once the region is the console's to keep. */
int cn_image(sh *s, const char *pane, int row, int col, int h, int w,
	     const unsigned char *rgb, int iw, int ih, unsigned flags)
{
	int prow = 0, pcol = 0, ph = 0, pw = 0, cw = 0, chh = 0, tw, th, k;
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
	if (row < 0 || col < 0 || row + h > cn_back.rows || col + w > cn_back.cols)
		return 0;
	sum = cn_hash(rgb, (size_t)iw * ih * 3, 2166136261u);
	sum = cn_hash(&flags, sizeof flags, sum);
	for (i = 0; i < cn_nimg; i++) {
		im = &cn_imgs[i];
		if (im->row != row || im->col != col || im->h != h || im->w != w)
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
	if (tw != iw || th != ih) {
		px = xm((size_t)tw * th * 3);
		cn_imgscale(rgb, iw, ih, px, tw, th);
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
	lg(HIBR_LDBG, "image: %s, %dx%d cells at %d,%d from %dx%d pixels, "
	   "%zu bytes%s%s", cn_gfxname(k), w, h, row, col, iw, ih, im->data.n,
	   (flags & DP_IMG_CHOSEN) ? ", chosen palette" : "",
	   im->over ? ", under text" : "");
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
		if (!cn_imgs[i].over || cn_imgs[i].row != row ||
		    cn_imgs[i].col != col || cn_imgs[i].h != h ||
		    cn_imgs[i].w != w)
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
