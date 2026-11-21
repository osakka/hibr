#define _GNU_SOURCE

#include "ct.h"
#include "../highlight.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CT_DIM "\033[38;5;244m"
#define CT_BAD "\033[38;5;203;1m"
#define CT_STR "\033[38;5;71m"
#define CT_NUMC "\033[38;5;173m"
#define CT_KEY "\033[38;5;110m"
#define CT_CMT "\033[38;5;245;3m"
#define CT_FN "\033[38;5;176m"
#define CT_HEAD "\033[1m"
#define CT_OFF "\033[0m"

static const char *ct_c_kw[] = {
	"auto", "break", "case", "char", "const", "continue", "default", "do",
	"double", "else", "enum", "extern", "float", "for", "goto", "if",
	"int", "long", "return", "short", "signed", "sizeof", "static",
	"struct", "switch", "typedef", "union", "unsigned", "void", "while", 0
};

static const char *ct_sh_kw[] = {
	"case", "do", "done", "elif", "else", "esac", "fi", "fn", "for",
	"function", "if", "in", "local", "return", "select", "then", "until",
	"while", "export", "readonly", "declare", "ret", "fail", "try", 0
};

static const char *ct_py_kw[] = {
	"and", "as", "assert", "async", "await", "break", "class", "continue",
	"def", "del", "elif", "else", "except", "finally", "for", "from",
	"global", "if", "import", "in", "is", "lambda", "none", "nonlocal",
	"not", "or", "pass", "raise", "return", "true", "false", "try",
	"while", "with", "yield", 0
};

/* hibr's own words: the reserved ones, then the builtins that carry the
   language rather than merely being commands. */
static const char *ct_hibr_kw[] = {
	"case", "do", "done", "elif", "else", "esac", "fi", "fn", "for", "if",
	"in", "select", "then", "until", "while", "function",
	"local", "return", "ret", "fail", "try", "export", "readonly",
	"declare", "unset", "shift", "exit", "eval", "exec", "source",
	"opt", "args", "title", "match", "rsub", "str", "arr", "json",
	"mod", "listen", "coproc", "dial", "drop", "hash", "mapfile",
	"shopt", "set", "trap", "ulimit", "read", "printf", "echo",
	"test", "let", "command", "builtin", "type", "getopts", 0
};

static const char *ct_json_kw[] = { "true", "false", "null", 0 };

/* Name the language a file is in, from its extension, or null. */
const char *ct_lang(const char *nm)
{
	const char *d;

	if (!nm)
		return 0;
	d = strrchr(nm, '.');
	if (!d) {
		d = strrchr(nm, '/');
		d = d ? d + 1 : nm;
		if (!strcmp(d, "Makefile") || !strcmp(d, "makefile"))
			return "mk";
		if (!strcmp(d, ".hibrc") || !strcmp(d, "hibrc"))
			return "hibr";
		return 0;
	}
	d++;
	if (!strcmp(d, "c") || !strcmp(d, "h"))
		return "c";
	if (!strcmp(d, "hibr") || !strcmp(d, "hibrc") || !strcmp(d, "t"))
		return "hibr";
	if (!strcmp(d, "sh") || !strcmp(d, "bash"))
		return "sh";
	if (!strcmp(d, "py"))
		return "py";
	if (!strcmp(d, "json"))
		return "json";
	if (!strcmp(d, "md"))
		return "md";
	if (!strcmp(d, "mk"))
		return "mk";
	return 0;
}

/* The keyword table for a language, or null when it has none. */
const char **ct_kw(const char *lang)
{
	if (!lang)
		return 0;
	if (!strcmp(lang, "c"))
		return ct_c_kw;
	if (!strcmp(lang, "hibr"))
		return ct_hibr_kw;
	if (!strcmp(lang, "sh") || !strcmp(lang, "mk"))
		return ct_sh_kw;
	if (!strcmp(lang, "py"))
		return ct_py_kw;
	if (!strcmp(lang, "json"))
		return ct_json_kw;
	return 0;
}

