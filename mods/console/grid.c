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
size_t cn_wrote;
static size_t cn_wflush;
static int cn_skip(int row, int col);
static int cn_gband(int row);
unsigned cn_fg, cn_bg, cn_penat;
/* While this is non-zero, a cell written with no background colour of its
   own, over a picture drawn under the text, takes that picture's colour
   scaled to this percentage instead -- a window the wallpaper shows
   through, which a cell cannot do with alpha because it has none (Gitea
   #184). It is the same question `darkdefault` answers for a *darkened*
   default cell, asked for a *written* one. A caller sets it around its own
   drawing, and cn_flush puts it back to 0 at the end of every frame, so an
   app that forgets cannot leak it into whatever is drawn next -- a guard in
   one place rather than a line each caller has to remember. */
static int cn_transpct;
/* What a translucent write lands on where there is no picture to be
   translucent over -- a glyph wallpaper, a terminal that cannot place a
   pixel, half blocks. Without it a caller would have to ask whether a
   picture is really there before deciding what to send, and be wrong
   whenever the answer changed between the asking and the drawing. */
static unsigned cn_transfb;
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
	cn_wrote++;
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

/* The background a translucent write lands on: the picture's own colour at
   that cell, scaled. With no picture under it there is nothing to be
   translucent over, and the cell keeps the default it was written with --
   so the same app draws correctly over a glyph wallpaper, on a terminal
   that cannot place pixels, and in half blocks, with no branch of its own. */
unsigned cn_bgtrans(int row, int col)
{
	unsigned pic;

	if (!cn_imgcellat(row, col, &pic))
		return cn_transfb;
	return cn_dim1(pic, cn_transpct, 0);
}

/* The background a cell is written with, which is its own except in the
   one case the terminal cannot tell from "no background at all": kitty
   decides whether a cell has one by comparing its *resolved* value with
   the window's own background colour, so a cell painted an explicit black
   on a black terminal is a cell a picture drawn under the text is let
   through -- an ncurses program in a window over a picture wallpaper was
   see-through where every window in a theme's own RGB was not (Gitea
   #215). One in the last channel is the whole substitution: the same
   colour to look at, and not the one the terminal is comparing against.
   Asked of every cell written, and of a translucent write's own computed
   colour as well: that is the picture's colour at the cell, scaled, so a
   black patch of a photograph under a shadow can land on exactly the
   terminal's background by arithmetic rather than by anybody's choice.
   The order is the cheap questions first -- a default background is the
   wallpaper's own and must stay let through, and resolving a colour is
   arithmetic where asking which pictures cover a cell is a walk. */
unsigned cn_opaque(int row, int col, unsigned bg)
{
	unsigned v;

	if (bg == DP_DEFAULT)
		return bg;
	v = cn_rgbof(bg);
	if (v != cn_termbg() || !cn_imgunderat(row, col))
		return bg;
	return DP_RGB | (v ^ 1);
}

/* How translucent the next writes are, and what it was before: a caller
   saves and restores around its own drawing, the way cn_behind is used. A
   negative percentage reads the current one without changing it, so asking
   never has a window where it is something else. */
