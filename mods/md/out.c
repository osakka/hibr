#define _GNU_SOURCE

#include "mk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A line break unless the output already ends a line. */
static void mk_cr(str *o)
{
	if (o->n && o->p[o->n - 1] != '\n')
		s_ch(o, '\n');
}

/* Raw HTML, with GFM's disallowed tags made harmless by escaping their <. */
static void mk_raw(str *o, const char *p, size_t n, int gfm)
{
	static const char *bad[] = { "title", "textarea", "style", "xmp",
				     "iframe", "noembed", "noframes", "script",
				     "plaintext", 0 };
	size_t i, k, t;
	int j;

	for (i = 0; i < n; i++) {
		if (gfm && p[i] == '<') {
			t = i + 1 < n && p[i + 1] == '/' ? i + 2 : i + 1;
			for (j = 0; bad[j]; j++) {
				k = strlen(bad[j]);
				if (t + k <= n && !strncasecmp(p + t, bad[j], k) &&
				    (t + k == n || strchr(" \t\n\r\f>", p[t + k]) ||
				     (p[t + k] == '/' && t + k + 1 < n &&
				      p[t + k + 1] == '>')))
					break;
			}
			if (bad[j]) {
				s_cat(o, "&lt;");
				continue;
			}
		}
		s_ch(o, p[i]);
	}
}

/* The plain text of inline nodes, for an image's alt. */
static void mk_plain(mk_n *n, str *o)
{
	mk_n *k;

	for (k = n->kid; k; k = k->nx) {
		switch (k->t) {
		case MK_TEXT:
		case MK_CSPAN:
		case MK_RAW:
			mk_esc(o, k->s.p, k->s.n);
			break;
		case MK_SOFT:
		case MK_HARD:
			s_ch(o, ' ');
			break;
		default:
			mk_plain(k, o);
		}
	}
}

/* Whether a paragraph sits in a tight list's item. */
static int mk_tightp(mk_n *n)
{
	return n->up && n->up->t == MK_ITEM && n->up->up &&
	       n->up->up->t == MK_LIST && n->up->up->tight;
}

static void mk_kids(mk_n *n, str *o, int gfm)
{
	mk_n *k;

	for (k = n->kid; k; k = k->nx)
		mk_html(k, o, gfm);
}

/* A table cell's opening tag, with its alignment. */
static void mk_cellopen(mk_n *c, str *o, int head)
{
	s_cat(o, head ? "<th" : "<td");
	switch (c->align) {
	case 'l':
		s_cat(o, " align=\"left\"");
		break;
	case 'c':
		s_cat(o, " align=\"center\"");
		break;
	case 'r':
		s_cat(o, " align=\"right\"");
		break;
	}
	s_ch(o, '>');
}

/* A table: head row, then the body rows if there are any. */
static void mk_table(mk_n *t, str *o, int gfm)
{
	mk_n *r, *c;
	int body = 0;

	mk_cr(o);
	s_cat(o, "<table>\n");
	for (r = t->kid; r; r = r->nx) {
		if (r->head) {
			s_cat(o, "<thead>\n");
		} else if (!body) {
			s_cat(o, "<tbody>\n");
			body = 1;
		}
		s_cat(o, "<tr>\n");
		for (c = r->kid; c; c = c->nx) {
			mk_cellopen(c, o, r->head);
			mk_kids(c, o, gfm);
			s_cat(o, r->head ? "</th>\n" : "</td>\n");
		}
		s_cat(o, "</tr>\n");
		if (r->head)
			s_cat(o, "</thead>\n");
	}
	if (body)
		s_cat(o, "</tbody>\n");
	s_cat(o, "</table>\n");
}

