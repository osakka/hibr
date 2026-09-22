#define _GNU_SOURCE

#include "vi.h"
#include "../display.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const dp_api *dp;
static vi_ed ed;

/* The column a position sits at on screen, tabs and wide glyphs counted. */
int vi_col(vi_buf *b, size_t pos)
{
	size_t ln = vi_lineof(b, pos), i = vi_lstart(b, ln);
	int col = 0, c;
	str t;

	s_init(&t);
	while (i < pos) {
		c = vi_at(b, i);
		if (c == '\t') {
			col += 8 - (col % 8);
			i++;
			continue;
		}
		t.n = 0;
		vi_get(b, i, 4, &t);
		if (t.n) {
			unsigned cp;
			int l = u8dec(t.p, t.n, &cp);
			col += u8w(cp) ? u8w(cp) : 0;
			i += (size_t)l;
			continue;
		}
		i++;
		col++;
	}
	s_free(&t);
	return col;
}

/* The position on a line nearest to a wanted column. */
size_t vi_atcol(vi_buf *b, size_t ln, int want)
{
	size_t i = vi_lstart(b, ln), e = vi_lend(b, ln);
	int col = 0;

	while (i < e && col < want) {
		if (vi_at(b, i) == '\t')
			col += 8 - (col % 8);
		else
			col++;
		i = vi_next(b, i);
	}
	return i;
}

/* Keep the cursor inside the text, and off a line's newline in normal mode. */
void vi_clamp(vi_ed *e)
{
	size_t ln, s, t;

	if (e->cur > vi_len(&e->b))
		e->cur = vi_len(&e->b);
	if (e->mode == VI_INSERT)
		return;
	ln = vi_lineof(&e->b, e->cur);
	s = vi_lstart(&e->b, ln);
	t = vi_lend(&e->b, ln);
	if (e->cur > t)
		e->cur = t > s ? vi_prev(&e->b, t) : s;
}

/* True for the characters vi calls a word. */
int vi_isw(int c)
{
	return c == '_' || isalnum(c) || c >= 128;
}

/* Move forward a word. */
size_t vi_wnext(vi_buf *b, size_t p)
{
	size_t len = vi_len(b);
	int c = vi_at(b, p);

	if (c >= 0 && vi_isw(c))
		while (p < len && vi_isw(vi_at(b, p)))
			p = vi_next(b, p);
	else if (c >= 0 && !isspace(c))
		while (p < len && !isspace(vi_at(b, p)) && !vi_isw(vi_at(b, p)))
			p = vi_next(b, p);
	while (p < len && isspace(vi_at(b, p)) && vi_at(b, p) != '\n')
		p = vi_next(b, p);
	if (p < len && vi_at(b, p) == '\n')
		p = vi_next(b, p);
	return p;
}

/* Move back a word. */
size_t vi_wprev(vi_buf *b, size_t p)
{
	if (!p)
		return 0;
	p = vi_prev(b, p);
	while (p && isspace(vi_at(b, p)))
		p = vi_prev(b, p);
	if (vi_isw(vi_at(b, p)))
		while (p && vi_isw(vi_at(b, vi_prev(b, p))))
			p = vi_prev(b, p);
	else
		while (p && !isspace(vi_at(b, vi_prev(b, p))) &&
		       !vi_isw(vi_at(b, vi_prev(b, p))))
			p = vi_prev(b, p);
	return p;
}

/* The end of the current or next word. */
size_t vi_wend(vi_buf *b, size_t p)
{
	size_t len = vi_len(b);

	if (p < len)
		p = vi_next(b, p);
	while (p < len && isspace(vi_at(b, p)))
		p = vi_next(b, p);
	while (p < len && vi_isw(vi_at(b, vi_next(b, p) < len ? p : p)) &&
	       vi_next(b, p) < len && vi_isw(vi_at(b, vi_next(b, p))))
		p = vi_next(b, p);
	return p;
}

/* Remember text for p and P. */
void vi_setyank(vi_ed *e, size_t pos, size_t n, int line)
{
	e->yank.n = 0;
	if (e->yank.p)
		e->yank.p[0] = 0;
	vi_get(&e->b, pos, n, &e->yank);
	e->yline = line;
}

