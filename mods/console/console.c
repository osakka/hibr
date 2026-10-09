#define _GNU_SOURCE

#include "cn.h"
#include "../uni.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern cn_grid cn_back;
extern int cn_crow, cn_ccol, cn_cvis;
extern str cn_pend;
extern int cn_pendo;
int cn_waiting(void);

vec cn_panes;

/* How many `console put`, `fill`, `darken` and `clear` calls have been made at
   absolute coordinates, rather than through a pane. Only ever compared with
   itself, so it is never reset and its own value means nothing. A caller that
   draws over the screen on top of everything else -- a note, a menu, a dialog
   -- cannot be told apart from one that owns its cells by anything the console
   sees, so this is how a script learns that *something* did it: read the count
   either side of the part of a frame where only such things draw, and a frame
   that leaves nothing of its own behind can be told from one that does.
   Cheaper and harder to forget than each of them saying so -- but it counts
   this builtin's own calls, not every write that reaches the grid: a module
   drawing through `dp->put` (img, media, term) moves it not at all, so an
   overlay built out of one of those has to say so for itself. */
static size_t cn_drawn;

/* Find a named pane, or null. */
cn_pane *cn_pfind(const char *nm)
{
	size_t i;
	cn_pane *p;

	for (i = 0; i < cn_panes.n; i++) {
		p = (cn_pane *)cn_panes.p[i];
		if (!strcmp(p->nm, nm))
			return p;
	}
	return 0;
}

/* A named pane's own rectangle, for another module to translate and clip
   its own drawing into -- see dp_api's own comment for why this exists. */
int cn_prect(const char *nm, int *row, int *col, int *h, int *w)
{
	cn_pane *p = cn_pfind(nm);

	if (!p)
		return 0;
	*row = p->row;
	*col = p->col;
	*h = p->h;
	*w = p->w;
	return 1;
}

/* Define or move a named pane. */
cn_pane *cn_pset(const char *nm, int row, int col, int h, int w)
{
	cn_pane *p = cn_pfind(nm);

	if (!p) {
		p = xm(sizeof *p);
		p->nm = xs(nm);
		v_add(&cn_panes, p);
	}
	p->row = row;
	p->col = col;
	p->h = h;
	p->w = w;
	cn_owninval();
	lg(HIBR_LDBG, "pane %s at %d,%d size %dx%d", nm, row, col, h, w);
	return p;
}

/* Forget every pane. */
void cn_pclear(void)
{
	cn_pane *p;

	while (cn_panes.n) {
		p = (cn_pane *)cn_panes.p[--cn_panes.n];
		free(p->nm);
		free(p);
	}
	v_free(&cn_panes);
	cn_owninval();
}

/* Move a pane to the top of the stack, so it is drawn last and hit first. */
void cn_praise(cn_pane *p)
{
	size_t i;

	for (i = 0; i < cn_panes.n; i++)
		if (cn_panes.p[i] == (void *)p)
			break;
	if (i >= cn_panes.n || i == cn_panes.n - 1)
		return;
	memmove(cn_panes.p + i, cn_panes.p + i + 1,
		(cn_panes.n - i - 1) * sizeof *cn_panes.p);
	cn_panes.p[cn_panes.n - 1] = p;
	lg(HIBR_LDBG, "pane %s raised to %d", p->nm, (int)cn_panes.n - 1);
}

/* Move a pane to the bottom of the stack. */
void cn_plower(cn_pane *p)
{
	size_t i;

	for (i = 0; i < cn_panes.n; i++)
		if (cn_panes.p[i] == (void *)p)
			break;
	if (i >= cn_panes.n || i == 0)
		return;
	memmove(cn_panes.p + 1, cn_panes.p, i * sizeof *cn_panes.p);
	cn_panes.p[0] = p;
	lg(HIBR_LDBG, "pane %s lowered", p->nm);
}

/* Forget one pane. */
int cn_pdrop(const char *nm)
{
	size_t i;
	cn_pane *p;

	for (i = 0; i < cn_panes.n; i++) {
		p = (cn_pane *)cn_panes.p[i];
		if (strcmp(p->nm, nm))
			continue;
		memmove(cn_panes.p + i, cn_panes.p + i + 1,
			(cn_panes.n - i - 1) * sizeof *cn_panes.p);
		cn_panes.n--;
		/* Its cells belong to nobody now, so the wallpaper must paint
		   them again: without this a closed window stayed on the
		   screen, since painting behind the panes skipped a rectangle
		   no pane owned any more. */
		cn_owninval();
		lg(HIBR_LDBG, "pane %s dropped", nm);
		free(p->nm);
		free(p);
		return 1;
	}
	return 0;
}

