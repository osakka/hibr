/* image -- a picture on the cell grid, as pixels the terminal paints.
 *
 * A sixel is not cells: it is a bitmap laid over a rectangle, and the grid
 * knows nothing about it. Drawing one straight at the screen is what this
 * module's own README forbids -- it corrupts whatever the diff writes next --
 * so a picture is a *region* the console owns, and the ordinary diff is what
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
 * The encoder is sixel.c's. A backend that could place pixels itself (a
 * framebuffer, Blit) would implement the same dp_api entry and never come
 * here.
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
	str data;		/* the encoded picture */
} cn_img;

static cn_img *cn_imgs;
static size_t cn_nimg, cn_imgcap;

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
	int prow = 0, pcol = 0, ph = 0, pw = 0, cw = 0, chh = 0, tw, th;
	unsigned char *px = 0;
	unsigned sum;
	size_t i;
	cn_img *im;

	if (!cn_isopen() || !rgb || h < 1 || w < 1 || iw < 1 || ih < 1)
		return 0;
	if (cn_gfx(s) != CN_GFX_SIXEL)
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
		if (im->sum == sum && !im->over) {
			/* the same picture in the same place: it is already on
			   screen, and the cells under it are whatever this
			   frame has drawn, so take those as its own again */
			im->under = cn_imgunder(im);
			return 1;
		}
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
	s_init(&im->data);
	/* The rectangle in pixels is what the terminal will paint 1:1, so the
	   bitmap is scaled to exactly that: a cell short either way and the
	   picture spills into its neighbours or leaves a gap. */
	tw = w * cw;
	th = h * chh;
	if (tw != iw || th != ih) {
		px = xm((size_t)tw * th * 3);
		cn_imgscale(rgb, iw, ih, px, tw, th);
		six_encode(px, tw, th, (flags & DP_IMG_CHOSEN) ? 1 : 0, &im->data);
		free(px);
	} else {
		six_encode(rgb, iw, ih, (flags & DP_IMG_CHOSEN) ? 1 : 0, &im->data);
	}
	im->under = cn_imgunder(im);
	im->sent = 0;
	lg(HIBR_LDBG, "image: %dx%d cells at %d,%d from %dx%d pixels, %zu bytes%s%s",
	   w, h, row, col, iw, ih, im->data.n,
	   (flags & DP_IMG_CHOSEN) ? ", chosen palette" : "",
	   im->over ? ", under text" : "");
	return 1;
}

/* Before the diff: a region whose cells are no longer the ones it was placed
   over has been drawn through, so it goes and they are painted again. */
void cn_imgcheck(void)
{
	size_t i = 0;

	while (i < cn_nimg) {
		/* One drawn under text claims no cells, so there is nothing to
		   compare: it is forgotten once its bytes have gone out, and
		   whatever wants it again places it again. */
		if (cn_imgs[i].over) {
			if (cn_imgs[i].sent) {
				s_free(&cn_imgs[i].data);
				cn_imgs[i] = cn_imgs[cn_nimg - 1];
				cn_nimg--;
				continue;
			}
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
		n += cn_imgs[i].data.n;
	}
	return n;
}

/* Whether any region is being kept, for `console gfx`. */
size_t cn_imgn(void)
{
	return cn_nimg;
}
