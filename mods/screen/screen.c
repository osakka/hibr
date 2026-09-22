#define _GNU_SOURCE

#include "scr.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern scr_grid scr_back;
extern int scr_crow, scr_ccol, scr_cvis;
extern str scr_pend;
extern int scr_pendo;

vec scr_panes;

/* Find a named pane, or null. */
scr_pane *scr_pfind(const char *nm)
{
	size_t i;
	scr_pane *p;

	for (i = 0; i < scr_panes.n; i++) {
		p = (scr_pane *)scr_panes.p[i];
		if (!strcmp(p->nm, nm))
			return p;
	}
	return 0;
}

/* Define or move a named pane. */
scr_pane *scr_pset(const char *nm, int row, int col, int h, int w)
{
	scr_pane *p = scr_pfind(nm);

	if (!p) {
		p = xm(sizeof *p);
		p->nm = xs(nm);
		v_add(&scr_panes, p);
	}
	p->row = row;
	p->col = col;
	p->h = h;
	p->w = w;
	lg(HIBR_LDBG, "pane %s at %d,%d size %dx%d", nm, row, col, h, w);
	return p;
}

/* Forget every pane. */
void scr_pclear(void)
{
	scr_pane *p;

	while (scr_panes.n) {
		p = (scr_pane *)scr_panes.p[--scr_panes.n];
		free(p->nm);
		free(p);
	}
	v_free(&scr_panes);
}

/* Write inside a pane, clipped to it, in the pane's own coordinates. */
int scr_pput(const scr_pane *p, int row, int col, const char *t)
{
	str clip;
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
	adv = scr_put(p->row + row, p->col + col, clip.p ? clip.p : "");
	s_free(&clip);
	return adv;
}

/* Read a colour: a name, a palette number, or #rrggbb. */
int scr_colour(const char *t, unsigned *out)
{
	static const char *nm[] = { "black", "red", "green", "yellow", "blue",
				    "magenta", "cyan", "white", 0 };
	int i;
	unsigned v = 0;

	if (!t || !*t || !strcmp(t, "default") || !strcmp(t, "-")) {
		*out = SCR_DEFAULT;
		return HIBR_OK;
	}
	for (i = 0; nm[i]; i++)
		if (!strcmp(t, nm[i])) {
			*out = SCR_PAL | (unsigned)i;
			return HIBR_OK;
		}
	for (i = 0; nm[i]; i++)
		if (!strncmp(t, "bright", 6) && !strcmp(t + 6, nm[i])) {
			*out = SCR_PAL | (unsigned)(i + 8);
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
		*out = SCR_RGB | v;
		return HIBR_OK;
	}
	for (i = 0; t[i]; i++)
		if (!isdigit((unsigned char)t[i]))
			return HIBR_FAIL;
	v = (unsigned)atoi(t);
	if (v > 255)
		return HIBR_FAIL;
	*out = SCR_PAL | v;
	return HIBR_OK;
}

/* Read one attribute name. */
unsigned scr_attr(const char *t)
{
	if (!strcmp(t, "bold"))
		return SCR_BOLD;
	if (!strcmp(t, "dim"))
		return SCR_DIM;
	if (!strcmp(t, "italic"))
		return SCR_ITAL;
	if (!strcmp(t, "underline"))
		return SCR_UNDER;
	if (!strcmp(t, "blink"))
		return SCR_BLINK;
	if (!strcmp(t, "reverse"))
		return SCR_REV;
	if (!strcmp(t, "strike"))
		return SCR_STRIKE;
	return 0;
}

/* Refuse an operation that needs the screen to be held. */
int scr_need(void)
{
	if (scr_isopen())
		return 1;
	lg(HIBR_LERR, "screen: not open");
	return 0;
}

