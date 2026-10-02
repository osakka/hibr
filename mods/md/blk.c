#define _GNU_SOURCE

#include "mk.h"
#include <stdlib.h>
#include <string.h>

/* The line's byte at i, or 0 past its end. */
static char mk_pk(mk_p *p, size_t i)
{
	return i < p->lnn ? p->ln[i] : 0;
}

static int mk_spt(char c)
{
	return c == ' ' || c == '\t';
}

/* Find the first non-blank from the current offset: where it is, its
   column, how far it is indented, and whether the rest is blank. */
static void mk_fnsp(mk_p *p)
{
	size_t i = p->off;
	int col = p->col, cw;

	if (p->ptab) {
		cw = 4 - (col % 4);
		col += cw;
		i++;
	}
	while (i < p->lnn) {
		if (p->ln[i] == ' ') {
			col++;
		} else if (p->ln[i] == '\t') {
			col += 4 - (col % 4);
		} else {
			break;
		}
		i++;
	}
	p->fns = i;
	p->fnscol = col;
	p->indent = col - p->col;
	p->blank = i >= p->lnn;
}

/* Advance count bytes, or count columns when cols is set -- a tab can be
   partly used up by columns, the rest of it left for the content. */
static void mk_adv(mk_p *p, int count, int cols)
{
	char c;
	int tw, n;

	while (count > 0 && p->off < p->lnn) {
		c = p->ln[p->off];
		if (c == '\t') {
			tw = 4 - (p->col % 4);
			if (cols) {
				p->ptab = tw > count;
				n = tw < count ? tw : count;
				p->col += n;
				if (!p->ptab)
					p->off++;
				count -= n;
			} else {
				p->ptab = 0;
				p->col += tw;
				p->off++;
				count--;
			}
		} else {
			p->ptab = 0;
			p->off++;
			p->col++;
			count--;
		}
	}
}

/* Add the rest of the line to a leaf, and its newline. A tab partly used
   up by indentation becomes the spaces left of it. */
static void mk_addline(mk_p *p, mk_n *n)
{
	int k;

	if (p->ptab) {
		p->off++;
		k = 4 - (p->col % 4);
		while (k-- > 0)
			mk_addb(n, " ", 1, -(p->lnoff + (long)p->off - 1) - 1);
		p->ptab = 0;
	}
	if (p->off < p->lnn)
		mk_addb(n, p->ln + p->off, p->lnn - p->off, p->lnoff + (long)p->off);
	mk_addb(n, "\n", 1, p->lnoff + (long)p->lnn);
}

static int mk_cancontain(int up, int t)
{
	switch (up) {
	case MK_DOC:
	case MK_QUOTE:
	case MK_ITEM:
		return t != MK_ITEM;
	case MK_LIST:
		return t == MK_ITEM;
	}
	return 0;
}

static int mk_takeslines(int t)
{
	return t == MK_PARA || t == MK_HEAD || t == MK_CODE || t == MK_HTML ||
	       t == MK_TABLE;
}

/* Remove the first k bytes of a leaf's content. */
static void mk_chop(mk_n *n, size_t k)
{
	if (k > n->s.n)
		k = n->s.n;
	memmove(n->s.p, n->s.p + k, n->s.n - k);
	memmove(n->off, n->off + k, (n->s.n - k) * sizeof *n->off);
	n->s.n -= k;
	n->s.p[n->s.n] = 0;
}

/* Trim trailing whitespace from a leaf's content. */
static void mk_rtrim(mk_n *n)
{
	while (n->s.n && strchr(" \t\n\r", n->s.p[n->s.n - 1]))
		n->s.n--;
	if (n->s.p)
		n->s.p[n->s.n] = 0;
}

/* Take link reference definitions off the start of a paragraph; whether
   any text is left. */
static int mk_takerefs(mk_p *p, mk_n *n)
{
	size_t k, at = 0;

	while (at < n->s.n && n->s.p[at] == '[' &&
	       (k = mk_refdef(n->s.p + at, n->s.n - at, &p->refs, n, 0)))
		at += k;
	if (at)
		mk_chop(n, at);
	return n->s.n > 0 && strspn(n->s.p, " \t\n\r") < n->s.n;
}