/* The line-comment introducer for a language, or null. */
const char *ct_cmt(const char *lang)
{
	if (!lang)
		return 0;
	if (!strcmp(lang, "sh") || !strcmp(lang, "py") || !strcmp(lang, "mk") ||
	    !strcmp(lang, "hibr"))
		return "#";
	if (!strcmp(lang, "c"))
		return "//";
	return 0;
}

/* True when a word sits in the keyword table. */
int ct_iskw(const char **t, const char *w, size_t n)
{
	int i;

	for (i = 0; t && t[i]; i++)
		if (!strncmp(t[i], w, n) && !t[i][n])
			return 1;
	return 0;
}

/* Whether a UTF-8 sequence starts here, and how many bytes it takes. */
int ct_u8ok(const char *p, size_t n, int *len)
{
	unsigned char c = (unsigned char)p[0];
	int need, i;
	unsigned cp;

	*len = 1;
	if (c < 0x80)
		return 1;
	if (c >= 0xC2 && c <= 0xDF) {
		need = 2;
		cp = c & 0x1F;
	} else if (c >= 0xE0 && c <= 0xEF) {
		need = 3;
		cp = c & 0x0F;
	} else if (c >= 0xF0 && c <= 0xF4) {
		need = 4;
		cp = c & 0x07;
	} else {
		return 0;
	}
	if ((size_t)need > n)
		return 0;
	for (i = 1; i < need; i++) {
		if (((unsigned char)p[i] & 0xC0) != 0x80)
			return 0;
		cp = (cp << 6) | ((unsigned char)p[i] & 0x3F);
	}
	if (need == 3 && cp < 0x800)
		return 0;
	if (need == 4 && (cp < 0x10000 || cp > 0x10FFFF))
		return 0;
	if (cp >= 0xD800 && cp <= 0xDFFF)
		return 0;
	*len = need;
	return 1;
}

/* Append a byte as two hex digits, for one that is not text at all. */
void ct_hex(str *out, unsigned char c)
{
	static const char *d = "0123456789abcdef";

	s_cat(out, CT_BAD);
	s_ch(out, '<');
	s_ch(out, d[c >> 4]);
	s_ch(out, d[c & 15]);
	s_ch(out, '>');
	s_cat(out, CT_OFF);
}

/* Append one character, keeping the column count the file would have. */
int ct_put(str *out, const char *p, size_t n, ct_opt *o)
{
	unsigned char c = (unsigned char)p[0];
	int len = 1, k;

	if (c == '\t') {
		if (o->f & CT_TABS) {
			s_cat(out, CT_DIM);
			s_cat(out, "^I");
			s_cat(out, CT_OFF);
			o->col += 2;
			return 1;
		}
		k = o->tabw - (o->col % o->tabw);
		while (k--)
			s_ch(out, ' ');
		o->col += o->tabw - (o->col % o->tabw);
		return 1;
	}
	if (c < 32 || c == 127) {
		s_cat(out, CT_DIM);
		s_ch(out, '^');
		s_ch(out, c == 127 ? '?' : (char)(c + 64));
		s_cat(out, CT_OFF);
		o->col += 2;
		return 1;
	}
	if (c < 0x80) {
		s_ch(out, (char)c);
		o->col++;
		return 1;
	}
	if (!ct_u8ok(p, n, &len)) {
		ct_hex(out, c);
		o->col += 4;
		return 1;
	}
	s_add(out, p, (size_t)len);
	o->col++;
	return len;
}

/* Append a run of bytes through ct_put, one character at a time. */
void ct_run(str *out, const char *p, size_t n, ct_opt *o)
{
	size_t i = 0;

	while (i < n)
		i += (size_t)ct_put(out, p + i, n - i, o);
}

/* The block a C, Python or Markdown line left open, or none. */
int ct_blk(const ct_opt *o)
{
	return o->hs.n ? (unsigned char)o->hs.p[0] : CT_BLK_NONE;
}

/* Say which block a C, Python or Markdown line leaves open. */
void ct_setblk(ct_opt *o, int b)
{
	o->hs.n = 0;
	if (b != CT_BLK_NONE)
		s_ch(&o->hs, b);
	else if (o->hs.p)
		o->hs.p[0] = 0;
}

