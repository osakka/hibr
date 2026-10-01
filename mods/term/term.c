#define _GNU_SOURCE

#include "tm.h"
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

extern vec tm_list;

const py_api *tm_pty;
const dp_api *tm_dp;

/* Look a terminal up by id, complaining if there is none. */
tm_t *tm_arg(const char *sub, const char *t)
{
	tm_t *m = t ? tm_find(atoi(t)) : 0;

	if (!m)
		lg(HIBR_LERR, "term %s: %s: no such terminal", sub,
		   t ? t : "");
	return m;
}

/* Hand a value back through the result slot, printing it only when nobody
   asked for it. */
void tm_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* Collect whatever the program has written and run it through the parser. */
int tm_pump(tm_t *m, int ms)
{
	str b;
	int r;

	if (!tm_pty || !m->pty)
		return -1;
	s_init(&b);
	r = tm_pty->read(m->pty, ms, &b);
	if (b.n)
		tm_feed(m, b.p, b.n);
	s_free(&b);
	if (r < 0)
		m->done = 1;
	return r;
}

/* Whether a locale, as the C library resolves it, encodes UTF-8 -- asked
   of the library rather than read off the name, since a UTF-8 name that is
   not installed (en_GB.UTF-8 on a machine with only C.utf8) falls back to
   the C locale without a word. */
int tm_isutf8(const char *v)
{
	locale_t l;
	int ok;

	if (!v || !*v)
		return 0;
	l = newlocale(LC_CTYPE_MASK, v, (locale_t)0);
	if (!l)
		return 0;
	ok = !strcasecmp(nl_langinfo_l(CODESET, l), "UTF-8");
	freelocale(l);
	return ok;
}

/* Whether every category of a locale can be loaded -- what a program's
   setlocale(LC_ALL, "") needs of LANG, since one category that fails
   leaves all of them in C. */
int tm_loadable(const char *v)
{
	locale_t l;

	if (!v || !*v)
		return 1;
	l = newlocale(LC_ALL_MASK, v, (locale_t)0);
	if (!l)
		return 0;
	freelocale(l);
	return 1;
}

/* A UTF-8 locale this machine really has, or null for none: C.UTF-8 on
   any recent glibc, UTF-8 on macOS, a language's own as a last resort. */
const char *tm_u8loc(void)
{
	static const char *const c[] = {
		"C.UTF-8", "C.utf8", "UTF-8", "en_US.UTF-8", "en_US.utf8", 0
	};
	int i;

	for (i = 0; c[i]; i++)
		if (tm_isutf8(c[i]))
			return c[i];
	return 0;
}

/* Set a variable for the child, keeping what it was so tm_envback can put
   it back: *keep is 0 when it was unset. */
void tm_envset(const char *nm, const char *v, char **keep)
{
	char *o = getenv(nm);

	*keep = o ? strdup(o) : 0;
	setenv(nm, v, 1);
}

void tm_envback(const char *nm, char *keep)
{
	if (keep)
		setenv(nm, keep, 1);
	else
		unsetenv(nm);
	free(keep);
}

/* Spawn with the environment a program on this terminal should see: the
   terminal type is what this emulator implements, not whatever the desktop
   itself happens to be running in -- a desktop inside tmux or kitty would
   otherwise hand its child a terminfo entry describing a different
   terminal, and the program would use sequences meant for that one. And
   the character set is UTF-8, the only one it decodes: a child left in
   the C locale -- a machine with no other installed, or a session started
   without LANG -- prints '?' for what it thinks cannot be shown (screen,
   ls, every ncurses program) and raw bytes for the rest, which come out as
   U+FFFD. Only LC_CTYPE is given, so the language, the collation and the
   date format stay whatever the user chose; LC_ALL, which would override
   it, is changed only when it is itself set to something else, and LANG
   only when it names a locale this machine does not have -- in which case
   every program was already in C, since one category that fails to load
   fails them all. Everything is put back at once; only the child keeps it. */
