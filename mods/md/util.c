#define _GNU_SOURCE

#include "mk.h"
#include <stdlib.h>
#include <string.h>

/* A node of type t, zeroed. */
mk_n *mk_new(int t)
{
	mk_n *n = xm(sizeof *n);

	memset(n, 0, sizeof *n);
	n->t = t;
	n->open = 1;
	n->task = -1;
	n->start = 1;
	n->tight = 1;
	s_init(&n->s);
	s_init(&n->info);
	s_init(&n->url);
	s_init(&n->title);
	return n;
}

/* Free a node and everything below it. */
void mk_free(mk_n *n)
{
	mk_n *k, *nx;

	if (!n)
		return;
	for (k = n->kid; k; k = nx) {
		nx = k->nx;
		mk_free(k);
	}
	s_free(&n->s);
	s_free(&n->info);
	s_free(&n->url);
	s_free(&n->title);
	free(n->off);
	free(n);
}

/* Put n last among up's children. */
void mk_append(mk_n *up, mk_n *n)
{
	n->up = up;
	n->nx = 0;
	n->pv = up->last;
	if (up->last)
		up->last->nx = n;
	else
		up->kid = n;
	up->last = n;
}

/* Take n out of the tree, keeping it. */
void mk_unlink(mk_n *n)
{
	if (n->pv)
		n->pv->nx = n->nx;
	else if (n->up)
		n->up->kid = n->nx;
	if (n->nx)
		n->nx->pv = n->pv;
	else if (n->up)
		n->up->last = n->pv;
	n->up = n->nx = n->pv = 0;
}

/* Put n just after at. */
void mk_insafter(mk_n *at, mk_n *n)
{
	n->up = at->up;
	n->pv = at;
	n->nx = at->nx;
	if (at->nx)
		at->nx->pv = n;
	else if (at->up)
		at->up->last = n;
	at->nx = n;
}

/* Add bytes to a leaf's content, each remembering the source offset it
   came from: at, at+1, ... -- or at for every one when at is negative,
   as -at-1, for text that stands for a single place (a tab's spaces). */
void mk_addb(mk_n *n, const char *p, size_t len, long at)
{
	size_t i;

	if (n->s.n + len + 1 > n->offcap) {
		n->offcap = (n->s.n + len + 1) * 2 + 16;
		n->off = xr(n->off, n->offcap * sizeof *n->off);
	}
	for (i = 0; i < len; i++)
		n->off[n->s.n + i] = at < 0 ? -at - 1 : at + (long)i;
	s_add(&n->s, p, len);
}

/* --- Unicode ------------------------------------------------------------ */

/* Decode one UTF-8 character; a bad byte is itself, one long. */
unsigned mk_utf8(const char *s, size_t n, int *len)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned c;
	int k, i;

	*len = 1;
	if (!n)
		return 0;
	c = p[0];
	if (c < 0x80)
		return c;
	if ((c & 0xE0) == 0xC0) {
		k = 2;
		c &= 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		k = 3;
		c &= 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		k = 4;
		c &= 0x07;
	} else {
		return c;
	}
	if ((size_t)k > n)
		return p[0];
	for (i = 1; i < k; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return p[0];
		c = (c << 6) | (p[i] & 0x3F);
	}
	*len = k;
	return c;
}

/* The character ending just before byte i, or a newline at the start. */
unsigned mk_before(const char *s, size_t i)
{
	size_t j = i;
	int l;

	if (!i)
		return '\n';
	j--;
	while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80 && i - j < 4)
		j--;
	return mk_utf8(s + j, i - j, &l);
}

/* Unicode whitespace: Zs, and tab, line feed, form feed, return. */
int mk_isspace(unsigned c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r' ||
	       c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
	       c == 0x202F || c == 0x205F || c == 0x3000;
}

/* Unicode punctuation and symbols (the P and S categories), as ranges
   close enough for emphasis to be decided as the spec decides it. */
