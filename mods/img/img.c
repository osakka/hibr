#define _GNU_SOURCE

#include "im.h"
#include "../display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

void im_free(image *im)
{
	free(im->px);
	im->px = 0;
	im->w = im->h = 0;
}

/* 4x4 ordered (Bayer) dither, threshold in [0,16) -- spreads what would
   otherwise be a hard, per-cell-regular rounding step (a real but faint
   vertical gradient in a photo, quantised the same way cell after cell,
   reads as a clean repeating band once a shadow darkens it) into fine
   grain instead. An exact 1:1 pixel mapping (n == 1, no averaging) is
   untouched: sr/n is already a whole number, and any threshold below
   16/16 floors straight back to it. */
static const unsigned char im_bayer4[4][4] = {
	{  0,  8,  2, 10 }, {  12,  4, 14,  6 },
	{  3, 11,  1,  9 }, {  15,  7, 13,  5 },
};

static unsigned char im_dith(long s, long n, int d)
{
	return (unsigned char)((32 * s + n * (2 * d + 1)) / (32 * n));
}

void im_resample(const image *im, cell *out, int rows, int cols)
{
	int halfrows = rows * 2;
	int r, c;

	for (r = 0; r < halfrows; r++) {
		int y0 = (int)((long long)r * im->h / halfrows);
		int y1 = (int)((long long)(r + 1) * im->h / halfrows);

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > im->h)
			y1 = im->h;
		for (c = 0; c < cols; c++) {
			int x0 = (int)((long long)c * im->w / cols);
			int x1 = (int)((long long)(c + 1) * im->w / cols);
			long sr = 0, sg = 0, sb = 0, n = 0;
			int y, x;
			cell *cp = &out[(r / 2) * cols + c];

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > im->w)
				x1 = im->w;
			for (y = y0; y < y1; y++) {
				const unsigned char *row =
					im->px + (size_t)y * (size_t)im->w * 3;
				for (x = x0; x < x1; x++) {
					sr += row[x * 3 + 0];
					sg += row[x * 3 + 1];
					sb += row[x * 3 + 2];
					n++;
				}
			}
			if (n < 1)
				n = 1;
			{
				int d = im_bayer4[r & 3][c & 3];

				if (r % 2 == 0) {
					cp->tr = im_dith(sr, n, d);
					cp->tg = im_dith(sg, n, d);
					cp->tb = im_dith(sb, n, d);
				} else {
					cp->br = im_dith(sr, n, d);
					cp->bg = im_dith(sg, n, d);
					cp->bb = im_dith(sb, n, d);
				}
			}
		}
	}
}

/* Perceived brightness of one cell -- top and bottom halves averaged
   first, since a single character has no way to show two shades. */
static int im_luma(const cell *c)
{
	int r = (c->tr + c->br) / 2;
	int g = (c->tg + c->bg) / 2;
	int b = (c->tb + c->bb) / 2;

	return (299 * r + 587 * g + 114 * b) / 1000;
}

/* Sparse to dense: printed in the terminal's own foreground colour, so
   density is the only signal -- jp2a's own classic look, no colour. */
static const char im_ramp[] = " .:-=+*#%@";
#define IM_RAMPN ((int)sizeof(im_ramp) - 2)

/* Render to a str as raw ANSI: colour half-blocks (mode 0) or a grayscale
   ramp (mode 1), rows x cols cells. */
static void im_render(str *out, const cell *g, int rows, int cols, int gray)
{
	int r, c;

	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			const cell *cp = &g[r * cols + c];
			char buf[64];

			if (gray) {
				int lv = im_luma(cp) * IM_RAMPN / 255;

				if (lv < 0)
					lv = 0;
				if (lv > IM_RAMPN)
					lv = IM_RAMPN;
				s_ch(out, im_ramp[lv]);
				continue;
			}
			snprintf(buf, sizeof buf,
				 "\033[38;2;%u;%u;%u;48;2;%u;%u;%um",
				 cp->tr, cp->tg, cp->tb, cp->br, cp->bg,
				 cp->bb);
			s_cat(out, buf);
			s_cat(out, "\xe2\x96\x80"); /* U+2580 upper half block */
		}
		if (!gray)
			s_cat(out, "\033[0m");
		s_ch(out, '\n');
	}
}