/* Enter or leave full-screen mode, and report what is there. */
int m_screen(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows, cols;
	unsigned fg, bg, at;
	str k;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: screen open|close|size|clear|pen|put|"
			      "fill|cursor|flush|key|pane");
		return 2;
	}
	if (!strcmp(sub, "open"))
		return scr_open(s);
	if (!strcmp(sub, "close")) {
		scr_close(s);
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		scr_size(&rows, &cols);
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
	if (!strcmp(sub, "resized")) {
		return scr_resized() ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "clear")) {
		if (!scr_need())
			return HIBR_FAIL;
		scr_clear();
		return HIBR_OK;
	}
	if (!strcmp(sub, "pen")) {
		int i;
		if (ac < 3) {
			scr_pen(SCR_DEFAULT, SCR_DEFAULT, 0);
			return HIBR_OK;
		}
		if (scr_colour(av[2], &fg) != HIBR_OK) {
			lg(HIBR_LERR, "screen pen: %s: not a colour", av[2]);
			return HIBR_FAIL;
		}
		bg = SCR_DEFAULT;
		if (ac > 3 && scr_colour(av[3], &bg) != HIBR_OK) {
			lg(HIBR_LERR, "screen pen: %s: not a colour", av[3]);
			return HIBR_FAIL;
		}
		at = 0;
		for (i = 4; i < ac; i++) {
			unsigned a = scr_attr(av[i]);
			if (!a) {
				lg(HIBR_LERR, "screen pen: %s: not an attribute",
				   av[i]);
				return HIBR_FAIL;
			}
			at |= a;
		}
		scr_pen(fg, bg, at);
		return HIBR_OK;
	}
	if (!strcmp(sub, "put")) {
		if (!scr_need())
			return HIBR_FAIL;
		if (ac > 3 && !strcmp(av[2], "-p")) {
			scr_pane *p = scr_pfind(av[3]);
			if (!p) {
				lg(HIBR_LERR, "screen put: %s: no such pane",
				   av[3]);
				return HIBR_FAIL;
			}
			if (ac < 7) {
				lg(HIBR_LERR, "usage: screen put -p pane row "
					      "col text");
				return 2;
			}
			scr_pput(p, atoi(av[4]), atoi(av[5]), av[6]);
			return HIBR_OK;
		}
		if (ac < 5) {
			lg(HIBR_LERR, "usage: screen put row col text");
			return 2;
		}
		scr_put(atoi(av[2]), atoi(av[3]), av[4]);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fill")) {
		if (!scr_need())
			return HIBR_FAIL;
		if (ac < 6) {
			lg(HIBR_LERR, "usage: screen fill row col h w [char]");
			return 2;
		}
		scr_fill(atoi(av[2]), atoi(av[3]), atoi(av[4]), atoi(av[5]),
			ac > 6 ? av[6] : " ");
		return HIBR_OK;
	}
	if (!strcmp(sub, "cursor")) {
		if (ac > 2 && !strcmp(av[2], "off")) {
			scr_cursor(scr_crow, scr_ccol, 0);
			return HIBR_OK;
		}
		if (ac < 4) {
			lg(HIBR_LERR, "usage: screen cursor row col | off");
			return 2;
		}
		scr_cursor(atoi(av[2]), atoi(av[3]), 1);
		return HIBR_OK;
	}
	if (!strcmp(sub, "flush")) {
		if (!scr_need())
			return HIBR_FAIL;
		s_init(&k);
		s_num(&k, scr_flush());
		hibr_ret(s, k.p);
		s_free(&k);
		return HIBR_OK;
	}
	if (!strcmp(sub, "key")) {
		int r;
		if (!scr_need())
			return HIBR_FAIL;
		s_init(&k);
		r = scr_key(ac > 2 ? atoi(av[2]) : -1, &k);
		if (r == 1) {
			hibr_ret(s, k.p);
			if (!s->bind)
				printf("%s\n", k.p);
		}
		s_free(&k);
		return r == 1 ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "pane")) {
		if (ac == 3 && !strcmp(av[2], "clear")) {
			scr_pclear();
			return HIBR_OK;
		}
		if (ac < 7) {
			lg(HIBR_LERR, "usage: screen pane name row col h w");
			return 2;
		}
		scr_pset(av[2], atoi(av[3]), atoi(av[4]), atoi(av[5]),
			atoi(av[6]));
		return HIBR_OK;
	}
	lg(HIBR_LERR, "screen: %s: unknown subcommand", sub);
	return HIBR_FAIL;
}

static const scr_api screen_api = {
	scr_open, scr_close, scr_isopen, scr_size, scr_resized, scr_pen,
	scr_clear, scr_put, scr_fill, scr_cursor, scr_flush, scr_key,
	scr_colour, scr_attr
};

/* Offer the drawing table to whatever else wants to draw. */
int scr_ini(sh *s)
{
	return hibr_provide(s, "screen", SCR_API_VER, (void *)&screen_api);
}

/* Put the terminal back and release everything the screen held. */
void scr_fini(sh *s)
{
	scr_close(s);
	scr_pclear();
	scr_gfree(&scr_back);
	{
		extern scr_grid scr_front;
		scr_gfree(&scr_front);
	}
	s_free(&scr_pend);
	scr_pendo = 0;
	hibr_unprovide(s, "screen");
}

const hibr_bi screen_bi[] = {
	{ "screen", m_screen, "draw on the whole terminal" },
	HIBR_BI_END
};

HIBR_MODULE("screen", "0.21", "full-screen drawing and key decoding",
	    screen_bi, scr_ini, scr_fini);