int tm_spawn(sh *s, int rows, int cols, char **argv)
{
	const char *all = getenv("LC_ALL"), *ct = getenv("LC_CTYPE");
	const char *eff = all && *all ? all : ct && *ct ? ct : getenv("LANG");
	const char *u8 = 0;
	char *kt, *kc, *kl = 0, *kall = 0, *klang = 0;
	int id, fix = 0, fixall, fixlang;

	if (!tm_isutf8(eff)) {
		u8 = tm_u8loc();
		fix = u8 != 0;
		if (!u8)
			lg(HIBR_LDBG, "term: no UTF-8 locale installed to give");
	}
	fixall = fix && all && *all;
	fixlang = fix && !fixall && !tm_loadable(getenv("LANG"));

	tm_envset("TERM", "xterm-256color", &kt);
	tm_envset("COLORTERM", "truecolor", &kc);
	if (fixall)
		tm_envset("LC_ALL", u8, &kall);
	else if (fix)
		tm_envset("LC_CTYPE", u8, &kl);
	if (fixlang)
		tm_envset("LANG", u8, &klang);
	if (fix)
		lg(HIBR_LDBG, "term: %s is not UTF-8; the program gets %s",
		   eff ? eff : "an unset locale", u8);
	id = tm_pty->spawn(s, rows, cols, argv);
	tm_envback("TERM", kt);
	tm_envback("COLORTERM", kc);
	if (fixall)
		tm_envback("LC_ALL", kall);
	else if (fix)
		tm_envback("LC_CTYPE", kl);
	if (fixlang)
		tm_envback("LANG", klang);
	return id;
}

/* One colour as the cells dump names it: d for the default, p<n> for a
   palette entry, #rrggbb for a true colour. */
void tm_cname(str *o, unsigned v)
{
	static const char hx[] = "0123456789abcdef";
	int sh;

	if (v & DP_RGB) {
		s_ch(o, '#');
		for (sh = 20; sh >= 0; sh -= 4)
			s_ch(o, hx[(v >> sh) & 15]);
	} else if (v & DP_PAL) {
		s_ch(o, 'p');
		s_num(o, (long)(v & 0xFF));
	} else {
		s_ch(o, 'd');
	}
}

/* A row as cells, one line each: column, foreground, background, the
   attributes as letters (b d i u k r s h: bold, dim, italic, underline,
   blink, reverse, strike, hidden, or - for none), then the text. What a
   test compares against a reference terminal's own attribute dump. */
void tm_cells(tm_t *m, int r, str *o)
{
	static const char al[] = "bdiukrs";
	const tm_cell *k;
	int c, i, any;

	for (c = 0; c < m->cols; c++) {
		k = tm_vat(m, r, c);
		if (!k || !k->w)
			continue;
		s_num(o, (long)c);
		s_ch(o, '\t');
		tm_cname(o, k->fg);
		s_ch(o, '\t');
		tm_cname(o, k->bg);
		s_ch(o, '\t');
		any = 0;
		for (i = 0; i < 7; i++)
			if (k->attr & (1u << i)) {
				s_ch(o, al[i]);
				any = 1;
			}
		if (k->attr & TM_HIDE) {
			s_ch(o, 'h');
			any = 1;
		}
		if (!any)
			s_ch(o, '-');
		s_ch(o, '\t');
		tm_cellstr(m, k, o);
		s_ch(o, '\n');
	}
}

/* A colour given as #rrggbb, into 0xrrggbb; 0 if it is not one. */
int tm_hex(const char *p, unsigned *v)
{
	char *e;

	if (!p || *p != '#' || strlen(p) != 7)
		return 0;
	*v = (unsigned)strtoul(p + 1, &e, 16);
	return !*e;
}

/* Run a program on a terminal of its own and keep a picture of its screen. */
/* Decode base64 onto out, skipping anything that is not part of it. */
void tm_unb64(const char *p, str *out)
{
	unsigned acc = 0;
	int bits = 0, v;

	for (; *p && *p != '='; p++) {
		if (*p >= 'A' && *p <= 'Z')
			v = *p - 'A';
		else if (*p >= 'a' && *p <= 'z')
			v = *p - 'a' + 26;
		else if (*p >= '0' && *p <= '9')
			v = *p - '0' + 52;
		else if (*p == '+')
			v = 62;
		else if (*p == '/')
			v = 63;
		else
			continue;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(out, (int)((acc >> bits) & 0xFF));
		}
	}
}