/* Say something on the status line until the next key. */
void vi_say(vi_ed *e, const char *t)
{
	e->msg.n = 0;
	if (e->msg.p)
		e->msg.p[0] = 0;
	s_cat(&e->msg, t);
}

/* Keep the cursor on screen. */
void vi_scroll(vi_ed *e, int vh)
{
	size_t ln = vi_lineof(&e->b, e->cur);

	if (ln < e->top)
		e->top = ln;
	if (vh > 0 && ln >= e->top + (size_t)vh)
		e->top = ln - (size_t)vh + 1;
}

/* Draw one line of the buffer into a row. */
void vi_drawline(vi_ed *e, size_t ln, int row, int cols, size_t vs, size_t ve)
{
	size_t i = vi_lstart(&e->b, ln), end = vi_lend(&e->b, ln);
	int col = 0;
	str t;
	unsigned fg, bg;

	s_init(&t);
	while (i < end && col < cols) {
		int c = vi_at(&e->b, i), sel, hit = 0;
		size_t nx = vi_next(&e->b, i);
		sel = (e->mode == VI_VISUAL || e->mode == VI_VLINE) &&
		      i >= vs && i < ve;
		if (e->find.n) {
			str w;
			s_init(&w);
			vi_get(&e->b, i, e->find.n, &w);
			if (w.n == e->find.n && !memcmp(w.p, e->find.p, w.n))
				hit = 1;
			s_free(&w);
		}
		fg = sel ? DP_PAL | 16u : (hit ? DP_PAL | 16u : DP_DEFAULT);
		bg = sel ? DP_PAL | 252u : (hit ? DP_PAL | 227u : DP_DEFAULT);
		dp->pen(fg, bg, 0);
		if (c == '\t') {
			int k = 8 - (col % 8);
			while (k-- && col < cols)
				col += dp->put(row, col, " ");
			i++;
			continue;
		}
		t.n = 0;
		vi_get(&e->b, i, nx - i, &t);
		if (t.p) {
			t.p[t.n] = 0;
			if ((unsigned char)t.p[0] < 32) {
				char ctl[3];
				ctl[0] = '^';
				ctl[1] = (char)(t.p[0] + 64);
				ctl[2] = 0;
				dp->pen(DP_PAL | 244u, bg, 0);
				col += dp->put(row, col, ctl);
			} else {
				col += dp->put(row, col, t.p);
			}
		}
		i = nx;
	}
	s_free(&t);
	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
}

/* Redraw everything. */
void vi_render(vi_ed *e, int rows, int cols)
{
	int vh = rows - 1, r;
	size_t nl = vi_nlines(&e->b), ln, vs = 0, ve = 0;
	str st;

	if (e->mode == VI_VISUAL || e->mode == VI_VLINE) {
		vs = e->vstart < e->cur ? e->vstart : e->cur;
		ve = e->vstart < e->cur ? e->cur : e->vstart;
		ve = vi_next(&e->b, ve);
		if (e->mode == VI_VLINE) {
			vs = vi_lstart(&e->b, vi_lineof(&e->b, vs));
			ve = vi_lend(&e->b, vi_lineof(&e->b, ve ? ve - 1 : 0));
		}
	}
	dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
	dp->clear();
	for (r = 0; r < vh; r++) {
		ln = e->top + (size_t)r;
		if (ln < nl) {
			vi_drawline(e, ln, r, cols, vs, ve);
		} else {
			dp->pen(DP_PAL | 240u, DP_DEFAULT, 0);
			dp->put(r, 0, "~");
			dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
		}
	}
	s_init(&st);
	if (e->mode == VI_EX) {
		s_ch(&st, ':');
		s_cat(&st, e->cmd.p ? e->cmd.p : "");
		dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
		dp->fill(rows - 1, 0, 1, cols, " ");
		dp->put(rows - 1, 0, st.p);
		dp->cursor(rows - 1, 1 + (int)e->cmd.n, 1);
	} else {
		{
			const char *nm = e->path ? e->path : "[no name]";
			const char *sl = strrchr(nm, '/');
			s_ch(&st, ' ');
			s_cat(&st, sl && sl[1] ? sl + 1 : nm);
		}
		if (e->b.mod)
			s_cat(&st, " [+]");
		s_cat(&st, "  ");
		s_num(&st, (long)(vi_lineof(&e->b, e->cur) + 1));
		s_ch(&st, ',');
		s_num(&st, (long)vi_col(&e->b, e->cur) + 1);
		s_cat(&st, "  ");
		s_num(&st, (long)nl);
		s_cat(&st, " lines");
		if (e->mode == VI_INSERT)
			s_cat(&st, "  -- INSERT --");
		if (e->mode == VI_VISUAL)
			s_cat(&st, "  -- VISUAL --");
		if (e->mode == VI_VLINE)
			s_cat(&st, "  -- VISUAL LINE --");
		if (e->find.n) {
			s_cat(&st, "  /");
			s_cat(&st, e->find.p);
		}
		if (e->pend.n) {
			s_cat(&st, "  ");
			s_cat(&st, e->pend.p);
		}
		if (e->msg.n) {
			s_cat(&st, "  ");
			s_cat(&st, e->msg.p);
		}
		dp->pen(DP_PAL | 16u, DP_PAL | 110u, 0);
		dp->fill(rows - 1, 0, 1, cols, " ");
		dp->put(rows - 1, 0, st.p);
		dp->pen(DP_DEFAULT, DP_DEFAULT, 0);
		{
			size_t cl = vi_lineof(&e->b, e->cur);
			dp->cursor((int)(cl - e->top), vi_col(&e->b, e->cur), 1);
		}
	}
	s_free(&st);
	dp->flush();
}

