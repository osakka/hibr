#define _GNU_SOURCE

#include "cn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cn_fd;
extern int cn_on;
void cn_wr(int fd, const char *p, size_t n);

cn_grid cn_back, cn_front;
/* Which cells a pane covers, one byte each, and whether absolute writes are
   currently skipping them. The wallpaper is painted "behind" the windows:
   it would otherwise paint over them, which is the whole reason a window
   nobody redrew could not simply be left alone (ADR 0038). The map is built
   when something asks and a pane has moved since -- about 20 us for a
   screen -- rather than per cell, which would be ten rectangle tests for
   every one of 16,472 cells. Only writes at absolute coordinates skip: a
   menu dropping over a window, or any drawing through a pane, must still
   land. */
static unsigned char *cn_own;
/* The map's own size, not the grid's: a SIGWINCH is acted on by cn_fitq in
   the middle of a frame, so the grid can grow after the map was built, and a
   bounds check against the grid then indexes the map past the end of its own
   allocation. That read killed a live desktop with a signal 11 the first time
   its terminal was resized. */
static int cn_ownr, cn_ownc, cn_ownok, cn_behindon;
static int cn_skip(int row, int col);
unsigned cn_fg, cn_bg, cn_penat;
int cn_crow, cn_ccol, cn_cvis;
unsigned cn_lfg, cn_lbg, cn_lat;
int cn_lset, cn_lvis = -1;

/* What a cell left at the terminal's own default colour should darken
   toward -- the console has no way to ask the terminal what that colour
   actually is, so a caller with an opinion (the desktop, from its own
   theme) sets one; DP_DEFAULT (0) either way means none was ever set,
   which is also the value cn_dim1 already leaves an untouched cell at. */
unsigned cn_deffg, cn_defbg;

/* Set what DP_DEFAULT should darken toward. */
void cn_setdim(unsigned fg, unsigned bg)
{
	cn_deffg = fg;
	cn_defbg = bg;
}

/* Release every cell and the grid itself. */
void cn_gfree(cn_grid *g)
{
	int i, n = g->rows * g->cols;

	for (i = 0; i < n; i++)
		free(g->c[i].ext);
	free(g->c);
	g->c = 0;
	g->rows = 0;
	g->cols = 0;
}

/* Resize a grid, discarding what it held. */
int cn_gsize(cn_grid *g, int rows, int cols)
{
	int i, n = rows * cols;

	if (rows <= 0 || cols <= 0)
		return HIBR_FAIL;
	if (g->rows == rows && g->cols == cols)
		return HIBR_OK;
	cn_gfree(g);
	g->c = xm((size_t)n * sizeof *g->c);
	memset(g->c, 0, (size_t)n * sizeof *g->c);
	for (i = 0; i < n; i++) {
		g->c[i].cp = ' ';
		g->c[i].w = 1;
	}
	g->rows = rows;
	g->cols = cols;
	return HIBR_OK;
}

/* Set a cell's character, keeping any trailing combining marks. */
void cn_cellset(cn_cell *c, unsigned cp, const char *ext, size_t en)
{
	free(c->ext);
	c->ext = 0;
	c->cp = cp;
	if (ext && en) {
		c->ext = xm(en + 1);
		memcpy(c->ext, ext, en);
		c->ext[en] = 0;
	}
}

/* Make the front buffer describe nothing, so the next flush paints it all. */
void cn_inval(void)
{
	int i, n = cn_front.rows * cn_front.cols;

	/* Everything is painted again, which paints over any picture: the
	   regions go, and whatever still wants one places it afresh. */
	cn_imgclear();
	for (i = 0; i < n; i++)
		cn_front.c[i].cp = 0xFFFFFFFFu;
	cn_lset = 0;
	cn_lvis = -1;
}

