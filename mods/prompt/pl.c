#include "pr.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* pl -- the powerline look and the glyph sets (Gitea #360).

   PROMPT[look]=powerline draws each segment on a band of its own colour,
   taken from its style unless PROMPT[seg][bg] says otherwise, with its text
   in a colour readable against it and a separator between two bands drawn
   in the left one's colour over the right one's. PROMPT[sep] is arrow,
   round, slant or flat. PROMPT[glyphs] decides the separators and the
   segments' symbols together: nerd, blit, unicode or ascii, or auto, which
   takes HIBR_GLYPHS (the desktop's own setting, for its terminal windows),
   then Blit, then ascii on the Linux console or a locale whose charset is
   not UTF-8, then unicode, which every font draws. Round and slant are
   Nerd glyphs; every other set draws them as the arrow. */

/* While a segment renders inside a band: the band's background as SGR
   parameters, which every style and reset in it carries so nothing in the
   segment drops back to the terminal's own background, and the style its
   own $style stands for. Empty outside powerline. */
str pr_plbg;
const char *pr_plfg;

/* The separators of one set: between two bands, between two bands of one
   colour, the cap at the end, and the cap before the first. */
struct plsep { const char *set, *sep, *same, *end, *start; };

static const struct plsep pl_seps[] = {
	{ "nerd/arrow", "\356\202\260", "\356\202\261", "\356\202\260", "" },
	{ "nerd/round", "\356\202\264", "\356\202\265", "\356\202\264",
	  "\356\202\266" },
	{ "nerd/slant", "\356\202\274", "\356\202\275", "\356\202\274", "" },
	{ "nerd/flat", "", "", "", "" },
	{ "unicode/arrow", "\342\226\266", "\342\224\202", "\342\226\266", "" },
	{ "unicode/flat", "", "", "", "" },
	{ "ascii/arrow", ">", "|", ">", "" },
	{ "ascii/flat", "", "", "", "" },
	{ 0, 0, 0, 0, 0 }
};

/* A set's own symbol for a segment, where the segment's is not set. */
struct plsym { const char *set, *seg, *sym; };

static const struct plsym pl_syms[] = {
	{ "nerd", "git", "\356\202\240 " },
	{ "nerd", "status", "\357\200\215 " },
	{ "ascii", "git", "git:" },
	{ "ascii", "jobs", "jobs:" },
	{ "ascii", "status", "x" },
	{ "ascii", "char", ">" },
	{ 0, 0, 0 }
};

/* Whether a locale variable names a charset that is not UTF-8. */
int pl_notutf8(const char *v)
{
	const char *d;
	size_t n;

	if (!v || !*v || !(d = strchr(v, '.')))
		return 0;
	d++;
	n = strcspn(d, "@");
	if (n == 5 && !strncasecmp(d, "utf-8", 5))
		return 0;
	if (n == 4 && !strncasecmp(d, "utf8", 4))
		return 0;
	return 1;
}

/* The glyph set in force: as configured, or worked out. */
const char *pr_glyphs(pctx *c)
{
	const char *g = pr_top(c, "glyphs"), *v;

	if (g && *g && strcmp(g, "auto"))
		return g;
	v = pr_env(c, "HIBR_GLYPHS");
	if (v && (!strcmp(v, "nerd") || !strcmp(v, "blit") ||
		  !strcmp(v, "unicode") || !strcmp(v, "ascii")))
		return v;
	v = pr_env(c, "BLIT_SESSION_ID");
	if (v && *v)
		return "blit";
	v = pr_env(c, "TERM");
	if (v && !strcmp(v, "linux"))
		return "ascii";
	v = pr_env(c, "LC_ALL");
	if (!v || !*v)
		v = pr_env(c, "LC_CTYPE");
	if (!v || !*v)
		v = pr_env(c, "LANG");
	if (pl_notutf8(v))
		return "ascii";
	return "unicode";
}

/* The set's own symbol for a segment, or 0 to keep the segment's. */
const char *pr_gsym(pctx *c, const char *seg)
{
	const char *g = pr_glyphs(c);
	int i;

	for (i = 0; pl_syms[i].set; i++)
		if (!strcmp(pl_syms[i].set, g) && !strcmp(pl_syms[i].seg, seg))
			return pl_syms[i].sym;
	return 0;
}

/* The separators for a set and a shape. Blit's fonts differ in which
   powerline glyphs they draw, so blit has unicode's; round and slant fall
   back to the arrow wherever Nerd glyphs are not the set. */
const struct plsep *pl_sepfor(const char *g, const char *shape)
{
	str k;
	int i;
	const struct plsep *r = 0;

	if (!strcmp(g, "blit"))
		g = "unicode";
	if (!shape || !*shape)
		shape = "arrow";
	s_init(&k);
	s_cat(&k, g);
	s_ch(&k, '/');
	s_cat(&k, shape);
	for (i = 0; pl_seps[i].set && !r; i++)
		if (!strcmp(pl_seps[i].set, k.p))
			r = &pl_seps[i];
	if (!r) {
		k.n = 0;
		s_cat(&k, strcmp(g, "nerd") && strcmp(g, "ascii") ? "unicode" : g);
		s_cat(&k, "/arrow");
		for (i = 0; pl_seps[i].set && !r; i++)
			if (!strcmp(pl_seps[i].set, k.p))
				r = &pl_seps[i];
	}
	s_free(&k);
	return r ? r : &pl_seps[4];
}

/* Whether a style word is an attribute rather than a colour. */
int pl_attr(const char *w, size_t n)
{
	static const char *a[] = { "bold", "dimmed", "italic", "underline",
				   "blink", "inverted", "strikethrough", "none",
				   0 };
	int i;

	for (i = 0; a[i]; i++)
		if (strlen(a[i]) == n && !strncmp(a[i], w, n))
			return 1;
	return 0;
}

/* A style's own colour, for the band it is drawn on, and its attributes,
   for the text over it. A `bg:` names the band outright. */
void pl_split(const char *style, str *col, str *attrs)
{
	const char *p = style, *q;
	size_t n;
	int bgset = 0;

	while (p && *p) {
		q = p;
		while (*q && *q != ' ')
			q++;
		n = (size_t)(q - p);
		if (n > 3 && !strncmp(p, "bg:", 3)) {
			col->n = 0;
			s_add(col, p + 3, n - 3);
			bgset = 1;
		} else if (n && pl_attr(p, n)) {
			if (strncmp(p, "none", 4)) {
				if (attrs->n)
					s_ch(attrs, ' ');
				s_add(attrs, p, n);
			}
		} else if (n && !bgset && !col->n) {
			if (n > 3 && !strncmp(p, "fg:", 3))
				s_add(col, p + 3, n - 3);
			else
				s_add(col, p, n);
		}
		p = *q ? q + 1 : q;
	}
}

/* Whether text over a colour wants to be dark: by a hex colour's
   brightness, a 256-colour index's, or a name's. */
int pl_light(const char *v)
{
	static const char *lt[] = { "green", "yellow", "cyan", "white", 0 };
	static const int cube[] = { 0, 95, 135, 175, 215, 255 };
	int i, r, g, b;
	long k;

	if (!v || !*v)
		return 0;
	if (*v == '#' && strlen(v) == 7) {
		r = pr_hex(v[1]) * 16 + pr_hex(v[2]);
		g = pr_hex(v[3]) * 16 + pr_hex(v[4]);
		b = pr_hex(v[5]) * 16 + pr_hex(v[6]);
		return r * 299 + g * 587 + b * 114 > 140000;
	}
	if (isdigit((unsigned char)*v)) {
		k = atol(v);
		if (k < 16)
			return k == 2 || k == 3 || k == 6 || k == 7 || k >= 9;
		if (k < 232) {
			k -= 16;
			r = cube[k / 36];
			g = cube[(k / 6) % 6];
			b = cube[k % 6];
			return r * 299 + g * 587 + b * 114 > 140000;
		}
		return 8 + 10 * (k - 232) > 140;
	}
	if (!strncmp(v, "bright-", 7))
		return strcmp(v + 7, "black") != 0;
	for (i = 0; lt[i]; i++)
		if (!strcmp(lt[i], v))
			return 1;
	return 0;
}

/* The SGR for a colour as a foreground or a background, appended. */
void pl_colp(str *o, const char *v, int bg)
{
	if (v && *v)
		pr_col(o, v, strlen(v), bg);
}

/* How many columns of text a rendered segment shows, escapes left out. */
size_t pl_vis(const char *p)
{
	size_t n = 0;

	while (*p) {
		if (*p == 27 && p[1] == '[') {
			p += 2;
			while (*p && !(*p >= '@' && *p <= '~'))
				p++;
			if (*p)
				p++;
			continue;
		}
		if (*p != ' ')
			n++;
		p++;
	}
	return n;
}

/* One rendered segment trimmed of the blanks its format put around it,
   which a band pads for itself. */
char *pl_trim(char *t)
{
	size_t n = strlen(t), i = 0;
	char *r;

	while (n && t[n - 1] == ' ')
		n--;
	while (i < n && t[i] == ' ')
		i++;
	r = pr_span(t + i, t + n);
	free(t);
	return r;
}

/* The state of the run of bands being drawn. */
struct plrun { str *o; const struct plsep *sp; str last; int open; };

/* Close a run of bands with its end cap. */
void pl_close(struct plrun *u)
{
	if (!u->open)
		return;
	s_cat(u->o, "\033[0");
	pl_colp(u->o, u->last.p, 0);
	s_ch(u->o, 'm');
	s_cat(u->o, u->sp->end);
	s_cat(u->o, "\033[0m ");
	u->open = 0;
}

/* Draw one segment as a band, joined to the one before it. */
void pl_band(pctx *c, struct plrun *u, const char *nm)
{
	const seg *g = pr_find(nm);
	const char *st, *bgv;
	str col, attrs, fg;
	char *t;

	if (!g)
		return;
	st = pr_cfg(c, nm, "style");
	if (!st)
		st = g->style ? g->style : "";
	s_init(&col);
	s_init(&attrs);
	pl_split(st, &col, &attrs);
	bgv = pr_cfg(c, nm, "bg");
	if (bgv && *bgv) {
		col.n = 0;
		s_cat(&col, bgv);
	}
	if (!col.n)
		s_cat(&col, "bright-black");
	s_init(&fg);
	s_add(&fg, attrs.p ? attrs.p : "", attrs.n);
	if (fg.n)
		s_ch(&fg, ' ');
	s_cat(&fg, pr_cfg(c, nm, "fg") ? pr_cfg(c, nm, "fg") :
		   pl_light(col.p) ? "black" : "bright-white");
	pr_plbg.n = 0;
	pl_colp(&pr_plbg, col.p, 1);
	pr_plfg = fg.p;
	t = pr_seg(c, nm);
	if (t && !pl_vis(t)) {
		free(t);
		t = 0;
	}
	if (t) {
		t = pl_trim(t);
		if (!u->open) {
			if (*u->sp->start) {
				s_cat(u->o, "\033[0");
				pl_colp(u->o, col.p, 0);
				s_ch(u->o, 'm');
				s_cat(u->o, u->sp->start);
			}
		} else if (!strcmp(u->last.p, col.p)) {
			s_cat(u->o, "\033[0");
			s_add(u->o, pr_plbg.p, pr_plbg.n);
			pl_colp(u->o, pl_light(col.p) ? "black" : "bright-white", 0);
			s_ch(u->o, 'm');
			s_cat(u->o, u->sp->same);
		} else {
			s_cat(u->o, "\033[0");
			pl_colp(u->o, u->last.p, 0);
			s_add(u->o, pr_plbg.p, pr_plbg.n);
			s_ch(u->o, 'm');
			s_cat(u->o, u->sp->sep);
		}
		s_cat(u->o, "\033[0");
		s_add(u->o, pr_plbg.p, pr_plbg.n);
		s_ch(u->o, 'm');
		s_ch(u->o, ' ');
		s_cat(u->o, t);
		s_cat(u->o, "\033[0");
		s_add(u->o, pr_plbg.p, pr_plbg.n);
		s_ch(u->o, 'm');
		s_ch(u->o, ' ');
		u->last.n = 0;
		s_cat(&u->last, col.p);
		u->open = 1;
		free(t);
	}
	pr_plbg.n = 0;
	pr_plfg = 0;
	s_free(&col);
	s_free(&attrs);
	s_free(&fg);
}

/* The powerline look: the top level format read segment by segment, each
   a band, and anything else in it -- text, and the char segment, which is
   where typing starts -- after the run, outside it. */
char *pr_plrender(pctx *c, const char *f)
{
	str o;
	struct plrun u;
	const char *p = f, *b;
	char *nm, *t;

	s_init(&o);
	s_grow(&o, 1);
	o.p[0] = 0;
	memset(&u, 0, sizeof u);
	u.o = &o;
	u.sp = pl_sepfor(pr_glyphs(c), pr_top(c, "sep"));
	s_init(&u.last);
	while (*p) {
		if (*p == '$' && (isalnum((unsigned char)p[1]) || p[1] == '_')) {
			b = ++p;
			while (*p && (isalnum((unsigned char)*p) || *p == '_'))
				p++;
			nm = pr_span(b, p);
			if (!strcmp(nm, "char")) {
				pl_close(&u);
				t = pr_seg(c, nm);
				if (t) {
					s_cat(&o, t);
					free(t);
				}
			} else {
				pl_band(c, &u, nm);
			}
			free(nm);
			continue;
		}
		pl_close(&u);
		if (*p == '\\' && p[1] == 'n') {
			s_ch(&o, '\n');
			p += 2;
			continue;
		}
		s_ch(&o, *p++);
	}
	pl_close(&u);
	s_free(&u.last);
	return o.p;
}