/* Read one number from a PPM header, past blanks and # comments. */
static int im_ppmnum(FILE *f, long *v)
{
	int c = fgetc(f), n = 0;

	for (;;) {
		while (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			c = fgetc(f);
		if (c != '#')
			break;
		while (c != EOF && c != '\n')
			c = fgetc(f);
	}
	*v = 0;
	while (c >= '0' && c <= '9' && n < 6) {
		*v = *v * 10 + (c - '0');
		c = fgetc(f);
		n++;
	}
	return n > 0 && n < 6 && (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		? 0 : -1;
}

/* A binary PPM (P6, 8 bits): read here, by this module's own few lines and
   no library, which is what lets a program that must not hand a user's
   file to libpng or libturbojpeg -- the login screen, as root -- have that
   user's own process decode it to a small PPM and draw only that. Up to
   4096 pixels a side, every byte counted. */
static int im_ppmload(const char *path, image *out, str *err)
{
	FILE *f = fopen(path, "rb");
	long w, h, mx;
	size_t n;

	if (!f) {
		s_cat(err, "cannot open ");
		s_cat(err, path);
		return HIBR_FAIL;
	}
	if (fgetc(f) != 'P' || fgetc(f) != '6' || im_ppmnum(f, &w) ||
	    im_ppmnum(f, &h) || im_ppmnum(f, &mx) || w < 1 || h < 1 ||
	    w > 4096 || h > 4096 || mx != 255) {
		fclose(f);
		s_cat(err, path);
		s_cat(err, ": not an 8-bit binary PPM this reads");
		return HIBR_FAIL;
	}
	n = (size_t)w * (size_t)h * 3;
	out->px = xm(n);
	if (fread(out->px, 1, n, f) != n) {
		fclose(f);
		free(out->px);
		out->px = 0;
		s_cat(err, path);
		s_cat(err, ": the PPM is shorter than its header says");
		return HIBR_FAIL;
	}
	fclose(f);
	out->w = (int)w;
	out->h = (int)h;
	return HIBR_OK;
}

/* Decode an image by what its first bytes say it is, not its name: PNG's
   signature or JPEG's start-of-image marker, so a photo called .JPG, .jpeg
   or nothing at all still opens. */
static int im_load(const char *path, image *out, str *err)
{
	unsigned char m[8];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) {
		s_cat(err, "cannot open ");
		s_cat(err, path);
		return HIBR_FAIL;
	}
	n = fread(m, 1, sizeof m, f);
	fclose(f);
	if (n >= 8 && !memcmp(m, "\x89PNG\r\n\x1a\n", 8))
		return im_pngload(path, out, err);
	if (n >= 3 && m[0] == 0xFF && m[1] == 0xD8 && m[2] == 0xFF)
		return im_jpegload(path, out, err);
	if (n >= 2 && m[0] == 'P' && m[1] == '6')
		return im_ppmload(path, out, err);
	s_cat(err, path);
	s_cat(err, ": neither a PNG, a JPEG nor a PPM");
	return HIBR_FAIL;
}

/* Write a resampled grid as a binary PPM, a cell's two halves as two
   pixels one above the other: the picture at exactly the size it will be
   drawn, for `img draw` to read back without a library. */
static void im_ppm(str *out, const cell *g, int rows, int cols)
{
	int r, c, half;

	s_cat(out, "P6\n");
	s_num(out, cols);
	s_ch(out, ' ');
	s_num(out, (long)rows * 2);
	s_cat(out, "\n255\n");
	for (r = 0; r < rows; r++) {
		for (half = 0; half < 2; half++) {
			for (c = 0; c < cols; c++) {
				const cell *x = &g[r * cols + c];

				s_ch(out, half ? x->br : x->tr);
				s_ch(out, half ? x->bg : x->tg);
				s_ch(out, half ? x->bb : x->tb);
			}
		}
	}
}

/* img [-g] [-o ppm] [-w cols] [-h rows] file -- print an image as ANSI to
   stdout, or with -o ppm as a binary PPM of the size it would be drawn,
   sized to the terminal when neither -w nor -h is given and stdout is
   one. */
static int im_cat(sh *s, int ac, char **av)
{
	const char *path = 0;
	int gray = 0, cols = 0, rows = 0, i, ppm = 0;
	image im;
	str err, out;
	cell *grid;

	(void)s;
	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-g")) {
			gray = 1;
		} else if (!strcmp(av[i], "-o") && i + 1 < ac) {
			if (strcmp(av[++i], "ppm")) {
				lg(HIBR_LERR, "img: -o takes ppm");
				return 2;
			}
			ppm = 1;
		} else if (!strcmp(av[i], "-w") && i + 1 < ac) {
			cols = atoi(av[++i]);
		} else if (!strcmp(av[i], "-h") && i + 1 < ac) {
			rows = atoi(av[++i]);
		} else {
			path = av[i];
		}
	}
	if (!path) {
		lg(HIBR_LERR, "usage: img [-g] [-o ppm] [-w cols] [-h rows] file");
		return 2;
	}
	if (cols < 1 || rows < 1) {
		struct winsize ws;

		if (isatty(1) && ioctl(1, TIOCGWINSZ, &ws) == 0) {
			if (cols < 1 && ws.ws_col > 0)
				cols = ws.ws_col;
			if (rows < 1 && ws.ws_row > 1)
				rows = ws.ws_row - 1;
		}
		if (cols < 1)
			cols = 80;
		if (rows < 1)
			rows = 24;
	}
	s_init(&err);
	if (im_load(path, &im, &err) != HIBR_OK) {
		lg(HIBR_LERR, "%s", err.p ? err.p : "decode failed");
		s_free(&err);
		return HIBR_FAIL;
	}
	s_free(&err);
	grid = xm((size_t)rows * (size_t)cols * sizeof(cell));
	im_resample(&im, grid, rows, cols);
	im_free(&im);
	s_init(&out);
	if (ppm)
		im_ppm(&out, grid, rows, cols);
	else
		im_render(&out, grid, rows, cols, gray);
	free(grid);
	fflush(stdout);
	if (out.p && write(1, out.p, out.n) < 0)
		lg(HIBR_LDBG, "img: could not write the picture");
	s_free(&out);
	return HIBR_OK;
}