/* Find the next line that contains the search text. */
void vi_search(vi_ed *e, int back)
{
	size_t nl = vi_nlines(&e->b), ln = vi_lineof(&e->b, e->cur), i, k;
	str t;

	if (!e->find.n)
		return;
	s_init(&t);
	for (k = 1; k <= nl; k++) {
		i = back ? (ln + nl - k) % nl : (ln + k) % nl;
		t.n = 0;
		vi_get(&e->b, vi_lstart(&e->b, i),
		       vi_lend(&e->b, i) - vi_lstart(&e->b, i), &t);
		if (t.p) {
			t.p[t.n] = 0;
			if (strstr(t.p, e->find.p)) {
				e->cur = vi_lstart(&e->b, i) +
					 (size_t)(strstr(t.p, e->find.p) - t.p);
				s_free(&t);
				return;
			}
		}
	}
	s_free(&t);
	vi_say(e, "not found");
}

/* Apply an operator to a range. */
void vi_operate(vi_ed *e, char op, size_t a, size_t b, int line)
{
	if (b < a) {
		size_t t = a;
		a = b;
		b = t;
	}
	if (line) {
		a = vi_lstart(&e->b, vi_lineof(&e->b, a));
		b = vi_lend(&e->b, vi_lineof(&e->b, b));
		if (b < vi_len(&e->b))
			b++;
	}
	vi_ugroup(e);
	vi_setyank(e, a, b - a, line);
	if (op == 'y') {
		e->cur = a;
		return;
	}
	vi_edel(e, a, b - a);
	e->cur = a;
	if (op == 'c') {
		e->mode = VI_INSERT;
		if (line) {
			vi_eins(e, a, "\n", 1);
			e->cur = a;
		}
	}
	vi_clamp(e);
}