/* Render a node and everything under it as HTML, the way cmark-gfm does. */
void mk_html(mk_n *n, str *o, int gfm)
{
	char b[32];
	size_t k;

	switch (n->t) {
	case MK_DOC:
		mk_kids(n, o, gfm);
		break;
	case MK_QUOTE:
		mk_cr(o);
		s_cat(o, "<blockquote>\n");
		mk_kids(n, o, gfm);
		mk_cr(o);
		s_cat(o, "</blockquote>\n");
		break;
	case MK_LIST:
		mk_cr(o);
		if (n->lt && n->start != 1) {
			snprintf(b, sizeof b, "<ol start=\"%d\">\n", n->start);
			s_cat(o, b);
		} else {
			s_cat(o, n->lt ? "<ol>\n" : "<ul>\n");
		}
		mk_kids(n, o, gfm);
		s_cat(o, n->lt ? "</ol>\n" : "</ul>\n");
		break;
	case MK_ITEM:
		mk_cr(o);
		s_cat(o, "<li>");
		if (n->task >= 0)
			s_cat(o, n->task ? "<input checked=\"\" disabled=\"\" type=\"checkbox\">"
					 : "<input disabled=\"\" type=\"checkbox\">");
		mk_kids(n, o, gfm);
		s_cat(o, "</li>\n");
		break;
	case MK_PARA:
		if (mk_tightp(n)) {
			mk_kids(n, o, gfm);
			break;
		}
		mk_cr(o);
		s_cat(o, "<p>");
		mk_kids(n, o, gfm);
		s_cat(o, "</p>\n");
		break;
	case MK_HEAD:
		mk_cr(o);
		snprintf(b, sizeof b, "<h%d>", n->lvl);
		s_cat(o, b);
		mk_kids(n, o, gfm);
		snprintf(b, sizeof b, "</h%d>\n", n->lvl);
		s_cat(o, b);
		break;
	case MK_HR:
		mk_cr(o);
		s_cat(o, "<hr />\n");
		break;
	case MK_CODE:
		mk_cr(o);
		s_cat(o, "<pre><code");
		if (n->info.n) {
			k = strcspn(n->info.p, " \t");
			s_cat(o, " class=\"language-");
			mk_esc(o, n->info.p, k);
			s_ch(o, '"');
		}
		s_ch(o, '>');
		mk_esc(o, n->s.p, n->s.n);
		s_cat(o, "</code></pre>\n");
		break;
	case MK_HTML:
		mk_cr(o);
		mk_raw(o, n->s.p, n->s.n, gfm);
		mk_cr(o);
		break;
	case MK_TABLE:
		mk_table(n, o, gfm);
		break;
	case MK_TEXT:
		mk_esc(o, n->s.p, n->s.n);
		break;
	case MK_SOFT:
		s_ch(o, '\n');
		break;
	case MK_HARD:
		s_cat(o, "<br />\n");
		break;
	case MK_CSPAN:
		s_cat(o, "<code>");
		mk_esc(o, n->s.p, n->s.n);
		s_cat(o, "</code>");
		break;
	case MK_EMPH:
		s_cat(o, "<em>");
		mk_kids(n, o, gfm);
		s_cat(o, "</em>");
		break;
	case MK_STRONG:
		s_cat(o, "<strong>");
		mk_kids(n, o, gfm);
		s_cat(o, "</strong>");
		break;
	case MK_DEL:
		s_cat(o, "<del>");
		mk_kids(n, o, gfm);
		s_cat(o, "</del>");
		break;
	case MK_LINK:
		s_cat(o, "<a href=\"");
		mk_eschref(o, n->url.p, n->url.n);
		s_ch(o, '"');
		if (n->title.n) {
			s_cat(o, " title=\"");
			mk_esc(o, n->title.p, n->title.n);
			s_ch(o, '"');
		}
		s_ch(o, '>');
		mk_kids(n, o, gfm);
		s_cat(o, "</a>");
		break;
	case MK_IMG:
		s_cat(o, "<img src=\"");
		mk_eschref(o, n->url.p, n->url.n);
		s_cat(o, "\" alt=\"");
		mk_plain(n, o);
		s_ch(o, '"');
		if (n->title.n) {
			s_cat(o, " title=\"");
			mk_esc(o, n->title.p, n->title.n);
			s_ch(o, '"');
		}
		s_cat(o, " />");
		break;
	case MK_RAW:
		mk_raw(o, n->s.p, n->s.n, gfm);
		break;
	}
}

/* Mark the source bytes an inline node's content covers with its style;
   a heading keeps its own letter throughout. */
static void mk_stin(mk_n *leaf, mk_n *n, char *sty, size_t sn, char st)
{
	mk_n *k;
	long i, at;
	char me = st;

	if (st < '1' || st > '6') {
		switch (n->t) {
		case MK_EMPH:
			me = st == 'b' || st == 'B' ? 'B' : 'i';
			break;
		case MK_STRONG:
			me = st == 'i' || st == 'B' ? 'B' : 'b';
			break;
		case MK_DEL:
			me = 's';
			break;
		case MK_LINK:
			me = 'l';
			break;
		case MK_IMG:
			me = 'g';
			break;
		case MK_CSPAN:
			me = 'c';
			break;
		}
	}
	if (n->t == MK_TEXT || n->t == MK_CSPAN || n->t == MK_RAW) {
		for (i = n->ca; i < n->cb; i++) {
			if (i < 0 || (size_t)i >= leaf->s.n)
				continue;
			at = leaf->off[i];
			if (at >= 0 && (size_t)at < sn)
				sty[at] = n->t == MK_RAW && me == st ? 'h' : me;
		}
		return;
	}
	for (k = n->kid; k; k = k->nx)
		mk_stin(leaf, k, sty, sn, me);
}