/* The text that closes the block we are inside, or null. */
const char *ct_close(int blk)
{
	switch (blk) {
	case CT_BLK_CMT:
		return "*/";
	case CT_BLK_DQ:
		return "\"\"\"";
	case CT_BLK_SQ:
		return "'''";
	}
	return 0;
}

/* Colour a markdown line by what it opens with. */
void ct_md(str *out, const char *p, size_t n, ct_opt *o)
{
	size_t i = 0;
	const char *col = 0;

	if (n >= 3 && !strncmp(p, "```", 3)) {
		ct_setblk(o, ct_blk(o) == CT_BLK_FENCE ? CT_BLK_NONE : CT_BLK_FENCE);
		s_cat(out, CT_CMT);
		ct_run(out, p, n, o);
		s_cat(out, CT_OFF);
		return;
	}
	if (ct_blk(o) == CT_BLK_FENCE) {
		s_cat(out, CT_STR);
		ct_run(out, p, n, o);
		s_cat(out, CT_OFF);
		return;
	}
	while (i < n && (p[i] == ' ' || p[i] == '\t'))
		i++;
	if (i < n && p[i] == '#')
		col = CT_HEAD;
	else if (i < n && (p[i] == '-' || p[i] == '*' || p[i] == '>') &&
		 i + 1 < n && p[i + 1] == ' ')
		col = CT_KEY;
	if (col)
		s_cat(out, col);
	ct_run(out, p, n, o);
	if (col)
		s_cat(out, CT_OFF);
}

/* Finish a block that began on an earlier line, and say where it ended. */
size_t ct_resume(str *out, const char *p, size_t n, ct_opt *o)
{
	const char *cl = ct_close(ct_blk(o));
	size_t i, cn = strlen(cl);
	const char *col = ct_blk(o) == CT_BLK_CMT ? CT_CMT : CT_STR;

	s_cat(out, col);
	for (i = 0; i < n; i++) {
		if (i + cn <= n && !strncmp(p + i, cl, cn)) {
			ct_run(out, p + i, cn, o);
			s_cat(out, CT_OFF);
			ct_setblk(o, CT_BLK_NONE);
			return i + cn;
		}
		i += (size_t)ct_put(out, p + i, n - i, o) - 1;
	}
	s_cat(out, CT_OFF);
	return n;
}

/* Whether a language is read as a shell, its strings and heredocs running across lines. */
int ct_isshell(const char *lang)
{
	return lang && (!strcmp(lang, "hibr") || !strcmp(lang, "sh"));
}

/* How many shell contexts are open: the stack ends where the heredocs begin. */
size_t ct_depth(const str *hs)
{
	const char *e = hs->n ? memchr(hs->p, '\n', hs->n) : 0;

	return e ? (size_t)(e - hs->p) : hs->n;
}

/* The innermost open shell context, or 0 at the top level. */
int ct_top(const str *hs)
{
	size_t k = ct_depth(hs);

	return k ? (unsigned char)hs->p[k - 1] : 0;
}

/* Open a shell context inside the innermost one. */
void ct_push(str *hs, int c)
{
	size_t k = ct_depth(hs);

	s_ch(hs, 0);
	memmove(hs->p + k + 1, hs->p + k, hs->n - 1 - k);
	hs->p[k] = (char)c;
}

/* Close the innermost shell context. */
void ct_pop(str *hs)
{
	size_t k = ct_depth(hs);

	if (!k)
		return;
	memmove(hs->p + k - 1, hs->p + k, hs->n - k);
	hs->n--;
	hs->p[hs->n] = 0;
}

/* Change colour only when it differs, so a run is one escape and one reset. */
void ct_pen(str *out, const char **cur, const char *col)
{
	if (*cur == col || (*cur && col && !strcmp(*cur, col)))
		return;
	if (*cur)
		s_cat(out, CT_OFF);
	if (col)
		s_cat(out, col);
	*cur = col;
}