/* Whether a node, or the last thing in it, ended on a blank line. */
static int mk_endsblank(mk_n *n)
{
	while (n) {
		if (n->lb)
			return 1;
		if (n->t != MK_LIST && n->t != MK_ITEM)
			return 0;
		n = n->last;
	}
	return 0;
}

/* Close a block, and return the one it was in. */
static mk_n *mk_finalize(mk_p *p, mk_n *n)
{
	mk_n *up = n->up, *it, *k;
	size_t i;

	n->open = 0;
	n->el = p->lineno;
	switch (n->t) {
	case MK_PARA:
		if (!mk_takerefs(p, n)) {
			mk_unlink(n);
			mk_free(n);
			break;
		}
		if (p->gfm && up && up->t == MK_ITEM && up->kid == n &&
		    n->s.n >= 4 && n->s.p[0] == '[' && n->s.p[2] == ']' &&
		    strchr(" xX", n->s.p[1]) && strchr(" \t\n", n->s.p[3])) {
			up->task = n->s.p[1] != ' ';
			up->taskat = n->off[0];
			mk_chop(n, 3);
		}
		break;
	case MK_CODE:
		if (n->fence) {
			i = 0;
			while (i < n->s.n && n->s.p[i] != '\n')
				i++;
			s_init(&n->info);
			{
				size_t a = 0, b = i;
				while (a < b && strchr(" \t", n->s.p[a]))
					a++;
				while (b > a && strchr(" \t", n->s.p[b - 1]))
					b--;
				mk_unesc(&n->info, n->s.p + a, b - a);
			}
			mk_chop(n, i < n->s.n ? i + 1 : i);
		} else {
			while (n->s.n) {
				size_t e = n->s.n - 1, b = e;
				while (b > 0 && n->s.p[b - 1] != '\n')
					b--;
				if (strspn(n->s.p + b, " \t") >= e - b) {
					n->s.n = b;
					n->s.p[n->s.n] = 0;
				} else {
					break;
				}
			}
		}
		break;
	case MK_HEAD:
		mk_rtrim(n);
		while (n->s.n && strchr(" \t", n->s.p[0]))
			mk_chop(n, 1);
		break;
	case MK_LIST:
		n->tight = 1;
		for (it = n->kid; it; it = it->nx) {
			if (it->lb && it->nx) {
				n->tight = 0;
				break;
			}
			for (k = it->kid; k; k = k->nx)
				if (mk_endsblank(k) && (it->nx || k->nx)) {
					n->tight = 0;
					break;
				}
			if (!n->tight)
				break;
		}
		break;
	}
	return up;
}

/* Close blocks until up can hold a child of type t, then add one. */
static mk_n *mk_addchild(mk_p *p, mk_n *up, int t)
{
	mk_n *n;

	while (!mk_cancontain(up->t, t))
		up = mk_finalize(p, up);
	n = mk_new(t);
	n->sl = p->lineno;
	n->dep = up->dep + 1;
	mk_append(up, n);
	return n;
}

/* An ATX heading's start at i: its level, the # run's length in *len. */
static int mk_atx(mk_p *p, size_t i, int *len)
{
	int k = 0;

	while (mk_pk(p, i + k) == '#' && k < 7)
		k++;
	if (k < 1 || k > 6)
		return 0;
	if (i + k < p->lnn && !mk_spt(p->ln[i + k]))
		return 0;
	*len = k;
	return k;
}

/* A code fence at i: its length, with its character in *ch; 0 if none. */
static int mk_fenceopen(mk_p *p, size_t i, char *ch)
{
	char c = mk_pk(p, i);
	int k = 0;
	size_t j;

	if (c != '`' && c != '~')
		return 0;
	while (mk_pk(p, i + k) == c)
		k++;
	if (k < 3)
		return 0;
	if (c == '`')
		for (j = i + k; j < p->lnn; j++)
			if (p->ln[j] == '`')
				return 0;
	*ch = c;
	return k;
}

/* Whether the line at i closes a fence of ch, at least len long. */
static int mk_fenceclose(mk_p *p, size_t i, char ch, int len)
{
	int k = 0;
	size_t j;

	while (mk_pk(p, i + k) == ch)
		k++;
	if (k < len)
		return 0;
	for (j = i + k; j < p->lnn; j++)
		if (!mk_spt(p->ln[j]))
			return 0;
	return 1;
}

/* A thematic break at i: three or more of one of * - _, with nothing but
   blanks between and after. */