/* Resampled grids, kept across calls -- dt_wall draws the desktop's own
   wallpaper every frame, and re-decoding and re-resampling a PNG that many
   times a second for a picture that never changes would be wasted work
   every single one of them. Keyed on the file's own mtime too, so
   replacing the file is noticed without needing a restart.

   More than one entry now: once img draw can target a pane (-p), the
   wallpaper and a window's own preview both call it, with different
   files, in the same frame -- a single slot thrashed between the two,
   paying for a full decode and resample of each on every single frame
   instead of caching either. IM_CACHEN slots, evicted round-robin (oldest
   first, no recency bookkeeping needed for a handful of concurrent
   callers). Each slot's own previous grid is freed before it is reused,
   never accumulated, the same shape the single-entry version already
   was. ASan's leak detector cannot see a dlopen'd, tcc-built .so's own
   static data as a root to scan from, so the slots still live at process
   exit report as a leak of exactly their own size, not as a growing one
   or an actual bug -- verified the same way the single-entry version was. */
#define IM_CACHEN 4

static struct {
	char *path;
	int rows, cols;
	time_t mtime;
	cell *grid;
} im_cache[IM_CACHEN];
static int im_cachei;

static int im_cached(const char *path, int rows, int cols, cell **out)
{
	struct stat st;
	int i;

	if (stat(path, &st) != 0)
		return 0;
	for (i = 0; i < IM_CACHEN; i++) {
		if (!im_cache[i].path || strcmp(im_cache[i].path, path) ||
		    im_cache[i].rows != rows || im_cache[i].cols != cols)
			continue;
		if (im_cache[i].mtime != st.st_mtime)
			continue;
		*out = im_cache[i].grid;
		return 1;
	}
	return 0;
}