/* Append k bytes in a colour, noticing where ct_put closed it to mark a byte. */
size_t ct_paint(str *out, const char *p, size_t n, size_t k, ct_opt *o,
		const char **cur, const char *col)
{
	size_t i = 0, at;

	if (k)
		ct_pen(out, cur, col);
	while (i < k && i < n) {
		if (!*cur && col)
			ct_pen(out, cur, col);
		at = out->n;
		i += (size_t)ct_put(out, p + i, n - i, o);
		if (out->p[at] == '\033')
			*cur = 0;
	}
	return i;
}

/* Read the word after << or <<-, its quotes taken off, and say where it ends. */
size_t ct_hdword(const char *p, size_t n, size_t i, str *w)
{
	char q = 0, c;

	w->n = 0;
	for (; i < n; i++) {
		c = p[i];
		if (q) {
			if (c == q)
				q = 0;
			else
				s_ch(w, c);
			continue;
		}
		if (c == '\'' || c == '"') {
			q = c;
			continue;
		}
		if (c == '\\' && i + 1 < n) {
			s_ch(w, p[++i]);
			continue;
		}
		if (c == ' ' || c == '\t' || !c || strchr(";|&<>()", c))
			break;
		s_ch(w, c);
	}
	return i;
}

/* Colour a line of a heredoc's body, and close the heredoc on the line that ends it. */
void ct_hdline(str *out, const char *p, size_t n, ct_opt *o)
{
	str *hs = &o->hs;
	size_t k = ct_depth(hs), s = 0, dn;
	const char *d = hs->p + k + 2, *e = memchr(d, '\n', hs->n - k - 2), *cur = 0;

	dn = e ? (size_t)(e - d) : hs->n - k - 2;
	if (hs->p[k + 1] == '-')
		while (s < n && p[s] == '\t')
			s++;
	if (n - s == dn && !memcmp(p + s, d, dn)) {
		ct_paint(out, p, n, n, o, &cur, CT_KEY);
		memmove(hs->p + k, hs->p + k + dn + 2, hs->n - k - dn - 2);
		hs->n -= dn + 2;
		hs->p[hs->n] = 0;
	} else {
		ct_paint(out, p, n, n, o, &cur, CT_STR);
	}
	ct_pen(out, &cur, 0);
}

/* Whether byte i of a shell line begins a word. */
int ct_wstart(const char *p, size_t i)
{
	return !i || strchr(" \t;&|(){}`!", p[i - 1]);
}

/* Whether byte j of a shell line ends a word. */
int ct_wend(const char *p, size_t n, size_t j)
{
	return j >= n || (p[j] && strchr(" \t;&|(){}`", p[j]));
}

/* Where a function's name ends: letters, digits and _ - . : from i. */
size_t ct_fname(const char *p, size_t n, size_t i)
{
	while (i < n && (isalnum((unsigned char)p[i]) || strchr("_-.:", p[i])) && p[i])
		i++;
	return i;
}

/* Open a substitution at i -- $(( $( ${ or a backquote -- and say how long its opener is. */
size_t ct_subst(str *hs, const char *p, size_t n, size_t i)
{
	if (p[i] == '`') {
		ct_push(hs, '`');
		return 1;
	}
	if (p[i] != '$' || i + 1 >= n)
		return 0;
	if (p[i + 1] == '(' && i + 2 < n && p[i + 2] == '(') {
		ct_push(hs, 'a');
		return 3;
	}
	if (p[i + 1] == '(') {
		ct_push(hs, 'c');
		return 2;
	}
	if (p[i + 1] == '{') {
		ct_push(hs, 'b');
		return 2;
	}
	return 0;
}