int cn_translucent(int pct, unsigned fb)
{
	int was = cn_transpct;

	if (pct >= 0) {
		cn_transpct = pct > 100 ? 100 : pct;
		cn_transfb = fb;
	}
	return was;
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
	int r, c, g;
	unsigned pic;
	cn_cell *k;

	if (cn_fitq() != HIBR_OK)
		return;
	cn_wrote++;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	for (r = row; r < row + h; r++) {
		if (r < 0 || r >= cn_back.rows)
			continue;
		g = cn_gband(r);
		for (c = col; c < col + w; c++) {
			if (c < 0 || c >= cn_back.cols)
				continue;
			if (g && cn_ptopat(r, c))
				continue;
			k = &cn_back.c[r * cn_back.cols + c];
			if (once && (k->attr & CN_SHADOWED))
				continue;
			k->fg = cn_dim1(k->fg, pct, cn_deffg);
			/* A cell left at the default colour means two
			   different things, and only one of them is
			   cn_defbg's. Over a picture drawn under the text it
			   means "let the wallpaper through here" -- the
			   wallpaper writes exactly such cells on purpose --
			   so darkening toward the theme's own face painted an
			   opaque near-black band over the photograph and took
			   the picture with it (Gitea #184). The picture's own
			   colour is what a shadow on it looks like. Anywhere
			   else, including a glyph wallpaper and a terminal's
			   plain uncoloured output, cn_defbg is still right. */
			if (k->bg == DP_DEFAULT && cn_imgcellat(r, c, &pic))
				k->bg = cn_dim1(pic, pct, 0);
			else
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
	int start = col, w, l, g;
	unsigned cp;
	cn_cell *c;

	if (cn_fitq() != HIBR_OK || !t)
		return 0;
	cn_wrote++;
	n = strlen(t);
	g = cn_gband(row);
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
		    col + w > cn_back.cols || cn_skip(row, col) ||
		    (g && (cn_ptopat(row, col) ||
			   (w == 2 && cn_ptopat(row, col + 1))))) {
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
		c->bg = cn_opaque(row, col,
				  cn_transpct && cn_bg == DP_DEFAULT ?
					  cn_bgtrans(row, col) : cn_bg);
		c->attr = cn_penat;
		c->link = cn_plink;
		if (w == 2) {
			cn_cellset(c + 1, 0, 0, 0);
			(c + 1)->w = 0;
			(c + 1)->cont = 1;
			(c + 1)->fg = cn_fg;
			(c + 1)->bg = c->bg;
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
	int r, c, adv, l, gw, g;
	unsigned cp;
	cn_cell *p;

	if (!t || !*t)
		t = " ";
	if (cn_fitq() != HIBR_OK)
		return;
	cn_wrote++;
	l = u8dec(t, strlen(t), &cp);
	gw = u8w(cp);
	if (gw == 1 && t[l] == 0) {
		for (r = row; r < row + h; r++) {
			if (r < 0 || r >= cn_back.rows)
				continue;
			g = cn_gband(r);
			for (c = col; c < col + w; c++) {
				if (c < 0 || c >= cn_back.cols ||
				    cn_skip(r, c) || (g && cn_ptopat(r, c)))
					continue;
				cn_split(r, c);
				p = &cn_back.c[r * cn_back.cols + c];
				free(p->ext);
				p->ext = 0;
				p->cp = cp;
				p->w = 1;
				p->cont = 0;
				p->fg = cn_fg;
				p->bg = cn_opaque(r, c,
						  cn_transpct &&
						  cn_bg == DP_DEFAULT ?
							  cn_bgtrans(r, c) :
							  cn_bg);
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

/* The escape that moves the terminal's pen from what it is to this one:
   only the colour that changed when the attributes are the same, the full
   reset form otherwise. A cell over a translucent window or a picture
   differs from the one before it in its background alone, and sending the
   reset, the attributes and the foreground again for each one was half of
   what such a cell cost -- every frame, and every step of a drag. */
void cn_sgrto(str *b, unsigned fg, unsigned bg, unsigned at)
{
	size_t p;

	if (!cn_lset || at != cn_lat) {
		cn_sgr(b, fg, bg, at);
		return;
	}
	s_cat(b, "\033[");
	p = b->n;
	if (fg != cn_lfg)
		cn_sgrcol(b, fg, 0);
	if (bg != cn_lbg)
		cn_sgrcol(b, bg, 1);
	if (b->n > p && b->p[p] == ';') {
		memmove(b->p + p, b->p + p + 1, b->n - p - 1);
		b->n--;
		b->p[b->n] = 0;
	}
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
	cn_tok = 0;
}

/* Whether a row is one a top pane spans: asked once a row, never once a
   cell, so a row outside the band -- every row but the bar's -- costs the
   writers nothing per cell. Asking per cell measured 2 ns a cell, about
   35 us a frame at 232x71, against a 0.7 ms frame budget. */
static int cn_gband(int row)
{
	if (!cn_tok)
		cn_tband();
	return row >= cn_ttop && row < cn_tbot;
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

/* A rectangle drawn over the screen at flush and never into it: the outline
   a window is moved or resized by (`console outline`). Kept out of the back
   grid, so the cells it covers keep what is really there, and the cells it
   leaves differ from the front grid and paint themselves again -- nothing
   has to erase it, and nothing has to be redrawn for it to move. */
cn_ol cn_olay;

/* The outline's title, one cell a character: a glyph wider than a cell is
   drawn as a question mark rather than shifting the edge after it. */
void cn_oltitle(const char *t)
{
	size_t n = strlen(t), i = 0;
	unsigned cp;
	int l;

	free(cn_olay.tt);
	cn_olay.tt = xm((n + 1) * sizeof(unsigned));
	cn_olay.tn = 0;
	while (i < n && (l = u8dec(t + i, n - i, &cp)) > 0) {
		cn_olay.tt[cn_olay.tn++] = u8w(cp) == 1 ? cp : '?';
		i += (size_t)l;
	}
}

/* One glyph of the outline, from its UTF-8; a plus for one that is not. */
unsigned cn_olglyph(const char *t)
{
	unsigned cp;

	if (u8dec(t, strlen(t), &cp) < 1 || !cp)
		return '+';
	return cp;
}

/* The outline's glyph at a cell, or 0 where the outline is not. */
unsigned cn_olat(int r, int c)
{
	int r1 = cn_olay.r + cn_olay.h - 1, c1 = cn_olay.c + cn_olay.w - 1;

	if (r < cn_olay.r || r > r1 || c < cn_olay.c || c > c1)
		return 0;
	if (r == cn_olay.r) {
		if (c == cn_olay.c)
			return cn_olay.tl;
		if (c == c1)
			return cn_olay.tr;
		/* The title, a space either side, where the window's own
		   title bar has it -- so what is being moved still says
		   which window it is. */
		if (cn_olay.tn && c >= cn_olay.c + 1 && c < c1 - 1 &&
		    c <= cn_olay.c + 2 + cn_olay.tn) {
			int i = c - cn_olay.c - 2;
			if (i < 0 || i == cn_olay.tn)
				return ' ';
			return cn_olay.tt[i];
		}
		return cn_olay.hz;
	}
	if (r == r1)
		return c == cn_olay.c ? cn_olay.bl : c == c1 ? cn_olay.br :
		       cn_olay.hz;
	if (c == cn_olay.c || c == c1)
		return cn_olay.vt;
	return 0;
}

/* Send only the cells that changed, and return how many bytes that took. */
long cn_flush(void)
{
	int r, c, cr = -9, cc = -9;
	long n;
	str b;
	cn_cell *bk, *ft, *e, oc;
	unsigned g;

	if (!cn_on)
		return 0;
	if (cn_fit() != HIBR_OK)
		return 0;
	cn_askmaybe();
	/* Translucency lasts for the drawing, not for ever: a frame ends
	   here, so an app that sets it and never says otherwise cannot leak
	   it into whatever is drawn next. Each caller still turns it off
	   after its own drawing -- this is the guard that makes forgetting
	   impossible rather than merely unlikely, which is the difference
	   between a rule and a list nobody maintains. */
	cn_translucent(0, DP_DEFAULT);
	/* A picture whose cells have been drawn through since it was placed is
	   gone: dropping it here, before the diff, is what makes those cells
	   paint again. */
	/* The same frame flushed twice asks nothing about what is still
	   wanted, so it must retire nothing: shot.hibr's own `dt_draw;
	   console flush` was dropping the under-text wallpaper and taking the
	   screenshot without it (Gitea #118). */
	cn_imgcheck(cn_wrote == cn_wflush);
	cn_wflush = cn_wrote;
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
			e = bk;
			/* Under the outline: its glyph in its colour, on the
			   terminal's own background -- so a picture under the
			   text shows through it, and a run of outline cells is
			   one pen and three bytes a cell, where each taking its
			   cell's own background cost a colour change apiece. A
			   wide glyph half under it gives up its other half as a
			   blank. */
			if (cn_olay.on) {
				g = cn_olat(r, c);
				if (g || (bk->w == 2 && cn_olat(r, c + 1))) {
					oc = *bk;
					oc.cp = g ? g : ' ';
					oc.ext = 0;
					oc.w = 1;
					oc.cont = 0;
					oc.link = 0;
					if (g) {
						oc.fg = cn_olay.fg;
						oc.bg = DP_DEFAULT;
						oc.attr = 0;
					}
					e = &oc;
				}
			}
			if (e->cont)
				continue;
			/* Inside a picture: the cells belong to it, and the
			   front grid is left not knowing them, so the text
			   underneath paints the moment the picture goes. */
			if (cn_imgn() && cn_imgat(r, c))
				continue;
			if (cn_same(e, ft) &&
			    !(cr == r && cc == c && cn_near(r, c)))
				continue;
			cn_goto(&b, r, c, cr, cc);
			if (!cn_lset || e->fg != cn_lfg ||
			    e->bg != cn_lbg || e->attr != cn_lat) {
				cn_sgrto(&b, e->fg, e->bg, e->attr);
				cn_lfg = e->fg;
				cn_lbg = e->bg;
				cn_lat = e->attr;
				cn_lset = 1;
			}
			if (e->link != cn_llink) {
				s_cat(&b, "\033]8;;");
				if (e->link && (int)e->link <= cn_lkn)
					s_cat(&b, cn_lk[e->link - 1]);
				s_ch(&b, 7);
				cn_llink = e->link;
			}
			cn_enc(&b, e->cp ? e->cp : ' ');
			if (e->ext)
				s_cat(&b, e->ext);
			cr = r;
			cc = c + (e->w ? e->w : 1);
			cn_cellset(ft, e->cp, e->ext,
				   e->ext ? strlen(e->ext) : 0);
			ft->fg = e->fg;
			ft->bg = e->bg;
			ft->attr = e->attr;
			ft->link = e->link;
			ft->w = e->w;
			ft->cont = e->cont;
			if (e->w == 2 && c + 1 < cn_back.cols) {
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
	/* And only now the deletes owed by whatever those replaced: the new
	   placement is already on the wire, so there is no instant with
	   nothing in the rectangle. Before the diff is where an *ordinary*
	   delete belongs -- the text underneath has to paint in the same
	   frame -- and it is the wrong place for this one (Gitea #199). */
	cn_imgdlate(&b);
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