/* The topmost pane covering a screen cell, or null. */
cn_pane *cn_phit(int row, int col)
{
	size_t i = cn_panes.n;
	cn_pane *p;

	while (i--) {
		p = (cn_pane *)cn_panes.p[i];
		if (row >= p->row && row < p->row + p->h &&
		    col >= p->col && col < p->col + p->w)
			return p;
	}
	return 0;
}

int cn_bidi = -1;
int u8rtlq(const char *t, size_t n);

/* The text a script asked for, in display order when it holds right-to-left and bidi is on. */
const char *cn_vis(sh *s, const char *t, str *o)
{
	const uni_api *u;
	const char *v;

	if (!cn_bidi || !t || !u8rtlq(t, strlen(t)))
		return t;
	if (cn_bidi < 0 && (v = hibr_get(s, "HIBR_BIDI")) && !strcmp(v, "off"))
		return t;
	u = hibr_require(s, "uni", UNI_VER);
	if (!u || !u->rtl(t, strlen(t)))
		return t;
	u->vis(t, strlen(t), 2, o);
	return o->p ? o->p : "";
}

/* Write inside a pane, clipped to it, in the pane's own coordinates. */
int cn_pput(sh *s, const cn_pane *p, int row, int col, const char *t)
{
	str clip, vis;
	size_t n, i = 0;
	int w, l, room, adv;
	unsigned cp;

	if (!p || row < 0 || row >= p->h || !t)
		return 0;
	room = p->w - col;
	if (col < 0 || room <= 0)
		return 0;
	s_init(&clip);
	n = strlen(t);
	w = 0;
	while (i < n) {
		l = u8dec(t + i, n - i, &cp);
		if (w + u8w(cp) > room)
			break;
		w += u8w(cp);
		s_add(&clip, t + i, (size_t)l);
		i += (size_t)l;
	}
	s_init(&vis);
	{
		/* Drawing through a pane is the pane's own: it must land even
		   while the wallpaper is painting behind the windows. */
		int was = cn_behind(0);

		adv = cn_put(p->row + row, p->col + col,
			     cn_vis(s, clip.p ? clip.p : "", &vis));
		cn_behind(was);
	}
	s_free(&vis);
	s_free(&clip);
	return adv;
}

/* Read a colour: a name, a palette number, or #rrggbb. */
int cn_colour(const char *t, unsigned *out)
{
	static const char *nm[] = { "black", "red", "green", "yellow", "blue",
				    "magenta", "cyan", "white", 0 };
	int i;
	unsigned v = 0;

	if (!t || !*t || !strcmp(t, "default") || !strcmp(t, "-")) {
		*out = DP_DEFAULT;
		return HIBR_OK;
	}
	for (i = 0; nm[i]; i++)
		if (!strcmp(t, nm[i])) {
			*out = DP_PAL | (unsigned)i;
			return HIBR_OK;
		}
	for (i = 0; nm[i]; i++)
		if (!strncmp(t, "bright", 6) && !strcmp(t + 6, nm[i])) {
			*out = DP_PAL | (unsigned)(i + 8);
			return HIBR_OK;
		}
	if (*t == '#') {
		for (i = 1; t[i]; i++) {
			if (!isxdigit((unsigned char)t[i]))
				return HIBR_FAIL;
			v = v * 16 + (unsigned)(isdigit((unsigned char)t[i])
						? t[i] - '0'
						: (tolower(t[i]) - 'a' + 10));
		}
		if (i != 7)
			return HIBR_FAIL;
		*out = DP_RGB | v;
		return HIBR_OK;
	}
	for (i = 0; t[i]; i++)
		if (!isdigit((unsigned char)t[i]))
			return HIBR_FAIL;
	v = (unsigned)atoi(t);
	if (v > 255)
		return HIBR_FAIL;
	*out = DP_PAL | v;
	return HIBR_OK;
}

/* Read one attribute name. */
unsigned cn_attr(const char *t)
{
	if (!strcmp(t, "bold"))
		return DP_BOLD;
	if (!strcmp(t, "dim"))
		return DP_DIM;
	if (!strcmp(t, "italic"))
		return DP_ITAL;
	if (!strcmp(t, "underline"))
		return DP_UNDER;
	if (!strcmp(t, "blink"))
		return DP_BLINK;
	if (!strcmp(t, "reverse"))
		return DP_REV;
	if (!strcmp(t, "strike"))
		return DP_STRIKE;
	return 0;
}