/* Colour a word of shell code at i: a keyword, a function being defined, or neither. */
size_t ct_shword(str *out, const char *p, size_t n, size_t i, const char **kw,
		 ct_opt *o, const char **cur)
{
	size_t j = i, f, m;

	while (j < n && (isalnum((unsigned char)p[j]) || p[j] == '_'))
		j++;
	if (!ct_wstart(p, i))
		return ct_paint(out, p + i, n - i, j - i, o, cur, 0);
	f = ct_fname(p, n, i);
	for (m = f; m < n && (p[m] == ' ' || p[m] == '\t'); m++)
		;
	if (m < n && p[m] == '(') {
		for (m++; m < n && (p[m] == ' ' || p[m] == '\t'); m++)
			;
		if (m < n && p[m] == ')')
			return ct_paint(out, p + i, n - i, f - i, o, cur, CT_FN);
	}
	if (!ct_wend(p, n, j) || !ct_iskw(kw, p + i, j - i))
		return ct_paint(out, p + i, n - i, j - i, o, cur, 0);
	ct_paint(out, p + i, n - i, j - i, o, cur, CT_KEY);
	if (strncmp(p + i, "fn", j - i) && strncmp(p + i, "function", j - i))
		return j - i;
	for (m = j; m < n && (p[m] == ' ' || p[m] == '\t'); m++)
		;
	ct_paint(out, p + j, n - j, m - j, o, cur, 0);
	f = ct_fname(p, n, m);
	ct_paint(out, p + m, n - m, f - m, o, cur, CT_FN);
	return f - i;
}

/* Colour shell code at i: what opens or closes a context, a comment, a heredoc, a word. */
size_t ct_shcode(str *out, const char *p, size_t n, size_t i, const char **kw,
		 ct_opt *o, const char **cur, str *w)
{
	str *hs = &o->hs;
	int t = ct_top(hs);
	size_t j, k;
	unsigned char c = (unsigned char)p[i];

	if (c == '\\')
		return ct_paint(out, p + i, n - i, 2, o, cur, 0);
	if (c == '#' && ct_wstart(p, i))
		return ct_paint(out, p + i, n - i, n - i, o, cur, CT_CMT);
	if (c == '`' && t == '`') {
		ct_pop(hs);
		return ct_paint(out, p + i, n - i, 1, o, cur, 0);
	}
	if (c == '\'' || (c == '$' && i + 1 < n && p[i + 1] == '\'')) {
		ct_push(hs, c == '$' ? 'e' : 's');
		return ct_paint(out, p + i, n - i, c == '$' ? 2 : 1, o, cur, CT_STR);
	}
	if (c == '"') {
		ct_push(hs, 'd');
		return ct_paint(out, p + i, n - i, 1, o, cur, CT_STR);
	}
	if ((k = ct_subst(hs, p, n, i)))
		return ct_paint(out, p + i, n - i, k, o, cur, 0);
	if (c == '(' && i + 1 < n && p[i + 1] == '(') {
		ct_push(hs, 'a');
		return ct_paint(out, p + i, n - i, 2, o, cur, 0);
	}
	if (c == '(' && t)
		ct_push(hs, 'p');
	if (c == ')' && (t == 'c' || t == 'p'))
		ct_pop(hs);
	if (c == '<' && i + 1 < n && p[i + 1] == '<') {
		for (k = i; k < n && p[k] == '<'; k++)
			;
		if (k - i > 2)
			return ct_paint(out, p + i, n - i, k - i, o, cur, 0);
		if (k < n && p[k] == '-')
			k++;
		for (j = k; j < n && (p[j] == ' ' || p[j] == '\t'); j++)
			;
		k = ct_hdword(p, n, j, w);
		if (w->n) {
			s_ch(hs, '\n');
			s_ch(hs, p[i + 2] == '-' ? '-' : ' ');
			s_add(hs, w->p, w->n);
			ct_paint(out, p + i, n - i, j - i, o, cur, 0);
			ct_paint(out, p + j, n - j, k - j, o, cur, CT_KEY);
			return k - i;
		}
	}
	if (isalpha(c) || c == '_')
		return ct_shword(out, p, n, i, kw, o, cur);
	if (isdigit(c) && (!i || !isalnum((unsigned char)p[i - 1]))) {
		for (j = i; j < n && (isalnum((unsigned char)p[j]) || p[j] == '.'); j++)
			;
		return ct_paint(out, p + i, n - i, j - i, o, cur, CT_NUMC);
	}
	for (j = i + 1; j < n && p[j] && !isalnum((unsigned char)p[j]) &&
		    !strchr("_\\#`'\"$()<", p[j]); j++)
		;
	return ct_paint(out, p + i, n - i, c == '(' || c == ')' ? 1 : j - i, o, cur, 0);
}