int mk_ispunct(unsigned c)
{
	if (c < 0x80)
		return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
		       (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
	if ((c >= 0xA1 && c <= 0xBF) || c == 0xD7 || c == 0xF7)
		return 1;
	if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E))
		return 1;
	if ((c >= 0x20A0 && c <= 0x20C0) || (c >= 0x2100 && c <= 0x214F &&
	     c != 0x2102 && c != 0x2107 && !(c >= 0x210A && c <= 0x2113) &&
	     c != 0x2115 && !(c >= 0x2119 && c <= 0x211D) && c != 0x2124 &&
	     c != 0x2126 && c != 0x2128 && !(c >= 0x212A && c <= 0x212D) &&
	     !(c >= 0x212F && c <= 0x2139)))
		return 1;
	if ((c >= 0x2190 && c <= 0x2BFF) || (c >= 0x2E00 && c <= 0x2E7F))
		return 1;
	if ((c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3020) ||
	    c == 0x3030 || c == 0x30FB)
		return 1;
	if ((c >= 0xFE10 && c <= 0xFE19) || (c >= 0xFE30 && c <= 0xFE6B) ||
	    (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
	    (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65))
		return 1;
	if (c >= 0x1F300 && c <= 0x1FAFF)
		return 1;
	return 0;
}

