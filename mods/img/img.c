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
   instead of caching either. IM_CACHEN slots, and the one evicted is the one
   least recently *used* rather than the one longest in the cache:
   round-robin threw out whatever was next in line, so four other pictures
   drawn in the same frame could between them evict a wallpaper that is
   redrawn on every frame, and it then paid a full decode again -- 268 ms
   for a 3840x2160 photograph, which is a fifth of a core at one miss a
   second (Gitea #105). A use stamp costs one integer a slot and makes the
   picture drawn every frame the last thing any of them would throw away.
   Each slot's own previous grid is freed before it is reused,
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
	unsigned used;
	cell *grid;
} im_cache[IM_CACHEN];
static unsigned im_cacheu;

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
		im_cache[i].used = ++im_cacheu;
		*out = im_cache[i].grid;
		return 1;
	}
	return 0;
}

/* The slot to reuse: an empty one, or the one used longest ago. */
static int im_cacheold(void)
{
	int i, old = 0;

	for (i = 0; i < IM_CACHEN; i++) {
		if (!im_cache[i].path)
			return i;
		if (im_cache[i].used < im_cache[old].used)
			old = i;
	}
	return old;
}

static void im_cache_store(const char *path, int rows, int cols, cell *grid)
{
	struct stat st;
	int i = im_cacheold();

	free(im_cache[i].path);
	free(im_cache[i].grid);
	im_cache[i].path = strdup(path);
	im_cache[i].rows = rows;
	im_cache[i].cols = cols;
	im_cache[i].grid = grid;
	im_cache[i].mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
	im_cache[i].used = ++im_cacheu;
}

/* The picture scaled to real pixels, for the caller that places it again on
   every frame -- the wallpaper does, because a region drawn under the text
   is kept only while its caller keeps placing it (ADR 0037). Without this
   the pixel path had no cache at all where the cell path has had one for
   releases: `img draw -m pixels` of a 3840x2160 photograph onto a 232x71
   screen decoded and resampled it every single call, 330 ms, so a desktop
   in Pixels spent 93% of a core to draw 2.7 frames a second (Gitea #108).
   One entry, not IM_CACHEN of them, because the sizes are not comparable:
   that screen's pixels are 1856x1136x3, 6.3 MB, where its cell grid is
   130 kB -- four slots would cost more resident memory than the shell. */
static struct {
	char *path;
	int w, h;
	time_t mtime;
	unsigned char *px;
} im_pix;

/* What was last placed under the text, and where. The display keeps such a
   picture only while its caller places it again, and a placement hands over
   every pixel so the display can hash them and learn whether it is the one it
   already has -- 6.3 MB and 19.6 ms for a screenful, on every frame that
   draws the wallpaper. When this says the same picture is going to the same
   rectangle, the display is asked to keep what it has instead, which is a
   handful of comparisons. Keyed on everything that decides what was sent,
   and only for a draw with no pane: the one caller that places a picture
   under the text is the wallpaper, which has none. The pixel cache's own
   mtime check is what makes a changed file a miss here too, since a hit is
   what this is only ever consulted after. */
static struct {
	char *path;
	int iw, ih, row, col, rows, cols;
	time_t mtime;
} im_lastu;

static int im_same(const char *path, int iw, int ih, int row, int col,
		   int rows, int cols)
{
	struct stat st;

	if (!im_lastu.path || strcmp(im_lastu.path, path) ||
	    im_lastu.iw != iw || im_lastu.ih != ih || im_lastu.row != row ||
	    im_lastu.col != col || im_lastu.rows != rows ||
	    im_lastu.cols != cols)
		return 0;
	return stat(path, &st) == 0 && im_lastu.mtime == st.st_mtime;
}

static void im_placed(const char *path, int iw, int ih, int row, int col,
		      int rows, int cols)
{
	struct stat st;

	free(im_lastu.path);
	im_lastu.path = strdup(path);
	im_lastu.mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
	im_lastu.iw = iw;
	im_lastu.ih = ih;
	im_lastu.row = row;
	im_lastu.col = col;
	im_lastu.rows = rows;
	im_lastu.cols = cols;
}