/* Colour inside an arithmetic $(( )) or (( )): numbers, quoted keys, nested parentheses. */
size_t ct_sharith(str *out, const char *p, size_t n, size_t i, ct_opt *o,
		  const char **cur)
{
	str *hs = &o->hs;
	int t = ct_top(hs);
	size_t j, k;
	unsigned char c = (unsigned char)p[i];

	if (c == '"') {
		ct_push(hs, 'd');
		return ct_paint(out, p + i, n - i, 1, o, cur, CT_STR);
	}
	if ((k = ct_subst(hs, p, n, i)))
		return ct_paint(out, p + i, n - i, k, o, cur, 0);
	if (c == '(')
		ct_push(hs, 'q');
	if (c == ')' && t == 'q')
		ct_pop(hs);
	if (c == ')' && t == 'a' && i + 1 < n && p[i + 1] == ')') {
		ct_pop(hs);
		return ct_paint(out, p + i, n - i, 2, o, cur, 0);
	}
	if (c == ')' && t == 'a')
		hs->p[ct_depth(hs) - 1] = 'c';
	if (isdigit(c) && (!i || !isalnum((unsigned char)p[i - 1]))) {
		for (j = i; j < n && (isalnum((unsigned char)p[j]) || p[j] == '.'); j++)
			;
		return ct_paint(out, p + i, n - i, j - i, o, cur, CT_NUMC);
	}
	if (isalpha(c) || c == '_') {
		for (j = i; j < n && (isalnum((unsigned char)p[j]) || p[j] == '_'); j++)
			;
		return ct_paint(out, p + i, n - i, j - i, o, cur, 0);
	}
	return ct_paint(out, p + i, n - i, 1, o, cur, 0);
}

/* Colour inside a string or a ${ }: what closes it, an escape, a substitution. */
size_t ct_shtext(str *out, const char *p, size_t n, size_t i, ct_opt *o,
		 const char **cur)
{
	str *hs = &o->hs;
	int t = ct_top(hs);
	size_t j, k;
	const char *col = t == 'b' ? 0 : CT_STR;
	unsigned char c = (unsigned char)p[i];

	if (t == 's' || t == 'e') {
		for (j = i; j < n && p[j] != '\''; j++)
			if (t == 'e' && p[j] == '\\' && j + 1 < n)
				j++;
		if (j < n)
			ct_pop(hs);
		return ct_paint(out, p + i, n - i, j < n ? j + 1 - i : n - i, o,
				cur, CT_STR);
	}
	for (j = i; j < n && p[j] && !strchr(t == 'b' ? "\\}\"$`" : "\\\"$`", p[j]); j++)
		;
	if (j > i)
		return ct_paint(out, p + i, n - i, j - i, o, cur, col);
	if (c == '\\')
		return ct_paint(out, p + i, n - i, 2, o, cur, col);
	if ((t == 'd' && c == '"') || (t == 'b' && c == '}')) {
		ct_pop(hs);
		return ct_paint(out, p + i, n - i, 1, o, cur, col);
	}
	if (t == 'b' && c == '"') {
		ct_push(hs, 'd');
		return ct_paint(out, p + i, n - i, 1, o, cur, CT_STR);
	}
	if ((k = ct_subst(hs, p, n, i)))
		return ct_paint(out, p + i, n - i, k, o, cur, 0);
	return ct_paint(out, p + i, n - i, 1, o, cur, col);
}

/* Colour a shell line, carrying open strings, substitutions and heredocs to the next. */
void ct_sh(str *out, const char *p, size_t n, const char **kw, ct_opt *o)
{
	str w;
	size_t i = 0;
	int t;
	const char *cur = 0;

	t = ct_top(&o->hs);
	if (ct_depth(&o->hs) < o->hs.n && (!t || t == 'c' || t == 'p' || t == '`')) {
		ct_hdline(out, p, n, o);
		return;
	}
	s_init(&w);
	while (i < n) {
		t = ct_top(&o->hs);
		if (t == 's' || t == 'e' || t == 'd' || t == 'b')
			i += ct_shtext(out, p, n, i, o, &cur);
		else if (t == 'a' || t == 'q')
			i += ct_sharith(out, p, n, i, o, &cur);
		else
			i += ct_shcode(out, p, n, i, kw, o, &cur, &w);
	}
	ct_pen(out, &cur, 0);
	s_free(&w);
}