/* Mark from a to b with c where the source holds one of the bytes in set
   and nothing has claimed it. */
static void mk_stset(const char *src, char *sty, size_t sn, long a, long b,
		     const char *set, char c)
{
	long i;

	for (i = a; i < b && i >= 0 && (size_t)i < sn; i++)
		if (sty[i] == 'm' && strchr(set, src[i]))
			sty[i] = c;
}

/* Mark a node's own source bytes, and what is under it. */
static void mk_stblk(mk_n *n, const char *src, char *sty, size_t sn, char q)
{
	mk_n *k, *c;
	size_t i;
	long at;
	char st;

	switch (n->t) {
	case MK_PARA:
	case MK_HEAD:
		st = n->t == MK_HEAD ? (char)('0' + n->lvl) : q ? q : 'p';
		for (k = n->kid; k; k = k->nx)
			mk_stin(n, k, sty, sn, st);
		return;
	case MK_CODE:
	case MK_HTML:
		for (i = 0; i < n->s.n; i++) {
			at = n->off[i];
			if (at >= 0 && (size_t)at < sn && n->s.p[i] != '\n')
				sty[at] = n->t == MK_CODE ? 'f' : 'h';
		}
		return;
	case MK_HR:
		mk_stset(src, sty, sn, n->a, n->b, "-*_", 'r');
		return;
	case MK_TABLE:
		mk_stset(src, sty, sn, n->ca, n->cb, "-:|", '=');
		for (k = n->kid; k; k = k->nx) {
			for (c = k->kid; c; c = c->nx) {
				mk_n *x;
				for (x = c->kid; x; x = x->nx)
					mk_stin(c, x, sty, sn, k->head ? 'b' : 'p');
			}
			mk_stset(src, sty, sn, k->a, k->b, "|", '|');
			mk_stset(src, sty, sn, k->a, k->b, " \t", ' ');
		}
		return;
	case MK_ITEM:
		at = n->a;
		if (at >= 0 && (size_t)at < sn) {
			if (!n->lt) {
				sty[at] = n->task >= 0 ? 'm' : '-';
			} else {
				while ((size_t)at < sn && src[at] >= '0' && src[at] <= '9')
					sty[at++] = 'n';
				if ((size_t)at < sn)
					sty[at] = 'n';
			}
			for (at++; (size_t)at < sn && (src[at] == ' ' || src[at] == '\t') &&
			     sty[at] == 'm'; at++)
				sty[at] = n->task >= 0 && !n->lt ? 'm' : ' ';
		}
		break;
	case MK_QUOTE:
		q = 'q';
		break;
	}
	for (k = n->kid; k; k = k->nx)
		mk_stblk(k, src, sty, sn, q);
	if (n->t == MK_ITEM && n->task >= 0 && n->taskat >= 0) {
		for (at = n->taskat; at < n->taskat + 3 && (size_t)at < sn; at++)
			sty[at] = n->task ? 'X' : 'x';
	}
}

/* A style letter for every source byte: content by what it is, markup m,
   and structure by its own code -- - a bullet, n a list number, x and X a
   task box, > a quote's mark, r a rule, | a table's pipe, = its delimiter
   row -- with the blanks that start a line kept as a space. */
void mk_styles(mk_n *doc, const char *src, size_t n, char *sty)
{
	size_t i;
	int start = 1;

	memset(sty, 'm', n);
	mk_stblk(doc, src, sty, n, 0);
	for (i = 0; i < n; i++) {
		if (src[i] == '\n') {
			start = 1;
			continue;
		}
		if (!start)
			continue;
		if (sty[i] == 'm' && (src[i] == ' ' || src[i] == '\t'))
			sty[i] = ' ';
		else if (sty[i] == 'm' && src[i] == '>')
			sty[i] = '>';
		else if (sty[i] != ' ' && sty[i] != '>')
			start = 0;
	}
}
