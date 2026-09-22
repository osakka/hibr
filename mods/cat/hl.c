#define _GNU_SOURCE

#include "ct.h"
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
		return 0;
	}
	d++;
	if (!strcmp(d, "c") || !strcmp(d, "h"))
		return "c";
	if (!strcmp(d, "sh") || !strcmp(d, "hibr") || !strcmp(d, "bash") ||
	    !strcmp(d, "hibrc") || !strcmp(d, "t"))
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
	if (!strcmp(lang, "sh") || !strcmp(lang, "py") || !strcmp(lang, "mk"))
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
		o->blk = o->blk == CT_BLK_FENCE ? CT_BLK_NONE : CT_BLK_FENCE;
		s_cat(out, CT_CMT);
		ct_run(out, p, n, o);
		s_cat(out, CT_OFF);
		return;
	}
	if (o->blk == CT_BLK_FENCE) {
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
	const char *cl = ct_close(o->blk);
	size_t i, cn = strlen(cl);
	const char *col = o->blk == CT_BLK_CMT ? CT_CMT : CT_STR;

	s_cat(out, col);
	for (i = 0; i < n; i++) {
		if (i + cn <= n && !strncmp(p + i, cl, cn)) {
			ct_run(out, p + i, cn, o);
			s_cat(out, CT_OFF);
			o->blk = CT_BLK_NONE;
			return i + cn;
		}
		i += (size_t)ct_put(out, p + i, n - i, o) - 1;
	}
	s_cat(out, CT_OFF);
	return n;
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
	if (o->blk != CT_BLK_NONE)
		i = ct_resume(out, p, n, o);
	while (i < n) {
		c = (unsigned char)p[i];
		if (isc && i + 1 < n && p[i] == '/' && p[i + 1] == '*') {
			o->blk = CT_BLK_CMT;
			i += ct_resume(out, p + i, n - i, o);
			continue;
		}
		if (ispy && i + 2 < n &&
		    (!strncmp(p + i, "\"\"\"", 3) || !strncmp(p + i, "'''", 3))) {
			o->blk = p[i] == '"' ? CT_BLK_DQ : CT_BLK_SQ;
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