/* Colour a line lexically: comments, strings, numbers and keywords. */
void ct_hl(str *out, const char *p, size_t n, const char *lang, ct_opt *o)
{
	size_t i = 0, j;
	const char **kw = ct_kw(lang);
	const char *cm = ct_cmt(lang);
	int isc = lang && !strcmp(lang, "c");
	int ispy = lang && !strcmp(lang, "py");
	unsigned char c;

	if (lang && !strcmp(lang, "md")) {
		ct_md(out, p, n, o);
		return;
	}
	if (ct_isshell(lang)) {
		ct_sh(out, p, n, kw, o);
		return;
	}
	if (ct_blk(o) != CT_BLK_NONE)
		i = ct_resume(out, p, n, o);
	while (i < n) {
		c = (unsigned char)p[i];
		if (isc && i + 1 < n && p[i] == '/' && p[i + 1] == '*') {
			ct_setblk(o, CT_BLK_CMT);
			i += ct_resume(out, p + i, n - i, o);
			continue;
		}
		if (ispy && i + 2 < n &&
		    (!strncmp(p + i, "\"\"\"", 3) || !strncmp(p + i, "'''", 3))) {
			ct_setblk(o, p[i] == '"' ? CT_BLK_DQ : CT_BLK_SQ);
			s_cat(out, CT_STR);
			ct_run(out, p + i, 3, o);
			s_cat(out, CT_OFF);
			i += 3;
			i += ct_resume(out, p + i, n - i, o);
			continue;
		}
		if (cm && !strncmp(p + i, cm, strlen(cm))) {
			s_cat(out, CT_CMT);
			ct_run(out, p + i, n - i, o);
			s_cat(out, CT_OFF);
			return;
		}
		if (c == '"' || c == '\'') {
			unsigned char q = c;
			s_cat(out, CT_STR);
			i += (size_t)ct_put(out, p + i, n - i, o);
			while (i < n) {
				if (p[i] == '\\' && i + 1 < n) {
					i += (size_t)ct_put(out, p + i, n - i, o);
					i += (size_t)ct_put(out, p + i, n - i, o);
					continue;
				}
				c = (unsigned char)p[i];
				i += (size_t)ct_put(out, p + i, n - i, o);
				if (c == q)
					break;
			}
			s_cat(out, CT_OFF);
			continue;
		}
		if (isdigit(c) && (i == 0 || !isalnum((unsigned char)p[i - 1]))) {
			s_cat(out, CT_NUMC);
			while (i < n && (isalnum((unsigned char)p[i]) || p[i] == '.'))
				i += (size_t)ct_put(out, p + i, n - i, o);
			s_cat(out, CT_OFF);
			continue;
		}
		if (isalpha(c) || c == '_') {
			int key;
			for (j = i; j < n && (isalnum((unsigned char)p[j]) ||
					      p[j] == '_'); j++)
				;
			key = ct_iskw(kw, p + i, j - i);
			if (key)
				s_cat(out, CT_KEY);
			ct_run(out, p + i, j - i, o);
			if (key)
				s_cat(out, CT_OFF);
			i = j;
			continue;
		}
		i += (size_t)ct_put(out, p + i, n - i, o);
	}
}

/* Colour one line for another module, with the block state kept outside. */
void ct_hlapi(str *out, const char *p, size_t n, const char *lang, str *state)
{
	ct_opt o;

	memset(&o, 0, sizeof o);
	o.f = CT_COLOUR;
	o.tabw = 8;
	if (state)
		o.hs = *state;
	ct_hl(out, p, n, lang, &o);
	if (state)
		*state = o.hs;
	else
		s_free(&o.hs);
}

static const hl_api ct_api = { ct_lang, ct_hlapi };

/* Offer the colourer to whatever else draws text. */
int ct_ini(sh *s)
{
	return hibr_provide(s, "highlight", HL_API_VER, (void *)&ct_api);
}

/* Withdraw it. */
void ct_fini(sh *s)
{
	hibr_unprovide(s, "highlight");
}
