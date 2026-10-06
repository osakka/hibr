/* img -- the pictures a program put on this terminal, kept rather than
 * dropped.
 *
 * The emulator draws cells, and a bitmap is not cells: every sixel and every
 * kitty escape used to be consumed and thrown away, which is exactly right
 * for a terminal *window* (nothing in the desktop can render one inside a
 * window) and exactly wrong for `hold`, which re-serialises this grid onto
 * somebody's real terminal. There, a picture the program sent and the
 * emulator dropped is a picture nobody ever sees -- the OSC 52 trap again
 * (Gitea #103): an emulator that redraws loses what it does not draw.
 *
 * So a picture is kept as a region: where it starts, how many cells it
 * covers, and the escapes exactly as they came. Whatever re-serialises this
 * grid re-emits each one after its own frame, at the region's own corner. A
 * region goes when anything writes a cell inside it, when the screen is
 * cleared, scrolled or reset, or -- for the kitty protocol, where a picture
 * is an object with an id -- when the program deletes it.
 *
 * The cell size is the one thing this cannot work out for itself: a sixel
 * says how many *pixels* it covers, and only whoever owns the real terminal
 * knows what a cell measures there. Without it a sixel is not kept at all,
 * which is honest: it could not be placed.
 */
#include "tm.h"
#include <stdlib.h>
#include <string.h>

/* Forget one region. */
void tm_imgdrop(tm_t *t, int i)
{
	if (i < 0 || i >= t->imgn)
		return;
	s_free(&t->img[i].data);
	t->img[i] = t->img[t->imgn - 1];
	t->imgn--;
	t->imgen++;
}

/* Forget all of them: the screen was cleared, the buffer switched, the
   terminal resized or reset. */
void tm_imgclear(tm_t *t)
{
	while (t->imgn)
		tm_imgdrop(t, t->imgn - 1);
}

/* Drop whatever covers this cell, because something has just been drawn
   through it. */
void tm_imghit(tm_t *t, int r, int c)
{
	int i = 0;

	while (i < t->imgn) {
		tm_img *m = &t->img[i];

		if (r >= m->r && r < m->r + m->rows &&
		    c >= m->c && c < m->c + m->cols) {
			lg(HIBR_LDBG, "terminal %d: picture at %d,%d drawn "
				      "through", t->id, m->r, m->c);
			tm_imgdrop(t, i);
			continue;
		}
		i++;
	}
}

/* Keep a picture: rows by cols cells at r,c, the bytes as they came. id is
   the kitty protocol's own image id, or 0 for a sixel, which has none. One
   with the same id, or in the same place, replaces what was there. */
void tm_imgkeep(tm_t *t, int r, int c, int rows, int cols, unsigned id,
		const char *p, size_t n)
{
	tm_img *m;
	int i;

	if (rows < 1 || cols < 1 || !n || n > TM_IMGMAX)
		return;
	for (i = 0; i < t->imgn; i++)
		if ((id && t->img[i].id == id) ||
		    (t->img[i].r == r && t->img[i].c == c)) {
			tm_imgdrop(t, i);
			break;
		}
	if (t->imgn == t->imgcap) {
		int nc = t->imgcap ? t->imgcap * 2 : 4;
		tm_img *na = xm((size_t)nc * sizeof *na);

		memset(na, 0, (size_t)nc * sizeof *na);
		if (t->imgn)
			memcpy(na, t->img, (size_t)t->imgn * sizeof *na);
		free(t->img);
		t->img = na;
		t->imgcap = nc;
	}
	m = &t->img[t->imgn++];
	memset(m, 0, sizeof *m);
	m->r = r;
	m->c = c;
	m->rows = rows;
	m->cols = cols;
	m->id = id;
	s_init(&m->data);
	s_add(&m->data, p, n);
	t->imgen++;
	lg(HIBR_LDBG, "terminal %d: picture of %dx%d cells at %d,%d, %zu bytes",
	   t->id, cols, rows, r, c, n);
}

/* One key's number out of a kitty control string: tm_imgkey("a=T,i=7", 'i')
   is 7, and -1 when the key is not there. Every key is one letter. */
long tm_imgkey(const char *s, int k)
{
	const char *p = s;

	while (*p) {
		if (p[0] == k && p[1] == '=')
			return strtol(p + 2, 0, 10);
		while (*p && *p != ',' && *p != ';')
			p++;
		if (*p != ',')
			break;
		p++;
	}
	return -1;
}