/* The scaled pixels for this picture at this size, or nothing. */
static unsigned char *im_pixget(const char *path, int w, int h)
{
	struct stat st;

	if (!im_pix.px || !im_pix.path || strcmp(im_pix.path, path) ||
	    im_pix.w != w || im_pix.h != h)
		return 0;
	if (stat(path, &st) != 0 || im_pix.mtime != st.st_mtime)
		return 0;
	return im_pix.px;
}

/* Keep these scaled pixels, freeing whatever was kept before; the buffer
   belongs to the cache from here on, so no caller frees it. */
static void im_pixput(const char *path, int w, int h, unsigned char *px)
{
	struct stat st;

	free(im_pix.path);
	free(im_pix.px);
	im_pix.path = strdup(path);
	im_pix.w = w;
	im_pix.h = h;
	im_pix.px = px;
	im_pix.mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
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
	int row, col, rows, cols, gray = 0, mode = IM_AUTO, under = 0;
	int prow = 0, pcol = 0, ph = 0, pw = 0;
	image im;
	str err;
	cell *grid;
	int r, c, i;

	if (ac < 7) {
		lg(HIBR_LERR, "usage: img draw file row col h w [-g] [-m mode] "
			      "[-p pane] [-u]");
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
		else if (!strcmp(av[i], "-u"))
			under = 1;
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

				/* The same picture to the same place, under the
				   text, and the display still has it: keeping
				   it is the whole of what placing it again
				   would have said, without the hash of every
				   pixel -- and without the scaled copy, which
				   another picture may have taken the one cache
				   slot for. */
				if (under && !pane && dp->imgkeep &&
				    im_same(path, iw, ih, row, col, rows, cols) &&
				    dp->imgkeep(s, row, col, rows, cols)) {
					lg(HIBR_LDBG, "img draw: kept the "
					   "picture already placed at %d,%d",
					   row, col);
					return HIBR_OK;
				}
				px = im_pixget(path, iw, ih);
				if (!px) {
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
					im_pixput(path, iw, ih, px);
					lg(HIBR_LDBG, "img draw: scaled %s to %dx%d pixels",
					   path, iw, ih);
				}
				done = dp->image(s, pane, row, col, rows, cols,
						 px, iw, ih,
						 DP_IMG_CHOSEN |
						 (under ? DP_IMG_UNDER : 0));
				if (done) {
					if (under && !pane)
						im_placed(path, iw, ih, row,
							  col, rows, cols);
					return HIBR_OK;
				}
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
		/* Said outright, because a picture redrawn every frame that
		   misses is the difference between a tenth of a millisecond
		   and a full decode -- 268 ms for a 3840x2160 photograph --
		   and nothing on screen says which happened. `hibr -d 3` on a
		   desktop whose wallpaper keeps being re-read will name the
		   size it asked for, which is what says whether the screen
		   size is moving under it (Gitea #105). */
		lg(HIBR_LDBG, "img draw: %s at %dx%d is not cached, decoding",
		   path, rows, cols);
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
/* How big a picture is, from its own header: a PNG says so in the IHDR its
   signature is followed by, a JPEG in whichever SOF marker it opens a frame
   with. Reading 64 bytes rather than decoding, because the whole point of
   asking is usually to work out what size to draw at -- the desktop's
   wallpaper asks on every change of screen size -- and a full decode of a
   3840x2160 photograph to answer with two integers took 206 ms, once a
   second in a real session, which is 20% of a core spent on arithmetic
   nobody needed (Gitea #105). 0 when the header is not one we know, and
   then the caller decodes. */
int im_hdrsize(const char *path, int *w, int *h)
{
	unsigned char *b;
	size_t n, i, cap = 1 << 16;
	FILE *f = fopen(path, "rb");
	int ok = 0;

	if (!f)
		return 0;
	b = xm(cap);
	n = fread(b, 1, cap, f);
	fclose(f);
	if (n >= 24 && !memcmp(b, "\211PNG\r\n\032\n", 8) &&
	    !memcmp(b + 12, "IHDR", 4)) {
		*w = (int)((unsigned)b[16] << 24 | (unsigned)b[17] << 16 |
			   (unsigned)b[18] << 8 | b[19]);
		*h = (int)((unsigned)b[20] << 24 | (unsigned)b[21] << 16 |
			   (unsigned)b[22] << 8 | b[23]);
		ok = *w > 0 && *h > 0;
	} else if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
		i = 2;
		while (i + 9 <= n && b[i] == 0xFF) {
			size_t len = (size_t)b[i + 2] << 8 | b[i + 3];

			if (b[i + 1] >= 0xC0 && b[i + 1] <= 0xCF &&
			    b[i + 1] != 0xC4 && b[i + 1] != 0xC8 &&
			    b[i + 1] != 0xCC) {
				*h = (int)((unsigned)b[i + 5] << 8 | b[i + 6]);
				*w = (int)((unsigned)b[i + 7] << 8 | b[i + 8]);
				ok = *w > 0 && *h > 0;
				break;
			}
			if (b[i + 1] == 0xD8 ||
			    (b[i + 1] >= 0xD0 && b[i + 1] <= 0xD9)) {
				i += 2;
				continue;
			}
			if (len < 2)
				break;
			i += 2 + len;
		}
		/* A photograph from a phone says which way up it is and the
		   decoder turns it, so the size is turned with it -- or this
		   and `img draw` disagree about a rotated one, which is the
		   one way a header read can be wrong where a decode is not. */
		if (ok) {
			int o = jp_orient(b, n);

			if (o >= 5 && o <= 8) {
				int t = *w;

				*w = *h;
				*h = t;
			}
		}
	}
	free(b);
	return ok;
}

static int im_size(sh *s, int ac, char **av)
{
	image im;
	str err, t;
	int w = 0, h = 0;

	if (ac < 3) {
		lg(HIBR_LERR, "usage: img size file");
		return 2;
	}
	if (!im_hdrsize(av[2], &w, &h)) {
		s_init(&err);
		if (im_load(av[2], &im, &err) != HIBR_OK) {
			lg(HIBR_LERR, "%s", err.p ? err.p : "decode failed");
			s_free(&err);
			return HIBR_FAIL;
		}
		s_free(&err);
		w = im.w;
		h = im.h;
		im_free(&im);
	}
	s_init(&t);
	s_num(&t, w);
	s_ch(&t, ' ');
	s_num(&t, h);
	hibr_ret(s, t.p);
	if (!s->bind)
		printf("%s\n", t.p);
	s_free(&t);
	return HIBR_OK;
}

/* img keep file row col h w [-m mode]: the picture this module last placed
   under the text at exactly this rectangle, unchanged on disk, is still
   wanted -- the display keeps what it has and no pixel is handed over.
   Status 0 when it was kept, 1 when it was not, and *nothing is drawn*
   either way. That last part is why this is not a flag on `img draw`:
   draw falls back to half blocks whenever pixels cannot be placed, writing
   a cell per cell at absolute coordinates, which on a frame that meant to
   paint nothing would land on top of every window. A caller that cannot
   tell the two apart would have no safe way to ask. */
static int im_keep(sh *s, int ac, char **av)
{
	const dp_api *dp;
	const char *path;
	int row, col, rows, cols, cw = 0, chh = 0, mode = IM_AUTO, i;

	if (ac < 7) {
		lg(HIBR_LERR, "usage: img keep file row col h w [-m mode]");
		return 2;
	}
	path = av[2];
	row = atoi(av[3]);
	col = atoi(av[4]);
	rows = atoi(av[5]);
	cols = atoi(av[6]);
	for (i = 7; i < ac; i++)
		if (!strcmp(av[i], "-m") && i + 1 < ac)
			mode = im_mode(av[++i]);
	if (rows < 1 || cols < 1)
		return HIBR_FAIL;
	if (mode != IM_SIXEL && mode != IM_AUTO)
		return HIBR_FAIL;
	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp || !dp->isopen() || !dp->imgkeep || !dp->cellpx)
		return HIBR_FAIL;
	dp->cellpx(&cw, &chh);
	if (cw < 2 || chh < 2)
		return HIBR_FAIL;
	if (!im_same(path, cols * cw, rows * chh, row, col, rows, cols))
		return HIBR_FAIL;
	if (!dp->imgkeep(s, row, col, rows, cols))
		return HIBR_FAIL;
	lg(HIBR_LDBG, "img keep: %s kept at %d,%d", path, row, col);
	return HIBR_OK;
}

static int m_img(sh *s, int ac, char **av)
{
	if (ac > 1 && !strcmp(av[1], "draw"))
		return im_draw(s, ac, av);
	if (ac > 1 && !strcmp(av[1], "keep"))
		return im_keep(s, ac, av);
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
