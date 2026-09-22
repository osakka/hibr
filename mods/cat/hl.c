#define _GNU_SOURCE

#include "ct.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CT_DIM "\033[38;5;244m"
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

/* The comment introducer for a language, or null. */
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

/* Append one byte, showing it if it would otherwise be invisible. */
void ct_put(str *out, unsigned char c, unsigned f, int *plain)
{
	if (c == '\t' && !(f & CT_TABS)) {
		s_ch(out, '\t');
		return;
	}
	if (c >= 32 && c != 127) {
		s_ch(out, (char)c);
		return;
	}
	s_cat(out, CT_DIM);
	if (c == 127) {
		s_cat(out, "^?");
	} else {
		s_ch(out, '^');
		s_ch(out, (char)(c + 64));
	}
	s_cat(out, CT_OFF);
	*plain = 0;
}

/* Colour a markdown line by what it opens with. */
void ct_md(str *out, const char *p, size_t n, unsigned f)
{
	size_t i = 0;
	int plain = 1;
	const char *col = 0;

	while (i < n && (p[i] == ' ' || p[i] == '\t'))
		i++;
	if (i < n && p[i] == '#')
		col = CT_HEAD;
	else if (i < n && (p[i] == '-' || p[i] == '*' || p[i] == '>') &&
		 i + 1 < n && p[i + 1] == ' ')
		col = CT_KEY;
	else if (n >= 3 && !strncmp(p, "```", 3))
		col = CT_CMT;
	if (col)
		s_cat(out, col);
	for (i = 0; i < n; i++)
		ct_put(out, (unsigned char)p[i], f, &plain);
	if (col)
		s_cat(out, CT_OFF);
}

/* Colour a line lexically: comments, strings, numbers and keywords. */
void ct_hl(str *out, const char *p, size_t n, const char *lang, unsigned f)
{
	size_t i = 0, j;
	int plain = 1;
	const char **kw = ct_kw(lang);
	const char *cm = ct_cmt(lang);
	unsigned char c;

	if (lang && !strcmp(lang, "md")) {
		ct_md(out, p, n, f);
		return;
	}
	while (i < n) {
		c = (unsigned char)p[i];
		if (cm && !strncmp(p + i, cm, strlen(cm))) {
			s_cat(out, CT_CMT);
			for (; i < n; i++)
				ct_put(out, (unsigned char)p[i], f, &plain);
			s_cat(out, CT_OFF);
			break;
		}
		if (c == '"' || c == '\'') {
			unsigned char q = c;
			s_cat(out, CT_STR);
			ct_put(out, c, f, &plain);
			for (i++; i < n; i++) {
				ct_put(out, (unsigned char)p[i], f, &plain);
				if (p[i] == '\\' && i + 1 < n) {
					i++;
					ct_put(out, (unsigned char)p[i], f,
					       &plain);
					continue;
				}
				if ((unsigned char)p[i] == q) {
					i++;
					break;
				}
			}
			s_cat(out, CT_OFF);
			continue;
		}
		if (isdigit(c) && (i == 0 || !isalnum((unsigned char)p[i - 1]))) {
			s_cat(out, CT_NUMC);
			for (; i < n && (isalnum((unsigned char)p[i]) ||
					 p[i] == '.'); i++)
				ct_put(out, (unsigned char)p[i], f, &plain);
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
			for (; i < j; i++)
				ct_put(out, (unsigned char)p[i], f, &plain);
			if (key)
				s_cat(out, CT_OFF);
			continue;
		}
		ct_put(out, c, f, &plain);
		i++;
	}
}