/* The same for a key whose value is a letter rather than a number (a=, d=,
   t=, o=); 0 when it is not there. */
int tm_imgkeyc(const char *s, int k)
{
	const char *p = s;

	while (*p) {
		if (p[0] == k && p[1] == '=')
			return p[2];
		while (*p && *p != ',' && *p != ';')
			p++;
		if (*p != ',')
			break;
		p++;
	}
	return 0;
}

/* This APC string, whole, as it arrived. */
void tm_apcraw(tm_t *t, str *o)
{
	s_cat(o, "\033_");
	s_add(o, t->os.p ? t->os.p : "", t->os.n);
	s_cat(o, "\033\\");
}

/* An APC string is complete. The kitty graphics protocol is the only one
   that matters here, and it opens with G: a transmission that displays
   (a=T) becomes a region at the cursor, chunk by chunk if it is chunked
   (m=1), and a delete (a=d) takes one or all of them away. Any other APC,
   and a transmission that does not display, is dropped as it always was. */
void tm_apcend(tm_t *t)
{
	const char *s = t->os.p ? t->os.p : "";
	long rows, cols;
	int act, d, i;

	if (t->os.n < 2 || *s != 'G')
		return;
	s++;
	act = tm_imgkeyc(s, 'a');
	if (act == 'd') {
		d = tm_imgkeyc(s, 'd');
		if (d == 'A' || d == 'a') {
			tm_imgclear(t);
			return;
		}
		for (i = 0; i < t->imgn; i++) {
			long id = tm_imgkey(s, 'i');

			if (id > 0 && t->img[i].id == (unsigned)id) {
				tm_imgdrop(t, i);
				break;
			}
		}
		return;
	}
	if (act == 'T') {
		rows = tm_imgkey(s, 'r');
		cols = tm_imgkey(s, 'c');
		if (rows < 1 || cols < 1)
			return;
		t->imgr = t->cr;
		t->imgc = t->cc;
		t->imgrows = (int)rows;
		t->imgcols = (int)cols;
		t->imgid = (unsigned)tm_imgkey(s, 'i');
		t->imgb.n = 0;
		if (t->imgb.p)
			t->imgb.p[0] = 0;
		t->imgon = 1;
		tm_apcraw(t, &t->imgb);
	} else if (t->imgon && !act) {
		/* A chunk after the first carries no action of its own. */
		tm_apcraw(t, &t->imgb);
	} else {
		return;
	}
	if (tm_imgkey(s, 'm') == 1 && t->imgb.n < TM_IMGMAX)
		return;
	tm_imgkeep(t, t->imgr, t->imgc, t->imgrows, t->imgcols, t->imgid,
		   t->imgb.p, t->imgb.n);
	t->imgb.n = 0;
	t->imgon = 0;
}

/* The DCS parameters this string arrived with, as they came, so what is sent
   on is the escape the program wrote rather than one of our own. */
void tm_imgparams(tm_t *t, str *o)
{
	int i;

	for (i = 0; i < t->pn; i++) {
		if (i)
			s_ch(o, ';');
		s_num(o, t->pv[i]);
	}
}

/* A sixel is complete: its own raster attributes say how many pixels it
   covers (`"Pan;Pad;Ph;Pv`, which is what this project's own encoder
   writes), and the cell size says how many cells that is. Without either it
   is not kept -- there would be no way to place it. */
void tm_sixelend(tm_t *t)
{
	const char *p = t->os.p ? t->os.p : "";
	char *e;
	long w, h;
	int rows, cols;
	str o;

	if (t->cellw < 2 || t->cellh < 2 || *p != '"')
		return;
	p++;
	while (*p && *p != ';')
		p++;
	if (*p != ';')
		return;
	p++;
	while (*p && *p != ';')
		p++;
	if (*p != ';')
		return;
	w = strtol(p + 1, &e, 10);
	if (*e != ';')
		return;
	h = strtol(e + 1, &e, 10);
	if (w < 1 || h < 1)
		return;
	cols = (int)((w + t->cellw - 1) / t->cellw);
	rows = (int)((h + t->cellh - 1) / t->cellh);
	s_init(&o);
	s_cat(&o, "\033P");
	tm_imgparams(t, &o);
	s_ch(&o, 'q');
	s_add(&o, t->os.p, t->os.n);
	s_cat(&o, "\033\\");
	tm_imgkeep(t, t->cr, t->cc, rows, cols, 0, o.p, o.n);
	s_free(&o);
}