/* Run an ex command. */
void vi_ex(vi_ed *e, const char *c)
{
	int force = 0;
	const char *p = c;
	str nm;

	while (*p == ' ')
		p++;
	if (isdigit((unsigned char)*p)) {
		size_t ln = (size_t)atol(p);
		size_t nl = vi_nlines(&e->b);
		if (ln < 1)
			ln = 1;
		if (ln > nl)
			ln = nl;
		e->cur = vi_lstart(&e->b, ln - 1);
		return;
	}
	s_init(&nm);
	while (*p && *p != ' ' && *p != '!')
		s_ch(&nm, *p++);
	if (*p == '!') {
		force = 1;
		p++;
	}
	while (*p == ' ')
		p++;
	if (!strcmp(nm.p ? nm.p : "", "w") ||
	    !strcmp(nm.p ? nm.p : "", "wq") ||
	    !strcmp(nm.p ? nm.p : "", "x")) {
		const char *to = *p ? p : e->path;
		if (!to) {
			vi_say(e, "no file name");
			s_free(&nm);
			return;
		}
		if (!force && (!*p || !strcmp(p, e->path ? e->path : "")) &&
		    vi_changed(e)) {
			vi_say(e, "file changed on disk; :w! to overwrite");
			s_free(&nm);
			return;
		}
		if (vi_save(&e->b, to) != HIBR_OK) {
			vi_say(e, "write failed");
			s_free(&nm);
			return;
		}
		e->b.mod = 0;
		if (!*p || !strcmp(p, e->path ? e->path : ""))
			vi_stamp(e);
		vi_say(e, "written");
		if (strcmp(nm.p, "w"))
			e->quit = 1;
		s_free(&nm);
		return;
	}
	if (!strcmp(nm.p ? nm.p : "", "q")) {
		if (e->b.mod && !force)
			vi_say(e, "unsaved changes; :q! to leave anyway");
		else
			e->quit = 1;
		s_free(&nm);
		return;
	}
	if (!strcmp(nm.p ? nm.p : "", "e")) {
		if (e->b.mod && !force) {
			vi_say(e, "unsaved changes; :e! to reload anyway");
			s_free(&nm);
			return;
		}
		if (*p) {
			free(e->path);
			e->path = xs(p);
		}
		vi_load(&e->b, e->path);
		vi_stamp(e);
		vi_ufree(e);
		e->cur = 0;
		e->top = 0;
		vi_say(e, "read");
		s_free(&nm);
		return;
	}
	vi_say(e, "not a command");
	s_free(&nm);
}