static void im_cache_store(const char *path, int rows, int cols, cell *grid)
{
	struct stat st;
	int i = im_cachei;

	free(im_cache[i].path);
	free(im_cache[i].grid);
	im_cache[i].path = strdup(path);
	im_cache[i].rows = rows;
	im_cache[i].cols = cols;
	im_cache[i].grid = grid;
	im_cache[i].mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
	im_cachei = (im_cachei + 1) % IM_CACHEN;
}

/* img draw file row col h w [-g] [-p pane] -- blit directly into the open
   display, for a window or the desktop's own wallpaper, rather than
   printing text a shell would have to route somewhere. Requires "display"
   only here, so `img file.png` in a plain pipe never touches the console
   module. With -p, row/col are relative to that pane's own top-left
   corner rather than the root screen, and anything past its own edge is
   silently clipped rather than drawn -- the same discipline console put
   -p already gives text, so a window's own preview can use this without
   knowing where the window itself sits or surviving it moving. */
static int im_draw(sh *s, int ac, char **av)
{
	const dp_api *dp;
	const char *path, *pane = 0;
	int row, col, rows, cols, gray = 0, mode = IM_AUTO;
	int prow = 0, pcol = 0, ph = 0, pw = 0;
	image im;
	str err;
	cell *grid;
	int r, c, i;

	if (ac < 7) {
		lg(HIBR_LERR, "usage: img draw file row col h w [-g] [-m mode] [-p pane]");
		return 2;
	}
	path = av[2];
	row = atoi(av[3]);
	col = atoi(av[4]);
	rows = atoi(av[5]);
	cols = atoi(av[6]);
	for (i = 7; i < ac; i++) {
		if (!strcmp(av[i], "-g"))
			gray = 1;
		else if (!strcmp(av[i], "-m") && i + 1 < ac)
			mode = im_mode(av[++i]);
		else if (!strcmp(av[i], "-p") && i + 1 < ac)
			pane = av[++i];
	}
	if (gray)
		mode = IM_ASCII;
	if (rows < 1 || cols < 1) {
		lg(HIBR_LERR, "img draw: h and w must be at least 1");
		return 2;
	}
	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp || !dp->isopen()) {
		lg(HIBR_LERR, "img draw: no display is open");
		return HIBR_FAIL;
	}
	/* row/col become pane-relative once -p names one: this is the
	   only place that ever has to know that, since every dp->put
	   below still takes a plain absolute position either way. */
	if (pane) {
		if (!dp->prect || !dp->prect(pane, &prow, &pcol, &ph, &pw)) {
			lg(HIBR_LERR, "img draw: no such pane: %s", pane);
			return HIBR_FAIL;
		}
	}
	/* Pixels first where they are wanted and can be placed: the display
	   keeps the picture as a region of its own, so a window redrawing the
	   same one each frame costs nothing after the first. A still picture
	   gets a palette chosen from itself; a frame of a film comes through
	   dp->image straight from the media module with the fixed one. */
	if (mode == IM_SIXEL || mode == IM_AUTO) {
		int cw = 0, chh = 0;

		if (dp->image && dp->cellpx) {
			dp->cellpx(&cw, &chh);
			if (cw > 1 && chh > 1) {
				unsigned char *px;
				int iw = cols * cw, ih = rows * chh;
				int done;

				s_init(&err);
				if (im_load(path, &im, &err) != HIBR_OK) {
					if (mode == IM_SIXEL) {
						lg(HIBR_LERR, "%s", err.p ? err.p : "decode failed");
						s_free(&err);
						return HIBR_FAIL;
					}
					s_free(&err);
					goto cells;
				}
				s_free(&err);
				px = xm((size_t)iw * ih * 3);
				im_scale(&im, px, iw, ih);
				im_free(&im);
				done = dp->image(s, pane, row, col, rows, cols, px, iw, ih, DP_IMG_CHOSEN);
				free(px);
				if (done)
					return HIBR_OK;
			}
		}
		if (mode == IM_SIXEL) {
			lg(HIBR_LDBG, "img draw: no pixels here, cells instead");
			mode = IM_HALF;
		}
	}