static int mk_hr(mk_p *p, size_t i)
{
	char c = mk_pk(p, i);
	int k = 0;
	size_t j;

	if (c != '*' && c != '-' && c != '_')
		return 0;
	for (j = i; j < p->lnn; j++) {
		if (p->ln[j] == c)
			k++;
		else if (!mk_spt(p->ln[j]))
			return 0;
	}
	return k >= 3;
}

/* A setext underline at i: 1 for =, 2 for -, else 0. */
static int mk_setext(mk_p *p, size_t i)
{
	char c = mk_pk(p, i);
	size_t j = i;

	if (c != '=' && c != '-')
		return 0;
	while (j < p->lnn && p->ln[j] == c)
		j++;
	while (j < p->lnn && mk_spt(p->ln[j]))
		j++;
	if (j < p->lnn)
		return 0;
	return c == '=' ? 1 : 2;
}

static const char *mk_blocktags[] = {
	"address", "article", "aside", "base", "basefont", "blockquote", "body",
	"caption", "center", "col", "colgroup", "dd", "details", "dialog", "dir",
	"div", "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form",
	"frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
	"hr", "html", "iframe", "legend", "li", "link", "main", "menu",
	"menuitem", "nav", "noframes", "ol", "optgroup", "option", "p", "param",
	"search", "section", "summary", "table", "tbody", "td", "tfoot", "th",
	"thead", "title", "tr", "track", "ul", 0
};

/* Whether text at q, n long, starts with word, ignoring case. */
static int mk_ci(const char *q, size_t n, const char *word)
{
	size_t k = strlen(word);

	return n >= k && !strncasecmp(q, word, k);
}

/* The kind of HTML block that starts at i, 1 to 7, or 0. Type 7 cannot
   interrupt a paragraph. */
static int mk_htmlstart(mk_p *p, size_t i, int inpara)
{
	const char *q = p->ln + i;
	size_t n = p->lnn - i, k, t;
	int j;
	static const char *raw[] = { "script", "pre", "style", "textarea", 0 };

	if (!n || q[0] != '<')
		return 0;
	for (j = 0; raw[j]; j++) {
		k = strlen(raw[j]);
		if (mk_ci(q + 1, n - 1, raw[j]) &&
		    (n == k + 1 || mk_spt(q[k + 1]) || q[k + 1] == '>'))
			return 1;
	}
	if (mk_ci(q, n, "<!--"))
		return 2;
	if (mk_ci(q, n, "<?"))
		return 3;
	if (n > 2 && q[1] == '!' && ((q[2] | 32) >= 'a' && (q[2] | 32) <= 'z'))
		return 4;
	if (mk_ci(q, n, "<![CDATA["))
		return 5;
	t = q[1] == '/' ? 2 : 1;
	for (j = 0; mk_blocktags[j]; j++) {
		k = strlen(mk_blocktags[j]);
		if (mk_ci(q + t, n - t, mk_blocktags[j])) {
			size_t e = t + k;
			if (e == n || mk_spt(q[e]) || q[e] == '>' ||
			    (q[e] == '/' && e + 1 < n && q[e + 1] == '>'))
				return 6;
		}
	}
	if (inpara)
		return 0;
	k = mk_htmltag(q, n);
	if (k && q[1] != '!' && q[1] != '?') {
		for (j = 0; raw[j]; j++)
			if (mk_ci(q + t, n - t, raw[j]))
				return 0;
		while (k < n && mk_spt(q[k]))
			k++;
		if (k == n)
			return 7;
	}
	return 0;
}

/* Whether a line ends an HTML block of the given kind. */
static int mk_htmlend(const char *q, size_t n, int kind)
{
	static const char *ends[] = { "</script>", "</pre>", "</style>",
				      "</textarea>", 0 };
	size_t i;
	int j;

	switch (kind) {
	case 1:
		for (i = 0; i < n; i++)
			for (j = 0; ends[j]; j++)
				if (mk_ci(q + i, n - i, ends[j]))
					return 1;
		return 0;
	case 2:
		return memmem(q, n, "-->", 3) != 0;
	case 3:
		return memmem(q, n, "?>", 2) != 0;
	case 4:
		return memchr(q, '>', n) != 0;
	case 5:
		return memmem(q, n, "]]>", 3) != 0;
	}
	return 0;
}