/* Append a character as UTF-8. */
void mk_putu(str *o, unsigned c)
{
	if (c < 0x80) {
		s_ch(o, (int)c);
	} else if (c < 0x800) {
		s_ch(o, (int)(0xC0 | (c >> 6)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		s_ch(o, (int)(0xE0 | (c >> 12)));
		s_ch(o, (int)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	} else {
		s_ch(o, (int)(0xF0 | (c >> 18)));
		s_ch(o, (int)(0x80 | ((c >> 12) & 0x3F)));
		s_ch(o, (int)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	}
}

/* A character folded for comparing link labels: lower case, and the
   sharp s, capital or small, as ss -- the cases the spec's labels need. */
unsigned mk_lower(unsigned c, str *o)
{
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	if (c == 0xDF || c == 0x1E9E) {
		s_cat(o, "ss");
		return 0;
	}
	if ((c >= 0xC0 && c <= 0xDE && c != 0xD7))
		return c + 32;
	if (c >= 0x100 && c <= 0x17F && !(c & 1) && c != 0x130 && c != 0x138)
		return c + 1;
	if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
		return c + 32;
	if (c >= 0x410 && c <= 0x42F)
		return c + 32;
	if (c >= 0x400 && c <= 0x40F)
		return c + 80;
	return c;
}

/* A link label as it is compared: folded, whitespace runs one space,
   trimmed. */
void mk_fold(str *o, const char *p, size_t n)
{
	size_t i = 0;
	int l, sp = 0;
	unsigned c;

	while (i < n) {
		c = mk_utf8(p + i, n - i, &l);
		i += (size_t)l;
		if (mk_isspace(c)) {
			sp = 1;
			continue;
		}
		if (sp && o->n)
			s_ch(o, ' ');
		sp = 0;
		c = mk_lower(c, o);
		if (c)
			mk_putu(o, c);
	}
}

/* --- entities, escapes, and escaping for HTML ---------------------------- */

static int mk_entcmp(const void *k, const void *e)
{
	return strcmp((const char *)k, ((const mk_ent *)e)->nm);
}

/* The characters a named entity stands for, or 0. */
const char *mk_entity(const char *nm, size_t n)
{
	char b[40];
	const mk_ent *e;

	if (n == 0 || n >= sizeof b)
		return 0;
	memcpy(b, nm, n);
	b[n] = 0;
	e = bsearch(b, mk_ents, mk_nents, sizeof *mk_ents, mk_entcmp);
	return e ? e->ch : 0;
}

/* An entity or numeric reference at p: how long it is, its characters
   appended to o; 0 when it is not one. */
size_t mk_entref(const char *p, size_t n, str *o)
{
	size_t i;
	unsigned long v = 0;
	const char *e;

	if (n < 3 || p[0] != '&')
		return 0;
	if (p[1] == '#') {
		i = 2;
		if (i < n && (p[i] == 'x' || p[i] == 'X')) {
			i++;
			while (i < n && i < 9 && ((p[i] >= '0' && p[i] <= '9') ||
			       ((p[i] | 32) >= 'a' && (p[i] | 32) <= 'f'))) {
				v = v * 16 + (unsigned long)(p[i] <= '9' ? p[i] - '0' :
							      (p[i] | 32) - 'a' + 10);
				i++;
			}
			if (i == 3 || i >= n || p[i] != ';')
				return 0;
		} else {
			while (i < n && i < 9 && p[i] >= '0' && p[i] <= '9') {
				v = v * 10 + (unsigned long)(p[i] - '0');
				i++;
			}
			if (i == 2 || i >= n || p[i] != ';')
				return 0;
		}
		if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
			v = 0xFFFD;
		mk_putu(o, (unsigned)v);
		return i + 1;
	}
	for (i = 1; i < n && i < 40 && (((p[i] | 32) >= 'a' && (p[i] | 32) <= 'z') ||
					 (p[i] >= '0' && p[i] <= '9')); i++)
		;
	if (i == 1 || i >= n || p[i] != ';')
		return 0;
	e = mk_entity(p + 1, i - 1);
	if (!e)
		return 0;
	s_cat(o, e);
	return i + 1;
}

/* Text with its backslash escapes and entities resolved -- for a link's
   destination, its title, and a code block's info string. */
void mk_unesc(str *o, const char *p, size_t n)
{
	size_t i = 0, k;

	while (i < n) {
		if (p[i] == '\\' && i + 1 < n && mk_ispunct((unsigned char)p[i + 1]) &&
		    (unsigned char)p[i + 1] < 0x80) {
			s_ch(o, p[i + 1]);
			i += 2;
			continue;
		}
		if (p[i] == '&' && (k = mk_entref(p + i, n - i, o))) {
			i += k;
			continue;
		}
		s_ch(o, p[i]);
		i++;
	}
}

/* Escape text for HTML. */
void mk_esc(str *o, const char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		switch (p[i]) {
		case '&': s_cat(o, "&amp;"); break;
		case '<': s_cat(o, "&lt;"); break;
		case '>': s_cat(o, "&gt;"); break;
		case '"': s_cat(o, "&quot;"); break;
		default: s_ch(o, p[i]);
		}
	}
}

/* Escape a URL for an href or src, as cmark does: what is safe in a URL
   stays, & and ' become references, and anything else is percent-encoded. */
void mk_eschref(str *o, const char *p, size_t n)
{
	static const char safe[] = "-_.+!*(),%#@?=;:/$~";
	static const char hex[] = "0123456789ABCDEF";
	size_t i;
	unsigned char c;

	for (i = 0; i < n; i++) {
		c = (unsigned char)p[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || (c && strchr(safe, c))) {
			s_ch(o, c);
		} else if (c == '&') {
			s_cat(o, "&amp;");
		} else if (c == '\'') {
			s_cat(o, "&#x27;");
		} else {
			s_ch(o, '%');
			s_ch(o, hex[c >> 4]);
			s_ch(o, hex[c & 15]);
		}
	}
}

/* --- raw HTML ------------------------------------------------------------ */

static int mk_isws(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int mk_isal(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int mk_isdg(char c)
{
	return c >= '0' && c <= '9';
}

/* An attribute at p, after its leading whitespace: its length, or 0. */
static size_t mk_attr(const char *p, size_t n)
{
	size_t i = 0, j;

	if (i >= n || !(mk_isal(p[i]) || p[i] == '_' || p[i] == ':'))
		return 0;
	i++;
	while (i < n && (mk_isal(p[i]) || mk_isdg(p[i]) || p[i] == '_' ||
			 p[i] == '.' || p[i] == ':' || p[i] == '-'))
		i++;
	j = i;
	while (j < n && mk_isws(p[j]))
		j++;
	if (j < n && p[j] == '=') {
		j++;
		while (j < n && mk_isws(p[j]))
			j++;
		if (j >= n)
			return 0;
		if (p[j] == '"' || p[j] == '\'') {
			char q = p[j++];
			while (j < n && p[j] != q)
				j++;
			if (j >= n)
				return 0;
			return j + 1;
		}
		if (strchr(" \t\n\r\f\"'=<>`", p[j]))
			return 0;
		while (j < n && !strchr(" \t\n\r\f\"'=<>`", p[j]))
			j++;
		return j;
	}
	return i;
}

/* An HTML tag, comment, processing instruction, declaration or CDATA
   section at p, which starts with <: its length, or 0. */
size_t mk_htmltag(const char *p, size_t n)
{
	size_t i, k;
	const char *e;

	if (n < 3 || p[0] != '<')
		return 0;
	if (mk_isal(p[1])) {
		i = 2;
		while (i < n && (mk_isal(p[i]) || mk_isdg(p[i]) || p[i] == '-'))
			i++;
		for (;;) {
			size_t a;

			k = i;
			while (k < n && mk_isws(p[k]))
				k++;
			if (k == i || !(a = mk_attr(p + k, n - k)))
				break;
			i = k + a;
		}
		while (i < n && mk_isws(p[i]))
			i++;
		if (i < n && p[i] == '/')
			i++;
		if (i < n && p[i] == '>')
			return i + 1;
		return 0;
	}
	if (p[1] == '/') {
		if (!mk_isal(p[2]))
			return 0;
		i = 3;
		while (i < n && (mk_isal(p[i]) || mk_isdg(p[i]) || p[i] == '-'))
			i++;
		while (i < n && mk_isws(p[i]))
			i++;
		return i < n && p[i] == '>' ? i + 1 : 0;
	}
	if (n >= 4 && !strncmp(p, "<!--", 4)) {
		if (n >= 5 && p[4] == '>')
			return 5;
		if (n >= 6 && p[4] == '-' && p[5] == '>')
			return 6;
		e = memmem(p + 4, n - 4, "-->", 3);
		return e ? (size_t)(e - p) + 3 : 0;
	}
	if (p[1] == '?') {
		e = memmem(p + 2, n - 2, "?>", 2);
		return e ? (size_t)(e - p) + 2 : 0;
	}
	if (n >= 9 && !strncmp(p, "<![CDATA[", 9)) {
		e = memmem(p + 9, n - 9, "]]>", 3);
		return e ? (size_t)(e - p) + 3 : 0;
	}
	if (p[1] == '!' && mk_isal(p[2])) {
		e = memchr(p + 2, '>', n - 2);
		return e ? (size_t)(e - p) + 1 : 0;
	}
	return 0;
}

/* --- links ---------------------------------------------------------------- */

/* A link label at p, starting with [: its length including both brackets,
   or 0 -- at most 999 characters inside, no unescaped bracket, and not
   only whitespace. */
size_t mk_label(const char *p, size_t n)
{
	size_t i = 1;
	int any = 0;

	if (!n || p[0] != '[')
		return 0;
	while (i < n && i <= 1000) {
		if (p[i] == '\\' && i + 1 < n) {
			any = 1;
			i += 2;
			continue;
		}
		if (p[i] == '[')
			return 0;
		if (p[i] == ']')
			return any ? i + 1 : 0;
		if (!mk_isws(p[i]))
			any = 1;
		i++;
	}
	return 0;
}

/* A link destination at p: <...>, or a run with balanced parentheses and
   no space or control character. Its length, the destination unescaped
   into url; 0 when there is none (an empty <> is one). */
size_t mk_linkdest(const char *p, size_t n, str *url)
{
	size_t i = 0;
	int depth = 0;

	if (n && p[0] == '<') {
		i = 1;
		while (i < n && p[i] != '>' && p[i] != '<' && p[i] != '\n') {
			if (p[i] == '\\' && i + 1 < n)
				i++;
			i++;
		}
		if (i >= n || p[i] != '>')
			return 0;
		mk_unesc(url, p + 1, i - 1);
		return i + 1;
	}
	while (i < n && (unsigned char)p[i] > 0x20 && p[i] != 0x7F) {
		if (p[i] == '\\' && i + 1 < n && mk_ispunct((unsigned char)p[i + 1])) {
			i += 2;
			continue;
		}
		if (p[i] == '(') {
			depth++;
			if (depth > 32)
				return 0;
		} else if (p[i] == ')') {
			if (!depth)
				break;
			depth--;
		}
		i++;
	}
	if (!i || depth)
		return 0;
	mk_unesc(url, p, i);
	return i;
}

/* A link title at p: "...", '...' or (...). Its length, unescaped into
   title, or 0. */
size_t mk_linktitle(const char *p, size_t n, str *title)
{
	size_t i = 1;
	char close;

	if (!n)
		return 0;
	if (p[0] == '"' || p[0] == '\'')
		close = p[0];
	else if (p[0] == '(')
		close = ')';
	else
		return 0;
	while (i < n && p[i] != close) {
		if (p[i] == '\\' && i + 1 < n) {
			i += 2;
			continue;
		}
		if (close == ')' && p[i] == '(')
			return 0;
		i++;
	}
	if (i >= n)
		return 0;
	mk_unesc(title, p + 1, i - 1);
	return i + 1;
}

/* A label's hash, for the definitions' table. */
static size_t mk_rh(const char *k)
{
	size_t h = 2166136261u;

	while (*k)
		h = (h ^ (unsigned char)*k++) * 16777619u;
	return h;
}

/* The definition kept under a folded label, or 0. The table is open
   addressed in refs' own slots, cap a power of two. */
static mk_ref *mk_rget(vec *refs, const char *k)
{
	size_t i;
	mk_ref *r;

	if (!refs->cap)
		return 0;
	for (i = mk_rh(k) & (refs->cap - 1); (r = refs->p[i]);
	     i = (i + 1) & (refs->cap - 1))
		if (!strcmp(r->lab, k))
			return r;
	return 0;
}

/* Keep a definition in the table, growing it at half full. */
static void mk_rput(vec *refs, mk_ref *r)
{
	vec old = *refs;
	size_t i;

	if ((refs->n + 1) * 2 > refs->cap) {
		refs->cap = refs->cap ? refs->cap * 2 : 16;
		refs->p = xm(refs->cap * sizeof *refs->p);
		memset(refs->p, 0, refs->cap * sizeof *refs->p);
		refs->n = 0;
		for (i = 0; i < old.cap; i++)
			if (old.p[i])
				mk_rput(refs, old.p[i]);
		free(old.p);
	}
	for (i = mk_rh(r->lab) & (refs->cap - 1); refs->p[i];
	     i = (i + 1) & (refs->cap - 1))
		;
	refs->p[i] = r;
	refs->n++;
}

/* The definition a label names, or 0. */
mk_ref *mk_findref(vec *refs, const char *lab, size_t n)
{
	str k;
	mk_ref *r = 0;

	s_init(&k);
	mk_fold(&k, lab, n);
	if (k.n)
		r = mk_rget(refs, k.p);
	s_free(&k);
	return r;
}

/* A link reference definition at the start of p -- [label]: destination
   "title" -- kept in refs unless the label is defined already. Its length
   to the end of its last line, or 0 when there is none. */
size_t mk_refdef(const char *p, size_t n, vec *refs, mk_n *leaf, long base)
{
	size_t i, l, k, save, nl;
	str url, title, key;
	mk_ref *r;
	int ok;

	(void)leaf;
	(void)base;
	l = mk_label(p, n);
	if (!l || l >= n || p[l] != ':')
		return 0;
	i = l + 1;
	nl = 0;
	while (i < n && mk_isws(p[i])) {
		if (p[i] == '\n' && ++nl > 1)
			return 0;
		i++;
	}
	s_init(&url);
	s_init(&title);
	if (i < n && p[i] == '<' && i + 1 < n && p[i + 1] == '>') {
		k = 2;
	} else {
		k = mk_linkdest(p + i, n - i, &url);
		if (!k) {
			s_free(&url);
			return 0;
		}
	}
	i += k;
	save = i;
	nl = 0;
	k = i;
	while (k < n && mk_isws(p[k])) {
		if (p[k] == '\n')
			nl++;
		k++;
	}
	ok = 0;
	if (k > i && nl <= 1 && k < n && (l = mk_linktitle(p + k, n - k, &title))) {
		size_t e = k + l;
		while (e < n && (p[e] == ' ' || p[e] == '\t'))
			e++;
		if (e >= n || p[e] == '\n') {
			i = e < n ? e + 1 : e;
			ok = 1;
		} else {
			title.n = 0;
		}
	}
	if (!ok) {
		i = save;
		while (i < n && (p[i] == ' ' || p[i] == '\t'))
			i++;
		if (i < n && p[i] != '\n') {
			s_free(&url);
			s_free(&title);
			return 0;
		}
		if (i < n)
			i++;
	}
	s_init(&key);
	l = mk_label(p, n);
	mk_fold(&key, p + 1, l - 2);
	if (key.n && !mk_rget(refs, key.p)) {
		r = xm(sizeof *r);
		r->lab = xs(key.p ? key.p : "");
		r->url = xs(url.p ? url.p : "");
		r->title = title.p ? xs(title.p) : 0;
		mk_rput(refs, r);
	}
	s_free(&key);
	s_free(&url);
	s_free(&title);
	return i;
}