/* Make both grids match the terminal, after a resize or at open. */
int cn_fit(void)
{
	int rows, cols;

	cn_size(&rows, &cols);
	if (cn_back.rows == rows && cn_back.cols == cols)
		return HIBR_OK;
	if (cn_gsize(&cn_back, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	if (cn_gsize(&cn_front, rows, cols) != HIBR_OK)
		return HIBR_FAIL;
	cn_inval();
	lg(HIBR_LDBG, "screen now %d by %d", rows, cols);
	return HIBR_OK;
}

/* cn_fit for a write: the size the last fit found, unless a resize has
   arrived since. Asking the kernel on every write made each cell of a fill
   a system call -- 1920 of them for one wallpaper. cn_flush still asks on
   every frame. */
int cn_fitg = -1;
int cn_fitq(void)
{
	if (cn_back.c && cn_fitg == (int)cn_wgen)
		return HIBR_OK;
	cn_fitg = (int)cn_wgen;
	return cn_fit();
}

/* Links later writes carry, by number: 0 is none, n is cn_lk[n - 1]. The
   pen's link is cn_plink; cn_llink is the one the terminal was last left
   inside, always none between flushes. */
char **cn_lk;
int cn_lkn, cn_lkcap;
unsigned cn_plink, cn_llink;

/* Make later writes part of a link to uri, or of none for null or "". */
void cn_link(const char *uri)
{
	int i;

	cn_plink = 0;
	if (!uri || !*uri)
		return;
	for (i = 0; i < cn_lkn; i++)
		if (!strcmp(cn_lk[i], uri)) {
			cn_plink = (unsigned)i + 1;
			return;
		}
	if (cn_lkn == cn_lkcap) {
		cn_lkcap = cn_lkcap ? cn_lkcap * 2 : 8;
		cn_lk = xr(cn_lk, (size_t)cn_lkcap * sizeof *cn_lk);
	}
	cn_lk[cn_lkn] = xm(strlen(uri) + 1);
	strcpy(cn_lk[cn_lkn], uri);
	cn_plink = (unsigned)++cn_lkn;
}

/* Set the pen used by later writes. */
void cn_pen(unsigned fg, unsigned bg, unsigned attr)
{
	cn_fg = fg;
	cn_bg = bg;
	cn_penat = attr;
}

/* Read the pen back. */
void cn_getpen(unsigned *fg, unsigned *bg, unsigned *attr)
{
	*fg = cn_fg;
	*bg = cn_bg;
	*attr = cn_penat;
}

/* Blank the back buffer with the current pen. */
void cn_clear(void)
{
	int i, n;

	if (cn_fitq() != HIBR_OK)
		return;
	n = cn_back.rows * cn_back.cols;
	for (i = 0; i < n; i++) {
		cn_cellset(&cn_back.c[i], ' ', 0, 0);
		cn_back.c[i].w = 1;
		cn_back.c[i].cont = 0;
		cn_back.c[i].fg = cn_fg;
		cn_back.c[i].bg = cn_bg;
		cn_back.c[i].attr = cn_penat;
		cn_back.c[i].link = 0;
	}
}

/* Darken one colour toward black by pct percent of its own value. RGB
   scales exactly. deflt is what a cell left at the terminal's own
   default colour (DP_DEFAULT, 0) darkens toward instead -- cn_setdim's
   own reference, so a shadow over a terminal's own plain, uncoloured
   output (a prompt, ls of regular files, most of what actually appears
   on screen, all of it DP_DEFAULT since mods/term never colours a cell
   the program itself did not ask to be coloured) darkens the same as the
   decoration around it does, rather than not at all. A palette index is
   a program's own deliberate colour choice, not a placeholder for "none
   set" the way DP_DEFAULT is, so it is left exactly as DP_DEFAULT always
   was: unscaled without a colour table the display does not have, DP_DIM
   the fallback that reaches every terminal regardless. */
unsigned cn_dim1(unsigned v, int pct, unsigned deflt)
{
	unsigned r, g, b;

	if (v == DP_DEFAULT)
		v = deflt;
	if (!(v & DP_RGB))
		return v;
	r = (v >> 16) & 0xFF;
	g = (v >> 8) & 0xFF;
	b = v & 0xFF;
	r = (r * (unsigned)pct + 50) / 100;
	g = (g * (unsigned)pct + 50) / 100;
	b = (b * (unsigned)pct + 50) / 100;
	return DP_RGB | (r << 16) | (g << 8) | b;
}

/* Darken a rectangle of the back buffer in place, in front of whatever is
   drawn under it -- a window's shadow, cast on the desktop and on windows
   below it, is this over their already-drawn cells before the window
   drawing on top of it goes in and paints over its own footprint.  Nothing
   is undone: a fixed shape darkened once a frame, not state carried between
   frames, so there is nothing to restore when the window moves. */
void cn_darken(int row, int col, int h, int w, int pct)
{
	cn_darken1(row, col, h, w, pct, 0);
}

/* The same, and `once` makes it a shadow: a cell it has already darkened is
   left alone, and one it darkens is marked. Casting the same shadow on every
   frame then costs the first one and changes nothing after it, so nothing has
   to work out which frame is allowed to cast it -- and any ordinary write to
   the cell clears the mark, so the wallpaper painting underneath, or a window
   moving away, gets its shadow cast afresh. */
void cn_darken1(int row, int col, int h, int w, int pct, int once)
{
	int r, c;
	cn_cell *k;

	if (cn_fitq() != HIBR_OK)
		return;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	for (r = row; r < row + h; r++) {
		if (r < 0 || r >= cn_back.rows)
			continue;
		for (c = col; c < col + w; c++) {
			if (c < 0 || c >= cn_back.cols)
				continue;
			k = &cn_back.c[r * cn_back.cols + c];
			if (once && (k->attr & CN_SHADOWED))
				continue;
			k->fg = cn_dim1(k->fg, pct, cn_deffg);
			k->bg = cn_dim1(k->bg, pct, cn_defbg);
			k->attr |= DP_DIM;
			if (once)
				k->attr |= CN_SHADOWED;
		}
	}
}

/* Overwrite a cell that is half of a wide glyph, and its other half. */
void cn_split(int row, int col)
{
	cn_cell *c;

	if (row < 0 || row >= cn_back.rows || col < 0 || col >= cn_back.cols)
		return;
	c = &cn_back.c[row * cn_back.cols + col];
	if (c->cont && col > 0) {
		cn_cellset(c - 1, ' ', 0, 0);
		(c - 1)->w = 1;
	}
	if (c->w == 2 && col + 1 < cn_back.cols) {
		cn_cellset(c + 1, ' ', 0, 0);
		(c + 1)->w = 1;
		(c + 1)->cont = 0;
	}
}

/* Write text into the back buffer, returning the columns it advanced. */
int cn_put(int row, int col, const char *t)
{
	size_t n, i = 0;
	int start = col, w, l;
	unsigned cp;
	cn_cell *c;

	if (cn_fitq() != HIBR_OK || !t)
		return 0;
	n = strlen(t);
	while (i < n) {
		l = u8dec(t + i, n - i, &cp);
		w = u8w(cp);
		if (w == 0) {
			if (col > start && row >= 0 && row < cn_back.rows) {
				c = &cn_back.c[row * cn_back.cols + col - 1];
				if (!c->ext)
					cn_cellset(c, c->cp, t + i, (size_t)l);
				else {
					size_t o = strlen(c->ext);
					char *e = xm(o + (size_t)l + 1);
					memcpy(e, c->ext, o);
					memcpy(e + o, t + i, (size_t)l);
					e[o + (size_t)l] = 0;
					free(c->ext);
					c->ext = e;
				}
			}
			i += (size_t)l;
			continue;
		}
		if (row < 0 || row >= cn_back.rows || col < 0 ||
		    col + w > cn_back.cols || cn_skip(row, col)) {
			col += w;
			i += (size_t)l;
			continue;
		}
		cn_split(row, col);
		if (w == 2)
			cn_split(row, col + 1);
		c = &cn_back.c[row * cn_back.cols + col];
		cn_cellset(c, cp, 0, 0);
		c->w = (unsigned char)w;
		c->cont = 0;
		c->fg = cn_fg;
		c->bg = cn_bg;
		c->attr = cn_penat;
		c->link = cn_plink;
		if (w == 2) {
			cn_cellset(c + 1, 0, 0, 0);
			(c + 1)->w = 0;
			(c + 1)->cont = 1;
			(c + 1)->fg = cn_fg;
			(c + 1)->bg = cn_bg;
			(c + 1)->attr = cn_penat;
			(c + 1)->link = cn_plink;
		}
		col += w;
		i += (size_t)l;
	}
	return col - start;
}

/* Repeat one character over a rectangle.

   The glyph is decoded once rather than once a cell. Through cn_put every
   cell paid a strlen, a u8dec and a u8w to learn again what the one before
   it had already said, and a wallpaper is 16,472 cells: that fill was 1.6 ms
   of a 6.7 ms frame, and every window's own face is another fill. The fast
   path takes only a single codepoint one column wide -- a space, or one of
   the wallpaper glyphs -- and writes exactly the cell cn_put would have,
   cn_split and the freed combining characters included. Anything else, a
   wide glyph or a string, still goes through cn_put. */
void cn_fill(int row, int col, int h, int w, const char *t)
{
	int r, c, adv, l, gw;
	unsigned cp;
	cn_cell *p;

	if (!t || !*t)
		t = " ";
	if (cn_fitq() != HIBR_OK)
		return;
	l = u8dec(t, strlen(t), &cp);
	gw = u8w(cp);
	if (gw == 1 && t[l] == 0) {
		for (r = row; r < row + h; r++) {
			if (r < 0 || r >= cn_back.rows)
				continue;
			for (c = col; c < col + w; c++) {
				if (c < 0 || c >= cn_back.cols ||
				    cn_skip(r, c))
					continue;
				cn_split(r, c);
				p = &cn_back.c[r * cn_back.cols + c];
				free(p->ext);
				p->ext = 0;
				p->cp = cp;
				p->w = 1;
				p->cont = 0;
				p->fg = cn_fg;
				p->bg = cn_bg;
				p->attr = cn_penat;
				p->link = cn_plink;
			}
		}
		return;
	}
	for (r = row; r < row + h; r++)
		for (c = col; c < col + w;) {
			adv = cn_put(r, c, t);
			c += adv > 0 ? adv : 1;
		}
}

/* Place the cursor, and say whether it should be seen. */
void cn_cursor(int row, int col, int vis)
{
	cn_crow = row;
	cn_ccol = col;
	cn_cvis = vis;
}

/* Append the escape sequence for one colour. */
void cn_sgrcol(str *b, unsigned v, int bgp)
{
	if (v == DP_DEFAULT) {
		s_cat(b, bgp ? ";49" : ";39");
		return;
	}
	if (v & DP_RGB) {
		s_cat(b, bgp ? ";48;2;" : ";38;2;");
		s_num(b, (long)((v >> 16) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)((v >> 8) & 0xFF));
		s_ch(b, ';');
		s_num(b, (long)(v & 0xFF));
		return;
	}
	s_cat(b, bgp ? ";48;5;" : ";38;5;");
	s_num(b, (long)(v & 0xFF));
}

/* Append the escape sequence that moves from one pen to another. */
void cn_sgr(str *b, unsigned fg, unsigned bg, unsigned at)
{
	s_cat(b, "\033[0");
	if (at & DP_BOLD)
		s_cat(b, ";1");
	if (at & DP_DIM)
		s_cat(b, ";2");
	if (at & DP_ITAL)
		s_cat(b, ";3");
	if (at & DP_UNDER)
		s_cat(b, ";4");
	if (at & DP_BLINK)
		s_cat(b, ";5");
	if (at & DP_REV)
		s_cat(b, ";7");
	if (at & DP_STRIKE)
		s_cat(b, ";9");
	cn_sgrcol(b, fg, 0);
	cn_sgrcol(b, bg, 1);
	s_ch(b, 'm');
}

/* Append a cursor move, preferring the shorter form when on the same row. */
void cn_goto(str *b, int row, int col, int cr, int cc)
{
	if (row == cr && col == cc)
		return;
	if (row == cr && col == cc + 1) {
		s_cat(b, "\033[C");
		return;
	}
	s_cat(b, "\033[");
	s_num(b, (long)(row + 1));
	s_ch(b, ';');
	s_num(b, (long)(col + 1));
	s_ch(b, 'H');
}

/* True when two cells would draw identically. */
int cn_same(const cn_cell *a, const cn_cell *b)
{
	if (a->cp != b->cp || a->fg != b->fg || a->bg != b->bg ||
	    a->attr != b->attr || a->w != b->w || a->cont != b->cont ||
	    a->link != b->link)
		return 0;
	if (!a->ext && !b->ext)
		return 1;
	if (!a->ext || !b->ext)
		return 0;
	return !strcmp(a->ext, b->ext);
}

/* Encode one codepoint as UTF-8 into the buffer. */
void cn_enc(str *b, unsigned cp)
{
	if (cp < 0x80) {
		s_ch(b, (char)cp);
	} else if (cp < 0x800) {
		s_ch(b, (char)(0xC0 | (cp >> 6)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		s_ch(b, (char)(0xE0 | (cp >> 12)));
		s_ch(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	} else {
		s_ch(b, (char)(0xF0 | (cp >> 18)));
		s_ch(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
		s_ch(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
		s_ch(b, (char)(0x80 | (cp & 0x3F)));
	}
}

/* True when a changed cell is close enough that moving costs more than drawing. */
int cn_near(int r, int c)
{
	int k;

	for (k = c; k < c + 3 && k < cn_back.cols; k++)
		if (!cn_same(&cn_back.c[r * cn_back.cols + k],
			      &cn_front.c[r * cn_front.cols + k]))
			return 1;
	return 0;
}

/* A pane moved, was made or dropped: the map is stale. */
void cn_owninval(void)
{
	cn_ownok = 0;
}

/* Make the map describe the panes as they are now. */
static void cn_ownsync(void)
{
	size_t i;
	int r, c, n = cn_back.rows * cn_back.cols;
	cn_pane *p;

	if (cn_ownok && cn_ownr == cn_back.rows && cn_ownc == cn_back.cols)
		return;
	free(cn_own);
	cn_own = xm((size_t)n);
	memset(cn_own, 0, (size_t)n);
	cn_ownr = cn_back.rows;
	cn_ownc = cn_back.cols;
	for (i = 0; i < cn_panes.n; i++) {
		p = (cn_pane *)cn_panes.p[i];
		for (r = p->row; r < p->row + p->h; r++) {
			if (r < 0 || r >= cn_back.rows)
				continue;
			for (c = p->col; c < p->col + p->w; c++) {
				if (c < 0 || c >= cn_back.cols)
					continue;
				cn_own[r * cn_back.cols + c] = 1;
			}
		}
	}
	cn_ownok = 1;
}

/* Whether this cell belongs to a pane and is being skipped. */
static int cn_skip(int row, int col)
{
	if (!cn_behindon || !cn_own)
		return 0;
	/* Against the map's own size. A cell the map does not cover is one
	   the screen has only just grown into, which no pane owns yet, so it
	   is painted rather than skipped. */
	if (row < 0 || row >= cn_ownr || col < 0 || col >= cn_ownc)
		return 0;
	return cn_own[row * cn_ownc + col];
}

/* Paint behind the panes, or stop doing so, answering how it was before --
   so a caller that must not skip, like a write through a pane, can put it
   back exactly as it found it. */
int cn_behind(int on)
{
	int was = cn_behindon;

	if (on) {
		if (cn_fitq() != HIBR_OK)
			return was;
		cn_ownsync();
	}
	cn_behindon = on;
	return was;
}

/* Send only the cells that changed, and return how many bytes that took. */
long cn_flush(void)
{
	int r, c, cr = -9, cc = -9;
	long n;
	str b;
	cn_cell *bk, *ft;

	if (!cn_on)
		return 0;
	if (cn_fit() != HIBR_OK)
		return 0;
	/* A picture whose cells have been drawn through since it was placed is
	   gone: dropping it here, before the diff, is what makes those cells
	   paint again. */
	cn_imgcheck();
	s_init(&b);
	/* Then the deletes those drops owe the terminal, for a protocol that
	   keeps a picture rather than painting it: they go before the diff, so
	   the text that was underneath is painted in the same frame. */
	cn_imgdels(&b);
	/* A picture text is drawn over goes out first, so the text lands on
	   top of it; the pen and the cursor are then unknown. */
	if (cn_imgn() && cn_imgsend(&b, 1)) {
		cn_lset = 0;
		cr = -9;
		cc = -9;
	}
	for (r = 0; r < cn_back.rows; r++) {
		for (c = 0; c < cn_back.cols; c++) {
			bk = &cn_back.c[r * cn_back.cols + c];
			ft = &cn_front.c[r * cn_front.cols + c];
			if (bk->cont)
				continue;
			/* Inside a picture: the cells belong to it, and the
			   front grid is left not knowing them, so the text
			   underneath paints the moment the picture goes. */
			if (cn_imgn() && cn_imgat(r, c))
				continue;
			if (cn_same(bk, ft) &&
			    !(cr == r && cc == c && cn_near(r, c)))
				continue;
			cn_goto(&b, r, c, cr, cc);
			if (!cn_lset || bk->fg != cn_lfg ||
			    bk->bg != cn_lbg || bk->attr != cn_lat) {
				cn_sgr(&b, bk->fg, bk->bg, bk->attr);
				cn_lfg = bk->fg;
				cn_lbg = bk->bg;
				cn_lat = bk->attr;
				cn_lset = 1;
			}
			if (bk->link != cn_llink) {
				s_cat(&b, "\033]8;;");
				if (bk->link && (int)bk->link <= cn_lkn)
					s_cat(&b, cn_lk[bk->link - 1]);
				s_ch(&b, 7);
				cn_llink = bk->link;
			}
			cn_enc(&b, bk->cp ? bk->cp : ' ');
			if (bk->ext)
				s_cat(&b, bk->ext);
			cr = r;
			cc = c + (bk->w ? bk->w : 1);
			cn_cellset(ft, bk->cp, bk->ext,
				   bk->ext ? strlen(bk->ext) : 0);
			ft->fg = bk->fg;
			ft->bg = bk->bg;
			ft->attr = bk->attr;
			ft->link = bk->link;
			ft->w = bk->w;
			ft->cont = bk->cont;
			if (bk->w == 2 && c + 1 < cn_back.cols) {
				cn_cell *b2 = bk + 1, *f2 = ft + 1;
				cn_cellset(f2, b2->cp, 0, 0);
				f2->w = b2->w;
				f2->cont = b2->cont;
				f2->fg = b2->fg;
				f2->bg = b2->bg;
				f2->attr = b2->attr;
				f2->link = b2->link;
			}
		}
	}
	if (cn_llink) {
		s_cat(&b, "\033]8;;\a");
		cn_llink = 0;
	}
	/* The pictures last, so nothing the diff writes lands on top of one;
	   each moves the cursor itself, so the pen and position are forgotten
	   and the cursor below is placed outright. */
	if (cn_imgn() && cn_imgsend(&b, 0)) {
		cr = -9;
		cc = -9;
		cn_lset = 0;
	}
	if (cn_cvis) {
		cn_goto(&b, cn_crow, cn_ccol, cr, cc);
		if (cn_lvis != 1)
			s_cat(&b, "\033[?25h");
		cn_lvis = 1;
	} else if (cn_lvis != 0) {
		s_cat(&b, "\033[?25l");
		cn_lvis = 0;
	}
	n = (long)b.n;
	if (b.n)
		cn_wr(cn_fd, b.p, b.n);
	s_free(&b);
	lg(HIBR_LDBG, "screen flush wrote %ld bytes", n);
	return n;
}