/* A list marker at the first non-blank: its length, and what kind it is
   in n. Interrupting a paragraph, it must have content, and a numbered
   one must start at 1. */
static int mk_listmark(mk_p *p, mk_n *n, int inpara)
{
	size_t i = p->fns, j;
	char c = mk_pk(p, i);
	long v = 0;

	if (c == '*' || c == '+' || c == '-') {
		if (i + 1 < p->lnn && !mk_spt(p->ln[i + 1]))
			return 0;
		if (inpara) {
			for (j = i + 1; j < p->lnn && mk_spt(p->ln[j]); j++)
				;
			if (j >= p->lnn)
				return 0;
		}
		n->lt = 0;
		n->bch = c;
		return 1;
	}
	j = i;
	while (j < p->lnn && j - i < 9 && p->ln[j] >= '0' && p->ln[j] <= '9') {
		v = v * 10 + (p->ln[j] - '0');
		j++;
	}
	if (j == i || j - i > 9 || j >= p->lnn)
		return 0;
	if (p->ln[j] != '.' && p->ln[j] != ')')
		return 0;
	if (j + 1 < p->lnn && !mk_spt(p->ln[j + 1]))
		return 0;
	if (inpara) {
		size_t e;
		if (v != 1)
			return 0;
		for (e = j + 1; e < p->lnn && mk_spt(p->ln[e]); e++)
			;
		if (e >= p->lnn)
			return 0;
	}
	n->lt = 1;
	n->dlm = p->ln[j];
	n->start = (int)v;
	return (int)(j - i + 1);
}

/* Split a table row into cells: each one's start and end in the text, in
   a and b; how many. A backslash-escaped pipe is not a boundary. */
static int mk_cells(const char *q, size_t n, size_t *a, size_t *b, int max)
{
	size_t i = 0, s, e;
	int k = 0;

	while (i < n && mk_spt(q[i]))
		i++;
	while (n > i && (mk_spt(q[n - 1]) || q[n - 1] == '\n' || q[n - 1] == '\r'))
		n--;
	if (i < n && q[i] == '|')
		i++;
	if (n > i && q[n - 1] == '|' && !(n >= 2 && q[n - 2] == '\\'))
		n--;
	s = i;
	for (;;) {
		e = s;
		while (e < n && q[e] != '|') {
			if (q[e] == '\\' && e + 1 < n)
				e++;
			e++;
		}
		if (k < max) {
			size_t x = s, y = e;
			while (x < y && mk_spt(q[x]))
				x++;
			while (y > x && mk_spt(q[y - 1]))
				y--;
			a[k] = x;
			b[k] = y;
		}
		k++;
		if (e >= n)
			break;
		s = e + 1;
	}
	return k;
}

/* A table's delimiter row: its cells' alignments into al (l c r or n);
   how many cells, or 0 when it is not one. */
static int mk_delimrow(const char *q, size_t n, str *al)
{
	size_t a[256], b[256];
	int k, i;
	size_t j;

	if (!memchr(q, '-', n))
		return 0;
	if (!memchr(q, '|', n))
		return 0;
	k = mk_cells(q, n, a, b, 256);
	if (k > 256)
		return 0;
	for (i = 0; i < k; i++) {
		int l = 0, r = 0, d = 0;
		j = a[i];
		if (j < b[i] && q[j] == ':') {
			l = 1;
			j++;
		}
		while (j < b[i] && q[j] == '-') {
			d = 1;
			j++;
		}
		if (j < b[i] && q[j] == ':') {
			r = 1;
			j++;
		}
		if (!d || j != b[i])
			return 0;
		s_ch(al, l && r ? 'c' : r ? 'r' : l ? 'l' : 'n');
	}
	return k;
}

/* Add a row to a table: one cell per column, its text and where each of
   its bytes came from, a backslash before a pipe taken out. */