/* Handle one key in normal or visual mode. */
void vi_normal(vi_ed *e, const char *k, int vh)
{
	vi_buf *b = &e->b;
	size_t ln = vi_lineof(b, e->cur), nl = vi_nlines(b);
	int vis = e->mode == VI_VISUAL || e->mode == VI_VLINE;
	char op = e->pend.n == 1 ? e->pend.p[0] : 0;

	if (!strcmp(k, "escape")) {
		e->pend.n = 0;
		if (e->pend.p)
			e->pend.p[0] = 0;
		if (vis)
			e->mode = VI_NORMAL;
		return;
	}
	/* an operator waiting for a motion */
	if (op && !vis) {
		size_t from = e->cur, to = e->cur;
		int line = 0, ok = 1;
		if (!strcmp(k, "w"))
			to = vi_wnext(b, e->cur);
		else if (!strcmp(k, "b"))
			to = vi_wprev(b, e->cur);
		else if (!strcmp(k, "e"))
			to = vi_next(b, vi_wend(b, e->cur));
		else if (!strcmp(k, "$"))
			to = vi_lend(b, ln);
		else if (!strcmp(k, "0"))
			to = vi_lstart(b, ln);
		else if (!strcmp(k, "G")) {
			to = vi_lstart(b, nl - 1);
			line = 1;
		} else if (k[0] == op && !k[1]) {
			line = 1;
		} else {
			ok = 0;
		}
		e->pend.n = 0;
		if (e->pend.p)
			e->pend.p[0] = 0;
		if (ok)
			vi_operate(e, op, from, to, line);
		return;
	}
	if (!strcmp(k, "h") || !strcmp(k, "left")) {
		if (e->cur > vi_lstart(b, ln))
			e->cur = vi_prev(b, e->cur);
		e->wantcol = vi_col(b, e->cur);
	} else if (!strcmp(k, "l") || !strcmp(k, "right")) {
		if (e->cur < vi_lend(b, ln))
			e->cur = vi_next(b, e->cur);
		vi_clamp(e);
		e->wantcol = vi_col(b, e->cur);
	} else if (!strcmp(k, "j") || !strcmp(k, "down")) {
		if (ln + 1 < nl)
			e->cur = vi_atcol(b, ln + 1, e->wantcol);
	} else if (!strcmp(k, "k") || !strcmp(k, "up")) {
		if (ln)
			e->cur = vi_atcol(b, ln - 1, e->wantcol);
	} else if (!strcmp(k, "0") || !strcmp(k, "home")) {
		e->cur = vi_lstart(b, ln);
		e->wantcol = 0;
	} else if (!strcmp(k, "$") || !strcmp(k, "end")) {
		e->cur = vi_lend(b, ln);
		vi_clamp(e);
		e->wantcol = 9999;
	} else if (!strcmp(k, "^")) {
		size_t i = vi_lstart(b, ln), t = vi_lend(b, ln);
		while (i < t && isspace(vi_at(b, i)))
			i = vi_next(b, i);
		e->cur = i;
	} else if (!strcmp(k, "w")) {
		e->cur = vi_wnext(b, e->cur);
		vi_clamp(e);
	} else if (!strcmp(k, "b")) {
		e->cur = vi_wprev(b, e->cur);
	} else if (!strcmp(k, "e")) {
		e->cur = vi_wend(b, e->cur);
		vi_clamp(e);
	} else if (!strcmp(k, "G")) {
		e->cur = vi_lstart(b, nl - 1);
	} else if (!strcmp(k, "g")) {
		s_cat(&e->pend, "g");
		return;
	} else if (!strcmp(k, "pagedown") || !strcmp(k, "ctrl-f")) {
		e->cur = vi_atcol(b, ln + (size_t)vh < nl ? ln + (size_t)vh
							  : nl - 1, e->wantcol);
	} else if (!strcmp(k, "pageup") || !strcmp(k, "ctrl-b")) {
		e->cur = vi_atcol(b, ln > (size_t)vh ? ln - (size_t)vh : 0,
				  e->wantcol);
	} else if (!strcmp(k, "ctrl-d")) {
		e->cur = vi_atcol(b, ln + (size_t)vh / 2 < nl
					     ? ln + (size_t)vh / 2 : nl - 1,
				  e->wantcol);
	} else if (!strcmp(k, "ctrl-u")) {
		e->cur = vi_atcol(b, ln > (size_t)vh / 2 ? ln - (size_t)vh / 2
							 : 0, e->wantcol);
	} else if (!strcmp(k, "i")) {
		e->mode = VI_INSERT;
		vi_ugroup(e);
	} else if (!strcmp(k, "a")) {
		if (e->cur < vi_lend(b, ln))
			e->cur = vi_next(b, e->cur);
		e->mode = VI_INSERT;
		vi_ugroup(e);
	} else if (!strcmp(k, "I")) {
		size_t i = vi_lstart(b, ln), t = vi_lend(b, ln);
		while (i < t && isspace(vi_at(b, i)))
			i = vi_next(b, i);
		e->cur = i;
		e->mode = VI_INSERT;
		vi_ugroup(e);
	} else if (!strcmp(k, "A")) {
		e->cur = vi_lend(b, ln);
		e->mode = VI_INSERT;
		vi_ugroup(e);
	} else if (!strcmp(k, "o") || !strcmp(k, "O")) {
		size_t at = !strcmp(k, "o") ? vi_lend(b, ln) : vi_lstart(b, ln);
		vi_ugroup(e);
		vi_eins(e, at, "\n", 1);
		e->cur = !strcmp(k, "o") ? at + 1 : at;
		e->mode = VI_INSERT;
	} else if (!strcmp(k, "x") || !strcmp(k, "delete")) {
		size_t nx = vi_next(b, e->cur);
		if (e->cur < vi_lend(b, ln)) {
			vi_ugroup(e);
			vi_setyank(e, e->cur, nx - e->cur, 0);
			vi_edel(e, e->cur, nx - e->cur);
			vi_clamp(e);
		}
	} else if (!strcmp(k, "X")) {
		if (e->cur > vi_lstart(b, ln)) {
			size_t pv = vi_prev(b, e->cur);
			vi_ugroup(e);
			vi_edel(e, pv, e->cur - pv);
			e->cur = pv;
		}
	} else if (!strcmp(k, "D")) {
		vi_ugroup(e);
		vi_edel(e, e->cur, vi_lend(b, ln) - e->cur);
		vi_clamp(e);
	} else if (!strcmp(k, "C")) {
		vi_ugroup(e);
		vi_edel(e, e->cur, vi_lend(b, ln) - e->cur);
		e->mode = VI_INSERT;
	} else if (!strcmp(k, "J")) {
		size_t t = vi_lend(b, ln);
		if (ln + 1 < nl) {
			vi_ugroup(e);
			vi_edel(e, t, 1);
			vi_eins(e, t, " ", 1);
			e->cur = t;
		}
	} else if (!strcmp(k, "d") || !strcmp(k, "c") || !strcmp(k, "y")) {
		if (vis) {
			size_t a = e->vstart, z = e->cur;
			if (z < a) {
				size_t sw = a;
				a = z;
				z = sw;
			}
			/* visual takes in the character under the cursor */
			if (e->mode == VI_VISUAL)
				z = vi_next(b, z);
			vi_operate(e, k[0], a, z, e->mode == VI_VLINE);
			e->mode = e->mode == VI_VLINE || k[0] != 'c'
					  ? VI_NORMAL : VI_INSERT;
			if (k[0] == 'c')
				e->mode = VI_INSERT;
		} else {
			s_cat(&e->pend, k);
			return;
		}
	} else if (!strcmp(k, "p") || !strcmp(k, "P")) {
		size_t at;
		if (!e->yank.n)
			return;
		vi_ugroup(e);
		if (e->yline) {
			at = !strcmp(k, "p") ? vi_lend(b, ln) + 1
					     : vi_lstart(b, ln);
			if (at > vi_len(b))
				at = vi_len(b);
			vi_eins(e, at, e->yank.p, e->yank.n);
			e->cur = at;
		} else {
			at = !strcmp(k, "p") ? vi_next(b, e->cur) : e->cur;
			if (at > vi_len(b))
				at = vi_len(b);
			vi_eins(e, at, e->yank.p, e->yank.n);
			e->cur = at + e->yank.n - 1;
		}
		vi_clamp(e);
	} else if (!strcmp(k, "u")) {
		if (!vi_undo1(e))
			vi_say(e, "nothing to undo");
		vi_clamp(e);
	} else if (!strcmp(k, "ctrl-r")) {
		if (!vi_redo1(e))
			vi_say(e, "nothing to redo");
		vi_clamp(e);
	} else if (!strcmp(k, "v")) {
		if (e->mode == VI_VISUAL) {
			e->mode = VI_NORMAL;
		} else {
			e->vstart = e->cur;
			e->mode = VI_VISUAL;
		}
	} else if (!strcmp(k, "V")) {
		if (e->mode == VI_VLINE) {
			e->mode = VI_NORMAL;
		} else {
			e->vstart = e->cur;
			e->mode = VI_VLINE;
		}
	} else if (!strcmp(k, "n")) {
		vi_search(e, 0);
	} else if (!strcmp(k, "N")) {
		vi_search(e, 1);
	} else if (!strcmp(k, ":") || !strcmp(k, "/")) {
		e->cmd.n = 0;
		if (e->cmd.p)
			e->cmd.p[0] = 0;
		if (!strcmp(k, "/"))
			s_cat(&e->cmd, "/");
		e->mode = VI_EX;
	}
	if (!vis && e->mode != VI_INSERT)
		vi_clamp(e);
}