int m_term(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows = 24, cols = 80, lines = 1000, i, r;
	tm_t *m;
	str o;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: term open|new|poll|draw|key|write|feed|size|"
			      "alive|status|fd|title|clip|cursor|row|cells|scroll|mouse|"
			      "focus|colors|screen|select|copy|close ...");
		return 2;
	}
	if (!strcmp(sub, "open")) {
		if (!tm_pty) {
			lg(HIBR_LERR, "term: no pty module");
			return HIBR_FAIL;
		}
		i = 2;
		while (i < ac && av[i][0] == '-' && av[i][1]) {
			if (!strcmp(av[i], "-r") && i + 1 < ac)
				rows = atoi(av[++i]);
			else if (!strcmp(av[i], "-c") && i + 1 < ac)
				cols = atoi(av[++i]);
			else if (!strcmp(av[i], "-s") && i + 1 < ac)
				lines = atoi(av[++i]);
			else if (!strcmp(av[i], "--")) {
				i++;
				break;
			} else {
				lg(HIBR_LERR, "term open: %s: unknown option",
				   av[i]);
				return 2;
			}
			i++;
		}
		if (i >= ac) {
			lg(HIBR_LERR, "usage: term open [-r rows] [-c cols] "
				      "[-s lines] command [args...]");
			return 2;
		}
		if (rows < 1 || cols < 1) {
			lg(HIBR_LERR, "term open: size must be positive");
			return 2;
		}
		m = tm_new(rows, cols);
		m->sbmax = lines < 0 ? 0 : lines;
		m->pty = tm_spawn(s, rows, cols, av + i);
		if (!m->pty) {
			tm_free(m);
			return HIBR_FAIL;
		}
		s_init(&o);
		s_num(&o, (long)m->id);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "new")) {
		for (i = 2; i + 1 < ac; i += 2) {
			if (!strcmp(av[i], "-r"))
				rows = atoi(av[i + 1]);
			else if (!strcmp(av[i], "-c"))
				cols = atoi(av[i + 1]);
		}
		if (rows < 1 || cols < 1) {
			lg(HIBR_LERR, "term new: size must be positive");
			return 2;
		}
		m = tm_new(rows, cols);
		s_init(&o);
		s_num(&o, (long)m->id);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (ac < 3) {
		lg(HIBR_LERR, "term %s: which terminal?", sub);
		return 2;
	}
	m = tm_arg(sub, av[2]);
	if (!m)
		return HIBR_FAIL;

	if (!strcmp(sub, "poll")) {
		r = tm_pump(m, ac > 3 ? atoi(av[3]) : 0);
		return r < 0 ? HIBR_FAIL : HIBR_OK;
	}
	if (!strcmp(sub, "draw")) {
		const char *pane = 0;
		int row, col, h, w, curon, n = ac, i;
		int prow = 0, pcol = 0, ph = 0, pw = 0;

		if (!tm_dp)
			tm_dp = (const dp_api *)hibr_require(s, "display",
						      DP_API_VER);
		if (!tm_dp) {
			lg(HIBR_LERR, "term draw: no display");
			return HIBR_FAIL;
		}
		if (ac < 5) {
			lg(HIBR_LERR,
			   "usage: term draw id row col [h] [w] [curon] [-p pane]");
			return 2;
		}
		/* -p, if given, always trails whichever positionals are
		   present -- found once here, the rest (h/w/curon) stop
		   being read from beyond it. */
		for (i = 5; i < ac; i++) {
			if (!strcmp(av[i], "-p") && i + 1 < ac) {
				pane = av[i + 1];
				n = i;
				break;
			}
		}
		row = atoi(av[3]);
		col = atoi(av[4]);
		h = n > 5 ? atoi(av[5]) : 0;
		w = n > 6 ? atoi(av[6]) : 0;
		curon = n > 7 ? atoi(av[7]) : 0;
		/* row/col become pane-relative once -p names one, the same
		   translate-and-clip img draw -p already does: nothing
		   inside tm_draw itself has to know panes exist at all. */
		if (pane) {
			if (!tm_dp->prect ||
			    !tm_dp->prect(pane, &prow, &pcol, &ph, &pw)) {
				lg(HIBR_LERR, "term draw: no such pane: %s",
				   pane);
				return HIBR_FAIL;
			}
			if (row < 0 || row >= ph || col < 0 || col >= pw)
				return HIBR_OK;
			if (h <= 0 || h > ph - row)
				h = ph - row;
			if (w <= 0 || w > pw - col)
				w = pw - col;
			row += prow;
			col += pcol;
		}
		tm_draw(m, tm_dp, row, col, h, w, curon);
		return HIBR_OK;
	}
	if (!strcmp(sub, "key")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term key id name");
			return 2;
		}
		s_init(&o);
		if (m->bpaste && !strncmp(av[3], "paste ", 6))
			s_cat(&o, "\033[200~");
		r = tm_keybytes(m, av[3], &o);
		if (r && m->bpaste && !strncmp(av[3], "paste ", 6))
			s_cat(&o, "\033[201~");
		if (r && tm_pty) {
			tm_pty->write(m->pty, o.p, o.n);
			m->view = 0;
		}
		s_free(&o);
		return r ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "write")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term write id text...");
			return 2;
		}
		for (i = 3; i < ac; i++)
			tm_pty->write(m->pty, av[i], strlen(av[i]));
		m->view = 0;
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		if (ac >= 5) {
			rows = atoi(av[3]);
			cols = atoi(av[4]);
			if (!tm_size(m, rows, cols))
				return HIBR_FAIL;
			if (tm_pty)
				tm_pty->resize(m->pty, rows, cols);
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->rows);
		s_ch(&o, ' ');
		s_num(&o, (long)m->cols);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "alive")) {
		tm_pump(m, 0);
		if (!tm_pty)
			return HIBR_FAIL;
		return tm_pty->alive(m->pty) ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "status")) {
		s_init(&o);
		s_num(&o, (long)(tm_pty ? tm_pty->status(m->pty) : -1));
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fd")) {
		s_init(&o);
		s_num(&o, (long)(tm_pty ? tm_pty->fd(m->pty) : -1));
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "title")) {
		tm_ret(s, m->title.p ? m->title.p : "");
		return HIBR_OK;
	}
	if (!strcmp(sub, "clip")) {
		if (!m->clip.n)
			return HIBR_FAIL;
		s_init(&o);
		tm_unb64(strchr(m->clip.p, ';') ? strchr(m->clip.p, ';') + 1 : "",
			 &o);
		m->clip.n = 0;
		m->clip.p[0] = 0;
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "cursor")) {
		if (ac > 3) {
			if (!strcmp(av[3], "block"))
				m->cshape = TM_BLOCK;
			else if (!strcmp(av[3], "underline"))
				m->cshape = TM_UNDER;
			else if (!strcmp(av[3], "bar"))
				m->cshape = TM_BAR;
			else {
				lg(HIBR_LERR, "term cursor: %s: block, "
					      "underline or bar", av[3]);
				return 2;
			}
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->cr);
		s_ch(&o, ' ');
		s_num(&o, (long)m->cc);
		s_ch(&o, ' ');
		s_num(&o, (long)m->vis);
		s_ch(&o, ' ');
		s_cat(&o, m->cshape == TM_UNDER ? "underline" :
			  m->cshape == TM_BAR ? "bar" : "block");
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "row")) {
		int c;
		tm_cell *k;
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term row id n");
			return 2;
		}
		r = atoi(av[3]);
		s_init(&o);
		for (c = 0; c < m->cols; c++) {
			k = tm_vat(m, r, c);
			if (k)
				tm_cellstr(m, k, &o);
		}
		while (o.n && o.p[o.n - 1] == ' ')
			o.p[--o.n] = 0;
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "scroll")) {
		if (ac > 3) {
			if (!strcmp(av[3], "top"))
				tm_view(m, m->sbn);
			else if (!strcmp(av[3], "bottom"))
				tm_view(m, -m->view);
			else
				tm_view(m, atoi(av[3]));
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->view);
		s_ch(&o, ' ');
		s_num(&o, (long)m->sbn);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "mouse")) {
		const char *btn = "";
		int at = 4;
		if (ac < 4) {
			tm_ret(s, tm_mname(m));
			return HIBR_OK;
		}
		if (strncmp(av[3], "wheel", 5))
			btn = ac > at ? av[at++] : "";
		if (ac < at + 2) {
			lg(HIBR_LERR, "usage: term mouse id [press|release|"
				      "drag button|wheelup|wheeldown] row col");
			return 2;
		}
		s_init(&o);
		r = tm_mouse(m, av[3], btn, atoi(av[at]), atoi(av[at + 1]), &o);
		if (r && o.n && tm_pty) {
			tm_pty->write(m->pty, o.p, o.n);
			m->view = 0;
		}
		s_free(&o);
		return r ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "select")) {
		if (ac > 3 && !strcmp(av[3], "none")) {
			m->sel = 0;
			return HIBR_OK;
		}
		if (ac < 6 || (strcmp(av[3], "start") && strcmp(av[3], "to"))) {
			lg(HIBR_LERR, "usage: term select id start|to row col, "
				      "or term select id none");
			return 2;
		}
		tm_selset(m, atoi(av[4]), atoi(av[5]), av[3][0] == 's');
		return HIBR_OK;
	}
	if (!strcmp(sub, "copy")) {
		s_init(&o);
		tm_seltext(m, &o);
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return m->sel ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "feed")) {
		FILE *f;
		char *buf;
		size_t got;

		if (ac < 4) {
			lg(HIBR_LERR, "usage: term feed id text... | -f file");
			return 2;
		}
		if (!strcmp(av[3], "-f") && ac > 4) {
			f = fopen(av[4], "rb");
			if (!f) {
				lg(HIBR_LERR, "term feed: %s: cannot read", av[4]);
				return HIBR_FAIL;
			}
			buf = xm(HIBR_IOCH);
			while ((got = fread(buf, 1, HIBR_IOCH, f)) > 0)
				tm_feed(m, buf, got);
			free(buf);
			fclose(f);
			return HIBR_OK;
		}
		for (i = 3; i < ac; i++)
			tm_feed(m, av[i], strlen(av[i]));
		return HIBR_OK;
	}
	if (!strcmp(sub, "cells")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term cells id row");
			return 2;
		}
		s_init(&o);
		tm_cells(m, atoi(av[3]), &o);
		if (o.n && o.p[o.n - 1] == '\n')
			o.p[--o.n] = 0;
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "focus")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term focus id 1|0");
			return 2;
		}
		if (m->focus && tm_pty && m->pty)
			tm_pty->write(m->pty, atoi(av[3]) ? "\033[I" : "\033[O", 3);
		return HIBR_OK;
	}
	if (!strcmp(sub, "colors")) {
		if (ac == 4 && !strcmp(av[3], "off")) {
			m->hasrgb = 0;
			return HIBR_OK;
		}
		if (ac < 5 || !tm_hex(av[3], &m->rgbfg) ||
		    !tm_hex(av[4], &m->rgbbg)) {
			lg(HIBR_LERR, "usage: term colors id #rrggbb #rrggbb | off");
			return 2;
		}
		m->hasrgb = 1;
		return HIBR_OK;
	}
	if (!strcmp(sub, "screen")) {
		tm_ret(s, m->inalt ? "alt" : "main");
		return HIBR_OK;
	}
	if (!strcmp(sub, "close")) {
		if (tm_pty && m->pty)
			tm_pty->drop(m->pty);
		tm_free(m);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "term: %s: unknown subcommand", sub);
	return HIBR_FAIL;
}