static void mk_addrow(mk_n *tab, const char *q, size_t n, const long *off,
		      int head)
{
	size_t a[256], b[256], j;
	int k, i, cols = (int)tab->info.n;
	mk_n *row = mk_new(MK_TROW), *c;

	row->head = head;
	row->open = 0;
	mk_append(tab, row);
	if (n) {
		row->a = off[0];
		row->b = off[n - 1] + 1;
	}
	k = mk_cells(q, n, a, b, 256);
	for (i = 0; i < cols; i++) {
		c = mk_new(MK_TCELL);
		c->align = tab->info.p[i];
		c->open = 0;
		mk_append(row, c);
		if (i >= k || i >= 256)
			continue;
		for (j = a[i]; j < b[i]; j++) {
			if (q[j] == '\\' && j + 1 < b[i] && q[j + 1] == '|')
				continue;
			mk_addb(c, q + j, 1, off[j]);
		}
	}
}

/* Make a table of a paragraph whose last line is a header row matching
   the delimiter row on this line; whether it was one. Lines before the
   header stay a paragraph. */
static int mk_maketable(mk_p *p, mk_n **cont)
{
	mk_n *para = *cont, *tab;
	str al;
	size_t hs, he, a[256], b[256];
	int k, hk;
	long *lo;
	size_t i;

	if (!p->gfm || para->t != MK_PARA)
		return 0;
	s_init(&al);
	k = mk_delimrow(p->ln + p->fns, p->lnn - p->fns, &al);
	if (!k) {
		s_free(&al);
		return 0;
	}
	he = para->s.n;
	if (he && para->s.p[he - 1] == '\n')
		he--;
	hs = he;
	while (hs > 0 && para->s.p[hs - 1] != '\n')
		hs--;
	hk = mk_cells(para->s.p + hs, he - hs, a, b, 256);
	if (hk != k) {
		s_free(&al);
		return 0;
	}
	tab = mk_new(MK_TABLE);
	tab->sl = p->lineno;
	tab->info = al;
	tab->ca = p->lnoff + (long)p->fns;
	tab->cb = p->lnoff + (long)p->lnn;
	lo = xm((he - hs + 1) * sizeof *lo);
	for (i = hs; i < he; i++)
		lo[i - hs] = para->off[i];
	mk_insafter(para, tab);
	mk_addrow(tab, para->s.p + hs, he - hs, lo, 1);
	free(lo);
	para->s.n = hs;
	if (para->s.p)
		para->s.p[hs] = 0;
	if (hs == 0 || !mk_takerefs(p, para)) {
		mk_unlink(para);
		mk_free(para);
	} else {
		para->open = 0;
	}
	tab->open = 1;
	p->cur = tab;
	*cont = tab;
	p->off = p->lnn;
	return 1;
}

/* Whether an open container goes on with this line, advancing past its
   prefix: 1 it does, 0 it does not, 2 the line closed it (a fence). */
static int mk_continues(mk_p *p, mk_n *n)
{
	mk_fnsp(p);
	switch (n->t) {
	case MK_QUOTE:
		if (p->indent <= 3 && mk_pk(p, p->fns) == '>') {
			mk_adv(p, p->indent + 1, 1);
			if (mk_spt(mk_pk(p, p->off)))
				mk_adv(p, 1, 1);
			return 1;
		}
		return 0;
	case MK_ITEM:
		if (p->indent >= n->moff + n->pad) {
			mk_adv(p, n->moff + n->pad, 1);
			return 1;
		}
		if (p->blank && n->kid) {
			mk_adv(p, (int)(p->fns - p->off), 0);
			return 1;
		}
		return 0;
	case MK_CODE:
		if (n->fence) {
			if (p->indent <= 3 &&
			    mk_fenceclose(p, p->fns, (char)n->fch, n->flen)) {
				return 2;
			}
			{
				int k = n->foff;
				while (k > 0 && mk_spt(mk_pk(p, p->off))) {
					mk_adv(p, 1, 1);
					k--;
				}
			}
			return 1;
		}
		if (p->indent >= 4) {
			mk_adv(p, 4, 1);
			return 1;
		}
		if (p->blank) {
			mk_adv(p, (int)(p->fns - p->off), 0);
			return 1;
		}
		return 0;
	case MK_HTML:
		return !(p->blank && (n->lvl == 6 || n->lvl == 7));
	case MK_PARA:
	case MK_TABLE:
		return !p->blank;
	case MK_HEAD:
	case MK_HR:
		return 0;
	}
	return 1;
}