/* Handle one key while typing text. */
void vi_insert(vi_ed *e, const char *k)
{
	if (!strcmp(k, "escape")) {
		e->mode = VI_NORMAL;
		vi_ugroup(e);
		if (e->cur)
			e->cur = vi_prev(&e->b, e->cur);
		vi_clamp(e);
		return;
	}
	if (!strcmp(k, "enter")) {
		vi_eins(e, e->cur, "\n", 1);
		e->cur++;
		return;
	}
	if (!strcmp(k, "tab")) {
		vi_eins(e, e->cur, "\t", 1);
		e->cur++;
		return;
	}
	if (!strcmp(k, "backspace")) {
		if (e->cur) {
			size_t pv = vi_prev(&e->b, e->cur);
			vi_edel(e, pv, e->cur - pv);
			e->cur = pv;
		}
		return;
	}
	if (!strcmp(k, "left")) {
		if (e->cur)
			e->cur = vi_prev(&e->b, e->cur);
		return;
	}
	if (!strcmp(k, "right")) {
		if (e->cur < vi_len(&e->b))
			e->cur = vi_next(&e->b, e->cur);
		return;
	}
	if (!strncmp(k, "paste ", 6)) {
		vi_eins(e, e->cur, k + 6, strlen(k + 6));
		e->cur += strlen(k + 6);
		return;
	}
	if (strlen(k) <= 4 && (unsigned char)k[0] >= 32) {
		vi_eins(e, e->cur, k, strlen(k));
		e->cur += strlen(k);
	}
}