cells:
	if (mode == IM_AUTO)
		mode = IM_HALF;
	if (mode == IM_ASCII)
		gray = 1;
	if (!im_cached(path, rows, cols, &grid)) {
		s_init(&err);
		if (im_load(path, &im, &err) != HIBR_OK) {
			lg(HIBR_LERR, "%s", err.p ? err.p : "decode failed");
			s_free(&err);
			return HIBR_FAIL;
		}
		s_free(&err);
		grid = xm((size_t)rows * (size_t)cols * sizeof(cell));
		im_resample(&im, grid, rows, cols);
		im_free(&im);
		im_cache_store(path, rows, cols, grid);
	}
	for (r = 0; r < rows; r++) {
		int ar = row + r;

		if (pane && (ar < 0 || ar >= ph))
			continue;
		for (c = 0; c < cols; c++) {
			const cell *cp = &grid[r * cols + c];
			int acol = col + c;

			if (pane && (acol < 0 || acol >= pw))
				continue;

			if (gray) {
				int lv = im_luma(cp) * IM_RAMPN / 255;
				char ch[2];

				if (lv < 0)
					lv = 0;
				if (lv > IM_RAMPN)
					lv = IM_RAMPN;
				dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
				ch[0] = im_ramp[lv];
				ch[1] = 0;
				dp->put(pane ? prow + ar : ar,
					pane ? pcol + acol : acol, ch);
				continue;
			}
			if (mode == IM_MONO) {
				unsigned tl = (unsigned)((cp->tr * 30 + cp->tg * 59 +
							  cp->tb * 11) / 100);
				unsigned bl = (unsigned)((cp->br * 30 + cp->bg * 59 +
							  cp->bb * 11) / 100);

				dp->pen(DP_RGB | tl << 16 | tl << 8 | tl,
					DP_RGB | bl << 16 | bl << 8 | bl, 0);
				dp->put(pane ? prow + ar : ar,
					pane ? pcol + acol : acol, "\xe2\x96\x80");
				continue;
			}
			dp->pen(DP_RGB | ((unsigned)cp->tr << 16) |
					 ((unsigned)cp->tg << 8) | cp->tb,
				DP_RGB | ((unsigned)cp->br << 16) |
					 ((unsigned)cp->bg << 8) | cp->bb,
				0);
			dp->put(pane ? prow + ar : ar,
				pane ? pcol + acol : acol, "\xe2\x96\x80");
		}
	}
	return HIBR_OK;
}

/* img size file -- the decoded pixel width and height, "W H", so a caller
   can work out an aspect-correct fit before calling img draw with it; img
   draw's own resample always stretches to exactly the rows/cols it is
   given, with no notion of the source's own shape. */
static int im_size(sh *s, int ac, char **av)
{
	image im;
	str err, t;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: img size file");
		return 2;
	}
	s_init(&err);
	if (im_load(av[2], &im, &err) != HIBR_OK) {
		lg(HIBR_LERR, "%s", err.p ? err.p : "decode failed");
		s_free(&err);
		return HIBR_FAIL;
	}
	s_free(&err);
	s_init(&t);
	s_num(&t, im.w);
	s_ch(&t, ' ');
	s_num(&t, im.h);
	hibr_ret(s, t.p);
	if (!s->bind)
		printf("%s\n", t.p);
	s_free(&t);
	im_free(&im);
	return HIBR_OK;
}

static int m_img(sh *s, int ac, char **av)
{
	if (ac > 1 && !strcmp(av[1], "draw"))
		return im_draw(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "size"))
		return im_size(s, ac, av);
	return im_cat(s, ac, av);
}

const hibr_bi img_bi[] = {
	{ "img", m_img, "an image as ANSI text, or blitted into a window" },
	HIBR_BI_END
};

HIBR_MODULE("img", "0.21", "decode an image and draw it as terminal cells",
	    img_bi, 0, 0);