/* Try each block start in turn, opening what the line begins. */
static mk_n *mk_starts(mk_p *p, mk_n *cont, mk_n **lm, int allm)
{
	int k, len, lazy = p->cur->t == MK_PARA;
	char ch;
	mk_n data;

	while (cont->t != MK_CODE && cont->t != MK_HTML) {
		mk_fnsp(p);
		if (cont->dep >= MK_DEPTH) {
			lg(HIBR_LDBG, "md: nesting past %d, the rest is text", MK_DEPTH);
			break;
		}
		if (p->indent < 4 && mk_pk(p, p->fns) == '>') {
			mk_adv(p, (int)(p->fns + 1 - p->off), 0);
			if (mk_spt(mk_pk(p, p->off)))
				mk_adv(p, 1, 1);
			cont = mk_addchild(p, cont, MK_QUOTE);
		} else if (p->indent < 4 && mk_atx(p, p->fns, &len)) {
			mk_adv(p, (int)(p->fns + len - p->off), 0);
			cont = mk_addchild(p, cont, MK_HEAD);
			cont->lvl = len;
		} else if (p->indent < 4 && (k = mk_fenceopen(p, p->fns, &ch))) {
			cont = mk_addchild(p, cont, MK_CODE);
			cont->fence = 1;
			cont->fch = ch;
			cont->flen = k;
			cont->foff = p->indent;
			mk_adv(p, (int)(p->fns + k - p->off), 0);
		} else if (p->indent < 4 &&
			   (k = mk_htmlstart(p, p->fns, cont->t == MK_PARA))) {
			cont = mk_addchild(p, cont, MK_HTML);
			cont->lvl = k;
		} else if (p->indent < 4 && cont->t == MK_PARA &&
			   (k = mk_setext(p, p->fns)) && mk_takerefs(p, cont)) {
			cont->t = MK_HEAD;
			cont->lvl = k;
			cont->setext = 1;
			p->off = p->lnn;
		} else if (p->indent < 4 && !(cont->t == MK_PARA && !allm) &&
			   mk_hr(p, p->fns)) {
			cont = mk_addchild(p, cont, MK_HR);
			cont->a = p->lnoff + (long)p->fns;
			cont->b = p->lnoff + (long)p->lnn;
			p->off = p->lnn;
		} else if (p->indent < 4 &&
			   (memset(&data, 0, sizeof data),
			    k = mk_listmark(p, &data, cont->t == MK_PARA))) {
			size_t so;
			int sc, sp, i;

			mk_adv(p, (int)(p->fns + k - p->off), 0);
			so = p->off;
			sc = p->col;
			sp = p->ptab;
			while (p->col - sc <= 5 && mk_spt(mk_pk(p, p->off)))
				mk_adv(p, 1, 1);
			i = p->col - sc;
			if (i >= 5 || i < 1 || p->off >= p->lnn) {
				data.pad = k + 1;
				p->off = so;
				p->col = sc;
				p->ptab = sp;
				if (i > 0)
					mk_adv(p, 1, 1);
			} else {
				data.pad = k + i;
			}
			data.moff = p->indent;
			if (cont->t != MK_LIST || cont->lt != data.lt ||
			    (data.lt == 0 && cont->bch != data.bch) ||
			    (data.lt == 1 && cont->dlm != data.dlm)) {
				cont = mk_addchild(p, cont, MK_LIST);
				cont->lt = data.lt;
				cont->bch = data.bch;
				cont->dlm = data.dlm;
				cont->start = data.start;
			}
			cont = mk_addchild(p, cont, MK_ITEM);
			cont->lt = data.lt;
			cont->bch = data.bch;
			cont->dlm = data.dlm;
			cont->moff = data.moff;
			cont->pad = data.pad;
			cont->a = p->lnoff + (long)p->fns;
		} else if (p->indent >= 4 && !lazy && !p->blank) {
			mk_adv(p, 4, 1);
			cont = mk_addchild(p, cont, MK_CODE);
		} else if (p->indent < 4 && cont->t == MK_PARA && allm &&
			   mk_maketable(p, &cont)) {
			*lm = cont;
			break;
		} else {
			break;
		}
		if (mk_takeslines(cont->t))
			break;
		lazy = 0;
	}
	return cont;
}