/* Refuse an operation that needs the screen to be held. */
int cn_need(void)
{
	if (cn_isopen())
		return 1;
	lg(HIBR_LERR, "console: not open");
	return 0;
}

/* Enter or leave full-screen mode, and report what is there. */
int m_console(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows, cols;
	unsigned fg, bg, at;
	str k;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: console open|close|size|clear|pen|put|"
			      "fill|cursor|flush|shot|key|watch|unwatch|pane|hit|mouse");
		return 2;
	}
	if (!strcmp(sub, "open"))
		return cn_open(s);
	if (!strcmp(sub, "close")) {
		cn_close(s);
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		cn_size(&rows, &cols);
		s_init(&k);
		s_num(&k, (long)rows);
		s_ch(&k, ' ');
		s_num(&k, (long)cols);
		hibr_ret(s, k.p);
		if (!s->bind)
			printf("%d %d\n", rows, cols);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "mouse")) {
		static const char *mm[] = { "off", "click", "drag", "motion" };
		const char *m = ac > 2 ? av[2] : "";

		if (!*m) {
			const char *now = mm[cn_mousemode & 3];

			hibr_ret(s, now);
			if (!s->bind)
				printf("%s\n", now);
			return HIBR_OK;
		}
		if (!strcmp(m, "off"))
			cn_mouseon(0);
		else if (!strcmp(m, "click"))
			cn_mouseon(1);
		else if (!strcmp(m, "drag"))
			cn_mouseon(2);
		else if (!strcmp(m, "motion"))
			cn_mouseon(3);
		else {
			lg(HIBR_LERR,
			   "usage: console mouse off|click|drag|motion");
			return 2;
		}
		return HIBR_OK;
	}
	if (!strcmp(sub, "link")) {
		cn_link(ac > 2 && strcmp(av[2], "-") ? av[2] : 0);
		return HIBR_OK;
	}
	if (!strcmp(sub, "bell")) {
		cn_bell();
		return HIBR_OK;
	}
	if (!strcmp(sub, "notify")) {
		if (ac < 3) {
			lg(HIBR_LERR, "usage: console notify text");
			return 2;
		}
		cn_notify(av[2]);
		return HIBR_OK;
	}
	if (!strcmp(sub, "clip")) {
		if (ac < 3) {
			lg(HIBR_LERR, "usage: console clip text");
			return 2;
		}
		cn_clip(av[2]);
		return HIBR_OK;
	}
	if (!strcmp(sub, "signals")) {
		if (ac < 3 || (strcmp(av[2], "on") && strcmp(av[2], "off"))) {
			lg(HIBR_LERR, "usage: console signals on|off");
			return 2;
		}
		cn_signals(!strcmp(av[2], "on"));
		return HIBR_OK;
	}
	if (!strcmp(sub, "resized")) {
		return cn_resized() ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "resizing")) {
		return cn_pending() ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "reassert")) {
		cn_reassert();
		return HIBR_OK;
	}
	if (!strcmp(sub, "clear")) {
		if (!cn_need())
			return HIBR_FAIL;
		cn_clear();
		cn_drawn++;
		return HIBR_OK;
	}
	if (!strcmp(sub, "pen")) {
		int i;
		if (ac < 3) {
			cn_pen(DP_DEFAULT, DP_DEFAULT, 0);
			return HIBR_OK;
		}
		if (cn_colour(av[2], &fg) != HIBR_OK) {
			lg(HIBR_LERR, "console pen: %s: not a colour", av[2]);
			return HIBR_FAIL;
		}
		bg = DP_DEFAULT;
		if (ac > 3 && cn_colour(av[3], &bg) != HIBR_OK) {
			lg(HIBR_LERR, "console pen: %s: not a colour", av[3]);
			return HIBR_FAIL;
		}
		at = 0;
		for (i = 4; i < ac; i++) {
			unsigned a = cn_attr(av[i]);
			if (!a) {
				lg(HIBR_LERR, "console pen: %s: not an attribute",
				   av[i]);
				return HIBR_FAIL;
			}
			at |= a;
		}
		cn_pen(fg, bg, at);
		return HIBR_OK;
	}
	if (!strcmp(sub, "put")) {
		int raw = 0, ob = cn_bidi;

		if (!cn_need())
			return HIBR_FAIL;
		if (ac > 2 && !strcmp(av[2], "-r")) {
			raw = 1;
			av++;
			ac--;
		}
		if (ac > 3 && !strcmp(av[2], "-p")) {
			cn_pane *p = cn_pfind(av[3]);
			if (!p) {
				lg(HIBR_LERR, "console put: %s: no such pane",
				   av[3]);
				return HIBR_FAIL;
			}
			if (ac < 7) {
				lg(HIBR_LERR, "usage: console put -p pane row "
					      "col text");
				return 2;
			}
			if (raw)
				cn_bidi = 0;
			cn_pput(s, p, atoi(av[4]), atoi(av[5]), av[6]);
			cn_bidi = ob;
			return HIBR_OK;
		}
		if (ac < 5) {
			lg(HIBR_LERR, "usage: console put row col text");
			return 2;
		}
		{
			str vis;

			s_init(&vis);
			if (raw)
				cn_bidi = 0;
			cn_put(atoi(av[2]), atoi(av[3]), cn_vis(s, av[4], &vis));
			s_free(&vis);
			cn_drawn++;
		}
		cn_bidi = ob;
		return HIBR_OK;
	}
	/* What this terminal can do with a picture, and the pixel size of a
	   cell while we are asking: "kitty 10 20", "sixel 8 16", or
	   "none 0 0". A script chooses how to draw with this -- the desktop's
	   Images setting does -- rather than each app guessing from $TERM for
	   itself. */
	if (!strcmp(sub, "gfx")) {
		int w = 0, h = 0, k;
		str o;

		k = cn_gfx(s);
		cn_cellpx(&w, &h);
		s_init(&o);
		s_cat(&o, cn_gfxname(k));
		s_ch(&o, ' ');
		s_num(&o, w);
		s_ch(&o, ' ');
		s_num(&o, h);
		hibr_ret(s, o.p ? o.p : "none 0 0");
		if (!s->bind)
			printf("%s\n", o.p ? o.p : "none 0 0");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "bidi")) {
		if (ac > 2 && (!strcmp(av[2], "on") || !strcmp(av[2], "off"))) {
			cn_bidi = !strcmp(av[2], "on");
			return HIBR_OK;
		}
		if (ac > 2) {
			lg(HIBR_LERR, "usage: console bidi [on|off]");
			return 2;
		}
		if (cn_bidi < 0) {
			const char *v = hibr_get(s, "HIBR_BIDI");

			return v && !strcmp(v, "off") ? HIBR_FAIL : HIBR_OK;
		}
		return cn_bidi ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "fill")) {
		if (!cn_need())
			return HIBR_FAIL;
		if (ac < 6) {
			lg(HIBR_LERR, "usage: console fill row col h w [char]");
			return 2;
		}
		cn_fill(atoi(av[2]), atoi(av[3]), atoi(av[4]), atoi(av[5]),
			ac > 6 ? av[6] : " ");
		cn_drawn++;
		return HIBR_OK;
	}
	if (!strcmp(sub, "darken")) {
		int once = 0;

		if (!cn_need())
			return HIBR_FAIL;
		/* -s: a shadow. Idempotent -- a cell already darkened by one
		   is left alone -- so it can be cast on every frame without
		   coming out darker each time, and nothing has to decide
		   which frame may cast it. */
		if (ac > 2 && !strcmp(av[2], "-s")) {
			once = 1;
			av++;
			ac--;
		}
		if (ac < 6) {
			lg(HIBR_LERR, "usage: console darken [-s] row col h w"
				      " [pct]");
			return 2;
		}
		cn_darken1(atoi(av[2]), atoi(av[3]), atoi(av[4]), atoi(av[5]),
			   ac > 6 ? atoi(av[6]) : 55, once);
		cn_drawn++;
		return HIBR_OK;
	}
	if (!strcmp(sub, "imgdetail")) {
		const char *m = ac > 2 ? av[2] : "";

		if (!*m) {
			const char *now = cn_imghalf ? "half" : "full";

			hibr_ret(s, now);
			if (!s->bind)
				printf("%s\n", now);
			return HIBR_OK;
		}
		if (!strcmp(m, "half"))
			cn_imghalf = 1;
		else if (!strcmp(m, "full"))
			cn_imghalf = 0;
		else {
			lg(HIBR_LERR, "usage: console imgdetail full|half");
			return 2;
		}
		return HIBR_OK;
	}
	if (!strcmp(sub, "behind")) {
		/* While this is on, a write at absolute coordinates skips any
		   cell a pane covers -- so the wallpaper paints around the
		   windows instead of over them, and that goes for the picture
		   too, since the img module draws through the same put. A
		   menu dropping over a window, or anything drawn through a
		   pane, is unaffected. */
		if (!cn_need())
			return HIBR_FAIL;
		cn_behind(ac > 2 && (!strcmp(av[2], "on") ||
				     !strcmp(av[2], "1")));
		return HIBR_OK;
	}
	if (!strcmp(sub, "waiting"))
		return cn_waiting() ? HIBR_OK : HIBR_FAIL;
	/* See cn_drawn: writes made at absolute coordinates, not through a
	   pane. The desktop reads it either side of the overlays it draws over
	   everything else, so the frame after one has drawn is a full one --
	   nothing else would repaint what a note or a menu covered. */
	if (!strcmp(sub, "drawn")) {
		s_init(&k);
		s_num(&k, (long)cn_drawn);
		hibr_ret(s, k.p);
		if (!s->bind)
			printf("%s\n", k.p);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "consumed")) {
		s_init(&k);
		s_num(&k, (long)cn_eaten);
		hibr_ret(s, k.p);
		if (!s->bind)
			printf("%s\n", k.p);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "shade")) {
		unsigned v;
		int pct = ac > 3 ? atoi(av[3]) : 55, i;
		if (ac < 3) {
			lg(HIBR_LERR, "usage: console shade colour [pct]");
			return 2;
		}
		if (cn_colour(av[2], &v) != HIBR_OK || !(v & DP_RGB)) {
			lg(HIBR_LERR, "console shade: %s: not a #rrggbb colour",
			   av[2]);
			return HIBR_FAIL;
		}
		if (pct < 0)
			pct = 0;
		if (pct > 100)
			pct = 100;
		v = cn_dim1(v, pct, 0);
		s_init(&k);
		s_ch(&k, '#');
		for (i = 20; i >= 0; i -= 4)
			s_ch(&k, "0123456789abcdef"[(v >> i) & 0xF]);
		hibr_ret(s, k.p);
		if (!s->bind)
			printf("%s\n", k.p);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "shot")) {
		const char *fm = "ansi";
		int i = 2, q[4] = { 0, 0, 0, 0 }, k = 0;

		if (i + 1 < ac && !strcmp(av[i], "-f")) {
			fm = av[i + 1];
			i += 2;
		}
		if (i >= ac) {
			lg(HIBR_LERR, "usage: console shot [-f ansi|html|text] file "
				      "[row col h w]");
			return 2;
		}
		for (k = 0; k < 4 && i + 1 + k < ac; k++)
			q[k] = atoi(av[i + 1 + k]);
		return cn_shot(av[i], fm, q[0], q[1], q[2], q[3]);
	}
	if (!strcmp(sub, "darkdefault")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: console darkdefault fg bg");
			return 2;
		}
		if (cn_colour(av[2], &fg) != HIBR_OK) {
			lg(HIBR_LERR, "console darkdefault: %s: not a colour",
			   av[2]);
			return HIBR_FAIL;
		}
		if (cn_colour(av[3], &bg) != HIBR_OK) {
			lg(HIBR_LERR, "console darkdefault: %s: not a colour",
			   av[3]);
			return HIBR_FAIL;
		}
		cn_setdim(fg, bg);
		return HIBR_OK;
	}
	if (!strcmp(sub, "cursor")) {
		if (ac > 2 && !strcmp(av[2], "off")) {
			cn_cursor(cn_crow, cn_ccol, 0);
			return HIBR_OK;
		}
		if (ac < 4) {
			lg(HIBR_LERR, "usage: console cursor row col | off");
			return 2;
		}
		cn_cursor(atoi(av[2]), atoi(av[3]), 1);
		return HIBR_OK;
	}
	if (!strcmp(sub, "flush")) {
		if (!cn_need())
			return HIBR_FAIL;
		s_init(&k);
		s_num(&k, cn_flush());
		hibr_ret(s, k.p);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "key")) {
		int r, q = 0;
		if (!cn_need())
			return HIBR_FAIL;
		/* -q: a quiet wait, which ends on input, a resize or the time
		   running out, and not on a watched descriptor having
		   something to say -- for a caller that is deliberately not
		   reading it yet. */
		if (ac > 2 && !strcmp(av[2], "-q")) {
			q = 1;
			av++;
			ac--;
		}
		s_init(&k);
		cn_quiet = q;
		r = cn_key(ac > 2 ? atoi(av[2]) : -1, &k);
		cn_quiet = 0;
		if (r == 1) {
			hibr_ret(s, k.p);
			if (!s->bind)
				printf("%s\n", k.p);
		}
		s_free(&k);
		return r == 1 ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "watch") || !strcmp(sub, "unwatch")) {
		if (ac < 3) {
			lg(HIBR_LERR, "usage: console %s fd", sub);
			return 2;
		}
		if (sub[0] == 'w')
			cn_watchadd(atoi(av[2]));
		else
			cn_watchdel(atoi(av[2]));
		return HIBR_OK;
	}
	if (!strcmp(sub, "pane")) {
		const char *verb = ac > 2 ? av[2] : "";
		cn_pane *p;
		size_t i;

		if (ac == 3 && !strcmp(verb, "clear")) {
			cn_pclear();
			return HIBR_OK;
		}
		if (ac == 3 && !strcmp(verb, "list")) {
			s_init(&k);
			for (i = 0; i < cn_panes.n; i++) {
				if (i)
					s_ch(&k, ' ');
				s_cat(&k, ((cn_pane *)cn_panes.p[i])->nm);
			}
			hibr_ret(s, k.p ? k.p : "");
			if (!s->bind && k.p)
				printf("%s\n", k.p);
			s_free(&k);
			return HIBR_OK;
		}
		if (ac == 4 && (!strcmp(verb, "raise") ||
				!strcmp(verb, "lower") ||
				!strcmp(verb, "drop"))) {
			if (!strcmp(verb, "drop"))
				return cn_pdrop(av[3]) ? HIBR_OK : HIBR_FAIL;
			p = cn_pfind(av[3]);
			if (!p) {
				lg(HIBR_LERR, "console pane %s: %s: no such "
					      "pane", verb, av[3]);
				return HIBR_FAIL;
			}
			if (!strcmp(verb, "raise"))
				cn_praise(p);
			else
				cn_plower(p);
			return HIBR_OK;
		}
		if (ac == 3) {
			p = cn_pfind(verb);
			if (!p)
				return HIBR_FAIL;
			s_init(&k);
			s_num(&k, (long)p->row);
			s_ch(&k, ' ');
			s_num(&k, (long)p->col);
			s_ch(&k, ' ');
			s_num(&k, (long)p->h);
			s_ch(&k, ' ');
			s_num(&k, (long)p->w);
			hibr_ret(s, k.p);
			if (!s->bind)
				printf("%s\n", k.p);
			s_free(&k);
			return HIBR_OK;
		}
		if (ac < 7) {
			lg(HIBR_LERR, "usage: console pane name row col h w, "
				      "or pane raise|lower|drop name, "
				      "or pane list|clear");
			return 2;
		}
		cn_pset(av[2], atoi(av[3]), atoi(av[4]), atoi(av[5]),
			atoi(av[6]));
		return HIBR_OK;
	}
	if (!strcmp(sub, "hit")) {
		cn_pane *p;

		if (ac < 4) {
			lg(HIBR_LERR, "usage: console hit row col");
			return 2;
		}
		p = cn_phit(atoi(av[2]), atoi(av[3]));
		if (!p) {
			hibr_ret(s, "");
			return HIBR_FAIL;
		}
		hibr_ret(s, p->nm);
		if (!s->bind)
			printf("%s\n", p->nm);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "console: %s: unknown subcommand", sub);
	return HIBR_FAIL;
}

static const dp_api console_api = {
	cn_open, cn_close, cn_isopen, cn_size, cn_resized, cn_pen,
	cn_clear, cn_put, cn_fill, cn_cursor, cn_flush, cn_key,
	cn_colour, cn_attr, cn_mouseon, cn_prect, cn_link, cn_image, cn_cellpx,
	cn_imgkeep
};

/* Offer the drawing table to whatever else wants to draw. */
int cn_ini(sh *s)
{
	return hibr_provide(s, "display", DP_API_VER, (void *)&console_api);
}

/* Put the terminal back and release everything the screen held. */
void cn_fini(sh *s)
{
	cn_close(s);
	cn_pclear();
	cn_gfree(&cn_back);
	{
		extern cn_grid cn_front;
		cn_gfree(&cn_front);
	}
	s_free(&cn_pend);
	cn_pendo = 0;
	hibr_unprovide(s, "display");
}

const hibr_bi console_bi[] = {
	{ "console", m_console, "draw text cells on the whole terminal" },
	HIBR_BI_END
};

HIBR_MODULE_P("console", "1.0",
	      "a text display: cells, panes and decoded keys", console_bi,
	      cn_ini, cn_fini, "display");