/* Handle one key on the colon or slash line. */
void vi_exkey(vi_ed *e, const char *k)
{
	if (!strcmp(k, "escape")) {
		e->mode = VI_NORMAL;
		return;
	}
	if (!strcmp(k, "enter")) {
		e->mode = VI_NORMAL;
		if (e->cmd.n && e->cmd.p[0] == '/') {
			e->find.n = 0;
			if (e->find.p)
				e->find.p[0] = 0;
			s_cat(&e->find, e->cmd.p + 1);
			vi_search(e, 0);
		} else if (e->cmd.n) {
			vi_ex(e, e->cmd.p);
		}
		return;
	}
	if (!strcmp(k, "backspace")) {
		if (e->cmd.n) {
			e->cmd.n--;
			e->cmd.p[e->cmd.n] = 0;
		} else {
			e->mode = VI_NORMAL;
		}
		return;
	}
	if (strlen(k) <= 4 && (unsigned char)k[0] >= 32)
		s_cat(&e->cmd, k);
}

/* Edit a file. */
int m_vi(sh *s, int ac, char **av)
{
	int rows, cols, vh, r;
	str key;

	dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!dp) {
		lg(HIBR_LERR, "vi: needs a display; mod load console");
		return HIBR_FAIL;
	}
	if (ac > 2) {
		lg(HIBR_LERR, "usage: vi [file]");
		return 2;
	}
	memset(&ed, 0, sizeof ed);
	vi_binit(&ed.b);
	s_init(&ed.find);
	s_init(&ed.cmd);
	s_init(&ed.msg);
	s_init(&ed.yank);
	s_init(&ed.pend);
	s_init(&key);
	if (ac == 2) {
		ed.path = xs(av[1]);
		if (vi_load(&ed.b, ed.path) != HIBR_OK) {
			lg(HIBR_LERR, "vi: %s: cannot read", ed.path);
			return HIBR_FAIL;
		}
		vi_stamp(&ed);
	}
	if (dp->open(s) != HIBR_OK)
		return HIBR_FAIL;
	while (!ed.quit) {
		dp->size(&rows, &cols);
		vh = rows - 1;
		vi_scroll(&ed, vh);
		vi_render(&ed, rows, cols);
		key.n = 0;
		if (key.p)
			key.p[0] = 0;
		r = dp->key(-1, &key);
		if (r < 0)
			break;
		if (r == 0)
			continue;
		ed.msg.n = 0;
		if (ed.msg.p)
			ed.msg.p[0] = 0;
		if (ed.mode == VI_EX)
			vi_exkey(&ed, key.p ? key.p : "");
		else if (ed.mode == VI_INSERT)
			vi_insert(&ed, key.p ? key.p : "");
		else
			vi_normal(&ed, key.p ? key.p : "", vh);
		if (ed.pend.n == 1 && ed.pend.p[0] == 'g') {
			key.n = 0;
			if (key.p)
				key.p[0] = 0;
			if (dp->key(-1, &key) == 1 && key.p && !strcmp(key.p, "g"))
				ed.cur = 0;
			ed.pend.n = 0;
			if (ed.pend.p)
				ed.pend.p[0] = 0;
		}
	}
	dp->close(s);
	vi_ufree(&ed);
	vi_bfree(&ed.b);
	free(ed.path);
	s_free(&ed.find);
	s_free(&ed.cmd);
	s_free(&ed.msg);
	s_free(&ed.yank);
	s_free(&ed.pend);
	s_free(&key);
	return HIBR_OK;
}

const hibr_bi vi_bi[] = {
	{ "vi", m_vi, "edit a file" },
	HIBR_BI_END
};

HIBR_MODULE("vi", "0.21", "a modal editor on the display interface", vi_bi, 0, 0);