/* One line of the document. */
static void mk_line(mk_p *p)
{
	mk_n *cont = p->root, *last, *lm, *n;
	int allm = 1, r;

	p->off = 0;
	p->col = 0;
	p->ptab = 0;
	p->blank = 0;
	while (cont->last && cont->last->open) {
		last = cont->last;
		r = mk_continues(p, last);
		if (r == 2) {
			p->cur = mk_finalize(p, last);
			return;
		}
		if (!r) {
			allm = 0;
			break;
		}
		cont = last;
	}
	lm = cont;
	cont = mk_starts(p, cont, &lm, allm);
	mk_fnsp(p);
	if (p->blank && cont->last)
		cont->last->lb = 1;
	cont->lb = p->blank && cont->t != MK_QUOTE && cont->t != MK_HEAD &&
		   cont->t != MK_HR && !(cont->t == MK_CODE && cont->fence) &&
		   !(cont->t == MK_ITEM && !cont->kid && cont->sl == p->lineno);
	for (n = cont; n->up; n = n->up)
		n->up->lb = 0;
	if (p->cur != lm && cont == lm && !p->blank && p->cur->t == MK_PARA) {
		mk_addline(p, p->cur);
		return;
	}
	while (p->cur != lm)
		p->cur = mk_finalize(p, p->cur);
	if (cont->t == MK_TABLE) {
		if (p->off < p->lnn) {
			long *lo = xm((p->lnn - p->off + 1) * sizeof *lo);
			size_t i;
			for (i = p->off; i < p->lnn; i++)
				lo[i - p->off] = p->lnoff + (long)i;
			mk_addrow(cont, p->ln + p->off, p->lnn - p->off, lo, 0);
			free(lo);
		}
	} else if (cont->t == MK_CODE) {
		mk_addline(p, cont);
	} else if (cont->t == MK_HTML) {
		size_t o = p->off;
		mk_addline(p, cont);
		if (cont->lvl >= 1 && cont->lvl <= 5 &&
		    mk_htmlend(p->ln + o, p->lnn - o, cont->lvl))
			cont = mk_finalize(p, cont);
	} else if (p->blank) {
	} else if (cont->t == MK_HEAD && !cont->setext) {
		size_t e = p->lnn, s;
		mk_adv(p, (int)(p->fns - p->off), 0);
		while (e > p->off && mk_spt(p->ln[e - 1]))
			e--;
		s = e;
		while (s > p->off && p->ln[s - 1] == '#')
			s--;
		if (s < e && (s == p->off || mk_spt(p->ln[s - 1])))
			e = s;
		while (e > p->off && mk_spt(p->ln[e - 1]))
			e--;
		if (e > p->off)
			mk_addb(cont, p->ln + p->off, e - p->off,
				p->lnoff + (long)p->off);
	} else if (mk_takeslines(cont->t)) {
		mk_adv(p, (int)(p->fns - p->off), 0);
		mk_addline(p, cont);
	} else {
		cont = mk_addchild(p, cont, MK_PARA);
		mk_adv(p, (int)(p->fns - p->off), 0);
		mk_addline(p, cont);
	}
	p->cur = cont;
}

/* Parse each leaf's inlines, depth first. */
static void mk_walk(mk_n *n, vec *refs, int gfm)
{
	mk_n *k;

	if (n->t == MK_PARA || n->t == MK_HEAD || n->t == MK_TCELL) {
		mk_inlines(n, refs, gfm);
		return;
	}
	for (k = n->kid; k; k = k->nx)
		mk_walk(k, refs, gfm);
}

/* Parse a whole document into blocks, then each leaf's inlines. */
mk_n *mk_parse(const char *src, size_t n, int gfm, vec *refs)
{
	mk_p p;
	size_t i = 0, e;

	memset(&p, 0, sizeof p);
	p.root = mk_new(MK_DOC);
	p.cur = p.root;
	p.src = src;
	p.srcn = n;
	p.gfm = gfm;
	while (i < n) {
		e = i;
		while (e < n && src[e] != '\n' && src[e] != '\r')
			e++;
		p.ln = src + i;
		p.lnn = e - i;
		p.lnoff = (long)i;
		mk_line(&p);
		p.lineno++;
		if (e < n && src[e] == '\r' && e + 1 < n && src[e + 1] == '\n')
			e++;
		i = e + 1;
	}
	while (p.cur && p.cur != p.root)
		p.cur = mk_finalize(&p, p.cur);
	p.root->open = 0;
	mk_walk(p.root, &p.refs, gfm);
	*refs = p.refs;
	return p.root;
}