/* Find the pty this needs to do anything at all.  The display is not
   required here: a server-side user of the "terminal" interface (hold, for
   its own union-of-viewports emulator) never draws anywhere and must not
   drag console in just by loading term -- so `term draw` finds its own
   display lazily, on first use, instead. */
int tm_ini(sh *s)
{
	tm_pty = (const py_api *)hibr_require(s, "pty", PY_API_VER);
	if (!tm_pty) {
		lg(HIBR_LERR, "term: needs the pty module");
		return HIBR_FAIL;
	}
	return hibr_provide(s, "terminal", TM_API_VER, (void *)tm_apiget());
}

/* Close every terminal and the programs behind them. */
void tm_fini(sh *s)
{
	while (tm_list.n) {
		tm_t *m = (tm_t *)tm_list.p[tm_list.n - 1];
		if (tm_pty && m->pty)
			tm_pty->drop(m->pty);
		tm_free(m);
	}
	v_free(&tm_list);
	hibr_unprovide(s, "terminal");
}

const hibr_bi term_bi[] = {
	{ "term", m_term, "run a program on a terminal and draw its screen" },
	HIBR_BI_END
};

HIBR_MODULE_P("term", "0.24",
	      "a terminal emulator: a program's screen as cells",
	      term_bi, tm_ini, tm_fini, "terminal");
