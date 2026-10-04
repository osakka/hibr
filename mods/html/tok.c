#define _GNU_SOURCE

#include "hl.h"
#include <stdlib.h>
#include <string.h>

enum {
	S_DATA = HL_SDATA, S_RCDATA = HL_SRCDATA, S_RAWTEXT = HL_SRAWTEXT,
	S_SCRIPT = HL_SSCRIPT, S_PLAIN = HL_SPLAIN,
	S_TAGOPEN, S_ENDOPEN, S_TAGNAME,
	S_RCLT, S_RCENDOPEN, S_RCENDNAME,
	S_RAWLT, S_RAWENDOPEN, S_RAWENDNAME,
	S_SCLT, S_SCENDOPEN, S_SCENDNAME, S_SCESCSTART, S_SCESCSTARTDASH,
	S_SCESC, S_SCESCDASH, S_SCESCDASHDASH, S_SCESCLT, S_SCESCENDOPEN,
	S_SCESCENDNAME, S_SCDESCSTART, S_SCDESC, S_SCDESCDASH, S_SCDESCDASHDASH,
	S_SCDESCLT, S_SCDESCEND,
	S_BATTRNAME, S_ATTRNAME, S_AATTRNAME, S_BATTRVAL, S_ATTRVALDQ,
	S_ATTRVALSQ, S_ATTRVALUQ, S_AATTRVALQ, S_SELFCLOSE,
	S_BOGUSCOMMENT, S_MARKUP, S_CSTART, S_CSTARTDASH, S_COMMENT, S_CLT,
	S_CLTBANG, S_CLTBANGDASH, S_CLTBANGDASHDASH, S_CENDDASH, S_CEND,
	S_CENDBANG,
	S_DOCTYPE, S_BDTNAME, S_DTNAME, S_ADTNAME, S_ADTPUBKW, S_BDTPUBID,
	S_DTPUBIDDQ, S_DTPUBIDSQ, S_ADTPUBID, S_BETWEENDTIDS, S_ADTSYSKW,
	S_BDTSYSID, S_DTSYSIDDQ, S_DTSYSIDSQ, S_ADTSYSID, S_BOGUSDT,
	S_CDATA, S_CDATABR, S_CDATAEND
};

#ifndef HL_EOF
#define HL_EOF (-1)
#endif

/* Decode one UTF-8 character; how many bytes it took, a bad byte read as
   U+FFFD and taking one. */
int hl_utf8(const char *s, size_t n, unsigned *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	int k, i;
	unsigned c;

	if (!n) {
		*cp = 0;
		return 0;
	}
	if (u[0] < 0x80) {
		*cp = u[0];
		return 1;
	}
	if ((u[0] & 0xE0) == 0xC0) {
		k = 2;
		c = u[0] & 0x1F;
	} else if ((u[0] & 0xF0) == 0xE0) {
		k = 3;
		c = u[0] & 0x0F;
	} else if ((u[0] & 0xF8) == 0xF0) {
		k = 4;
		c = u[0] & 0x07;
	} else {
		*cp = 0xFFFD;
		return 1;
	}
	if ((size_t)k > n) {
		*cp = 0xFFFD;
		return 1;
	}
	for (i = 1; i < k; i++) {
		if ((u[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		c = (c << 6) | (u[i] & 0x3F);
	}
	*cp = c;
	return k;
}

/* Append a code point as UTF-8. */
void hl_putcp(str *o, unsigned c)
{
	if (c < 0x80) {
		s_ch(o, (char)c);
	} else if (c < 0x800) {
		s_ch(o, (char)(0xC0 | (c >> 6)));
		s_ch(o, (char)(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		s_ch(o, (char)(0xE0 | (c >> 12)));
		s_ch(o, (char)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (char)(0x80 | (c & 0x3F)));
	} else {
		s_ch(o, (char)(0xF0 | (c >> 18)));
		s_ch(o, (char)(0x80 | ((c >> 12) & 0x3F)));
		s_ch(o, (char)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (char)(0x80 | (c & 0x3F)));
	}
}

/* A named character reference by its exact name, or none. */
const hl_ent *hl_entfind(const char *nm, size_t len)
{
	size_t lo = 0, hi = hl_nents, mid;
	int c;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		c = strncmp(hl_ents[mid].nm, nm, len);
		if (!c && hl_ents[mid].nm[len])
			c = 1;
		if (c < 0)
			lo = mid + 1;
		else if (c > 0)
			hi = mid;
		else
			return &hl_ents[mid];
	}
	return 0;
}

/* The next input character, carriage returns made line feeds, or EOF. */
int tk_nx(hl_p *p)
{
	unsigned c;
	int k;

	if (p->i >= p->n) {
		p->i = p->n + 1;
		return HL_EOF;
	}
	k = hl_utf8(p->src + p->i, p->n - p->i, &c);
	p->i += (size_t)k;
	if (c == '\r') {
		if (p->i < p->n && p->src[p->i] == '\n')
			p->i++;
		return '\n';
	}
	return (int)c;
}

/* Put the last character back, to be read again. */
void tk_back(hl_p *p, int c)
{
	if (c == HL_EOF) {
		p->i = p->n;
		return;
	}
	p->i--;
	while (p->i > 0 && ((unsigned char)p->src[p->i] & 0xC0) == 0x80)
		p->i--;
	if (c == '\n' && p->i > 0 && p->src[p->i] == '\n' && p->src[p->i - 1] == '\r')
		p->i--;
}

/* Whether the input goes on with these characters, ASCII case ignored
   when asked; consumed if so. */
int tk_ahead(hl_p *p, const char *w, int fold)
{
	size_t k = strlen(w), j;
	char a, b;

	if (p->i + k > p->n)
		return 0;
	for (j = 0; j < k; j++) {
		a = p->src[p->i + j];
		b = w[j];
		if (fold) {
			if (a >= 'A' && a <= 'Z')
				a += 32;
			if (b >= 'A' && b <= 'Z')
				b += 32;
		}
		if (a != b)
			return 0;
	}
	p->i += k;
	return 1;
}

/* Whether a character is HTML whitespace. */
int tk_ws(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\f';
}

/* ASCII letter, digit, and the lowered form of an upper-case letter. */
int tk_alpha(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Whether c is an ASCII letter or digit. */
int tk_alnum(int c)
{
	return tk_alpha(c) || (c >= '0' && c <= '9');
}

/* Lower an ASCII upper-case letter. */
int tk_low(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Send the characters gathered so far as one token. */
void tk_flush(hl_p *p)
{
	hl_tok t;

	if (!p->chars.n)
		return;
	memset(&t, 0, sizeof t);
	t.t = HL_TCHAR;
	t.data = p->chars;
	hl_emit(p, &t);
	p->chars.n = 0;
	if (p->chars.p)
		p->chars.p[0] = 0;
	p->charws = 0;
}

/* Gather one character; a run is broken where whitespace starts or stops,
   and a NUL always goes alone, which is what the tree builder's rules
   about whitespace and NUL need. */
void tk_char(hl_p *p, unsigned c)
{
	int k = c == 0 ? 3 : tk_ws((int)c) ? 1 : 2;

	if (p->charws && (p->charws != k || k == 3))
		tk_flush(p);
	p->charws = k;
	hl_putcp(&p->chars, c);
}

/* Gather a string of characters. */
void tk_chars(hl_p *p, const char *s)
{
	unsigned c;
	int k;

	while (*s) {
		k = hl_utf8(s, strlen(s), &c);
		tk_char(p, c);
		s += k;
	}
}

/* Clear the current token for a new one of a kind. */
void tk_new(hl_p *p, int t)
{
	size_t i;

	for (i = 0; i < p->tok.attrs.n; i++) {
		hl_attr *a = p->tok.attrs.p[i];

		free(a->nm);
		free(a->val);
		free(a);
	}
	p->tok.attrs.n = 0;
	p->tok.t = t;
	p->tok.self = p->tok.quirks = p->tok.ackself = 0;
	p->tok.haspub = p->tok.hassys = p->tok.hasnm = 0;
	p->tok.nm.n = p->tok.data.n = p->tok.pub.n = p->tok.sys.n = 0;
	if (p->tok.nm.p)
		p->tok.nm.p[0] = 0;
	if (p->tok.data.p)
		p->tok.data.p[0] = 0;
	if (p->tok.pub.p)
		p->tok.pub.p[0] = 0;
	if (p->tok.sys.p)
		p->tok.sys.p[0] = 0;
}

/* Begin a new attribute on the current tag. */
void tk_attr(hl_p *p)
{
	hl_attr *a = xm(sizeof *a);

	memset(a, 0, sizeof *a);
	v_add(&p->tok.attrs, a);
	p->buf.n = 0;
	if (p->buf.p)
		p->buf.p[0] = 0;
	p->tmp.n = 0;
	if (p->tmp.p)
		p->tmp.p[0] = 0;
}

/* Finish the attribute being read: a name the tag already has is dropped,
   as the spec says. */
void tk_attrdone(hl_p *p)
{
	hl_attr *a;
	size_t i;

	if (!p->tok.attrs.n)
		return;
	a = p->tok.attrs.p[p->tok.attrs.n - 1];
	if (a->nm)
		return;
	for (i = 0; i + 1 < p->tok.attrs.n; i++) {
		hl_attr *b = p->tok.attrs.p[i];

		if (b->nm && !strcmp(b->nm, p->buf.p ? p->buf.p : "")) {
			free(a);
			p->tok.attrs.n--;
			return;
		}
	}
	a->nm = xs(p->buf.p ? p->buf.p : "");
	a->val = xs(p->tmp.p ? p->tmp.p : "");
}

/* Send the current tag, comment or doctype. */
void tk_emittok(hl_p *p)
{
	tk_attrdone(p);
	tk_flush(p);
	if (p->tok.t == HL_TSTART) {
		p->lastsg.n = 0;
		s_cat(&p->lastsg, p->tok.nm.p ? p->tok.nm.p : "");
	}
	hl_emit(p, &p->tok);
}

/* The name of an end tag being read is the last start tag's, so it may
   close an RCDATA, RAWTEXT or script element. */
int tk_apt(hl_p *p)
{
	return p->tok.t == HL_TEND && p->lastsg.n &&
	       !strcmp(p->tok.nm.p ? p->tok.nm.p : "", p->lastsg.p);
}

/* The replacement for a numeric reference to one of the C1 controls, as
   windows-1252 reads it, or the number itself. */
unsigned tk_c1(unsigned c)
{
	static const unsigned t[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178
	};

	return c >= 0x80 && c <= 0x9F ? t[c - 0x80] : c;
}

/* A character reference, after its &: the characters it stands for into
   out -- an attribute's value, or the text -- or the & itself when it is
   not one. */
void tk_ref(hl_p *p, int inattr, str *out)
{
	size_t st = p->i, j, best = 0;
	const hl_ent *e, *be = 0;
	unsigned long v = 0;
	int c, hex = 0, digits = 0, semi = 0;

	c = tk_nx(p);
	if (c == '#') {
		c = tk_nx(p);
		if (c == 'x' || c == 'X') {
			hex = 1;
			c = tk_nx(p);
		}
		while (c != HL_EOF && (hex ? (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f')
				       : (c >= '0' && c <= '9'))) {
			if (v < 0x110000)
				v = v * (hex ? 16 : 10) +
				    (unsigned long)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
			digits++;
			c = tk_nx(p);
		}
		if (!digits) {
			p->i = st;
			if (out)
				s_ch(out, '&');
			else
				tk_char(p, '&');
			return;
		}
		if (c != ';')
			tk_back(p, c);
		if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
			v = 0xFFFD;
		else
			v = tk_c1((unsigned)v);
		if (out)
			hl_putcp(out, (unsigned)v);
		else
			tk_char(p, (unsigned)v);
		return;
	}
	tk_back(p, c);
	for (j = 0; j < 36 && p->i + j < p->n && tk_alnum(p->src[p->i + j]); j++)
		;
	for (; j > 0; j--) {
		e = hl_entfind(p->src + p->i, j);
		if (!e)
			continue;
		semi = p->i + j < p->n && p->src[p->i + j] == ';';
		if (semi || e->legacy) {
			be = e;
			best = j;
			break;
		}
	}
	if (!be) {
		if (out)
			s_ch(out, '&');
		else
			tk_char(p, '&');
		return;
	}
	if (!semi && inattr && p->i + best < p->n &&
	    (p->src[p->i + best] == '=' || tk_alnum(p->src[p->i + best]))) {
		s_ch(out, '&');
		return;
	}
	p->i += best + (semi ? 1 : 0);
	if (out)
		s_cat(out, be->ch);
	else
		tk_chars(p, be->ch);
}

/* Prepare to tokenize a text. */
void hl_tokinit(hl_p *p, const char *src, size_t n)
{
	p->src = src;
	p->n = n;
	p->i = 0;
	p->state = S_DATA;
	s_init(&p->buf);
	s_init(&p->tmp);
	s_init(&p->lastsg);
	s_init(&p->chars);
	s_init(&p->tok.nm);
	s_init(&p->tok.data);
	s_init(&p->tok.pub);
	s_init(&p->tok.sys);
}

/* Let go of the tokenizer's buffers. */
void hl_tokfree(hl_p *p)
{
	tk_new(p, HL_TEOF);
	v_free(&p->tok.attrs);
	s_free(&p->buf);
	s_free(&p->tmp);
	s_free(&p->lastsg);
	s_free(&p->chars);
	s_free(&p->tok.nm);
	s_free(&p->tok.data);
	s_free(&p->tok.pub);
	s_free(&p->tok.sys);
}

/* Emit the end of file. */
void tk_eof(hl_p *p)
{
	hl_tok t;

	tk_flush(p);
	memset(&t, 0, sizeof t);
	t.t = HL_TEOF;
	hl_emit(p, &t);
}

/* An end tag's name, read in RCDATA, RAWTEXT or script data: the tag when
   it is the appropriate end tag, the characters read otherwise. */
int tk_endname(hl_p *p, int c, int back)
{
	if (tk_alpha(c)) {
		hl_putcp(&p->tok.nm, (unsigned)tk_low(c));
		hl_putcp(&p->tmp, (unsigned)c);
		return -1;
	}
	if (tk_apt(p)) {
		if (tk_ws(c)) {
			p->state = S_BATTRNAME;
			return -1;
		}
		if (c == '/') {
			p->state = S_SELFCLOSE;
			return -1;
		}
		if (c == '>') {
			p->state = S_DATA;
			tk_emittok(p);
			return -1;
		}
	}
	tk_chars(p, "</");
	tk_chars(p, p->tmp.p ? p->tmp.p : "");
	tk_back(p, c);
	p->state = back;
	return 0;
}

/* Run the tokenizer over the whole text, handing each token to the tree
   builder as it is made. */
void hl_toknext(hl_p *p)
{
	int c;

	for (;;) {
		c = tk_nx(p);
		switch (p->state) {
		case S_DATA:
			if (c == '&')
				tk_ref(p, 0, 0);
			else if (c == '<')
				p->state = S_TAGOPEN;
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, (unsigned)c);
			break;
		case S_RCDATA:
			if (c == '&')
				tk_ref(p, 0, 0);
			else if (c == '<')
				p->state = S_RCLT;
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, c ? (unsigned)c : 0xFFFD);
			break;
		case S_RAWTEXT:
		case S_SCRIPT:
			if (c == '<')
				p->state = p->state == S_RAWTEXT ? S_RAWLT : S_SCLT;
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, c ? (unsigned)c : 0xFFFD);
			break;
		case S_PLAIN:
			if (c == HL_EOF) {
				tk_eof(p);
				return;
			}
			tk_char(p, c ? (unsigned)c : 0xFFFD);
			break;
		case S_TAGOPEN:
			if (c == '!')
				p->state = S_MARKUP;
			else if (c == '/')
				p->state = S_ENDOPEN;
			else if (tk_alpha(c)) {
				tk_new(p, HL_TSTART);
				tk_back(p, c);
				p->state = S_TAGNAME;
			} else if (c == '?') {
				tk_new(p, HL_TCOMMENT);
				tk_back(p, c);
				p->state = S_BOGUSCOMMENT;
			} else {
				tk_char(p, '<');
				tk_back(p, c);
				p->state = S_DATA;
			}
			break;
		case S_ENDOPEN:
			if (tk_alpha(c)) {
				tk_new(p, HL_TEND);
				tk_back(p, c);
				p->state = S_TAGNAME;
			} else if (c == '>') {
				p->state = S_DATA;
			} else if (c == HL_EOF) {
				tk_chars(p, "</");
				tk_eof(p);
				return;
			} else {
				tk_new(p, HL_TCOMMENT);
				tk_back(p, c);
				p->state = S_BOGUSCOMMENT;
			}
			break;
		case S_TAGNAME:
			if (tk_ws(c))
				p->state = S_BATTRNAME;
			else if (c == '/')
				p->state = S_SELFCLOSE;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tok.nm, c ? (unsigned)tk_low(c) : 0xFFFD);
			break;
		case S_RCLT:
		case S_RAWLT:
			if (c == '/') {
				p->tmp.n = 0;
				if (p->tmp.p)
					p->tmp.p[0] = 0;
				p->state = p->state == S_RCLT ? S_RCENDOPEN : S_RAWENDOPEN;
			} else {
				tk_char(p, '<');
				tk_back(p, c);
				p->state = p->state == S_RCLT ? S_RCDATA : S_RAWTEXT;
			}
			break;
		case S_RCENDOPEN:
		case S_RAWENDOPEN:
		case S_SCENDOPEN:
		case S_SCESCENDOPEN:
			if (tk_alpha(c)) {
				tk_new(p, HL_TEND);
				tk_back(p, c);
				p->state = p->state == S_RCENDOPEN ? S_RCENDNAME :
					   p->state == S_RAWENDOPEN ? S_RAWENDNAME :
					   p->state == S_SCENDOPEN ? S_SCENDNAME : S_SCESCENDNAME;
			} else {
				tk_chars(p, "</");
				tk_back(p, c);
				p->state = p->state == S_RCENDOPEN ? S_RCDATA :
					   p->state == S_RAWENDOPEN ? S_RAWTEXT :
					   p->state == S_SCENDOPEN ? S_SCRIPT : S_SCESC;
			}
			break;
		case S_RCENDNAME:
			tk_endname(p, c, S_RCDATA);
			break;
		case S_RAWENDNAME:
			tk_endname(p, c, S_RAWTEXT);
			break;
		case S_SCENDNAME:
			tk_endname(p, c, S_SCRIPT);
			break;
		case S_SCESCENDNAME:
			tk_endname(p, c, S_SCESC);
			break;
		case S_SCLT:
			if (c == '/') {
				p->tmp.n = 0;
				if (p->tmp.p)
					p->tmp.p[0] = 0;
				p->state = S_SCENDOPEN;
			} else if (c == '!') {
				tk_chars(p, "<!");
				p->state = S_SCESCSTART;
			} else {
				tk_char(p, '<');
				tk_back(p, c);
				p->state = S_SCRIPT;
			}
			break;
		case S_SCESCSTART:
		case S_SCESCSTARTDASH:
			if (c == '-') {
				tk_char(p, '-');
				p->state = p->state == S_SCESCSTART ? S_SCESCSTARTDASH :
					   S_SCESCDASHDASH;
			} else {
				tk_back(p, c);
				p->state = S_SCRIPT;
			}
			break;
		case S_SCESC:
			if (c == '-') {
				tk_char(p, '-');
				p->state = S_SCESCDASH;
			} else if (c == '<')
				p->state = S_SCESCLT;
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, c ? (unsigned)c : 0xFFFD);
			break;
		case S_SCESCDASH:
		case S_SCESCDASHDASH:
			if (c == '-') {
				tk_char(p, '-');
				p->state = S_SCESCDASHDASH;
			} else if (c == '<')
				p->state = S_SCESCLT;
			else if (c == '>' && p->state == S_SCESCDASHDASH) {
				tk_char(p, '>');
				p->state = S_SCRIPT;
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else {
				tk_char(p, c ? (unsigned)c : 0xFFFD);
				p->state = S_SCESC;
			}
			break;
		case S_SCESCLT:
			if (c == '/') {
				p->tmp.n = 0;
				if (p->tmp.p)
					p->tmp.p[0] = 0;
				p->state = S_SCESCENDOPEN;
			} else if (tk_alpha(c)) {
				p->tmp.n = 0;
				if (p->tmp.p)
					p->tmp.p[0] = 0;
				tk_char(p, '<');
				tk_back(p, c);
				p->state = S_SCDESCSTART;
			} else {
				tk_char(p, '<');
				tk_back(p, c);
				p->state = S_SCESC;
			}
			break;
		case S_SCDESCSTART:
		case S_SCDESCEND:
			if (tk_ws(c) || c == '/' || c == '>') {
				int is = p->tmp.p && !strcmp(p->tmp.p, "script");

				if (p->state == S_SCDESCSTART)
					p->state = is ? S_SCDESC : S_SCESC;
				else
					p->state = is ? S_SCESC : S_SCDESC;
				tk_char(p, (unsigned)c);
			} else if (tk_alpha(c)) {
				hl_putcp(&p->tmp, (unsigned)tk_low(c));
				tk_char(p, (unsigned)c);
			} else {
				tk_back(p, c);
				p->state = p->state == S_SCDESCSTART ? S_SCESC : S_SCDESC;
			}
			break;
		case S_SCDESC:
			if (c == '-') {
				tk_char(p, '-');
				p->state = S_SCDESCDASH;
			} else if (c == '<') {
				tk_char(p, '<');
				p->state = S_SCDESCLT;
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, c ? (unsigned)c : 0xFFFD);
			break;
		case S_SCDESCDASH:
		case S_SCDESCDASHDASH:
			if (c == '-') {
				tk_char(p, '-');
				p->state = S_SCDESCDASHDASH;
			} else if (c == '<') {
				tk_char(p, '<');
				p->state = S_SCDESCLT;
			} else if (c == '>' && p->state == S_SCDESCDASHDASH) {
				tk_char(p, '>');
				p->state = S_SCRIPT;
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else {
				tk_char(p, c ? (unsigned)c : 0xFFFD);
				p->state = S_SCDESC;
			}
			break;
		case S_SCDESCLT:
			if (c == '/') {
				p->tmp.n = 0;
				if (p->tmp.p)
					p->tmp.p[0] = 0;
				tk_char(p, '/');
				p->state = S_SCDESCEND;
			} else {
				tk_back(p, c);
				p->state = S_SCDESC;
			}
			break;
		case S_BATTRNAME:
			if (tk_ws(c))
				break;
			if (c == '/' || c == '>' || c == HL_EOF) {
				tk_back(p, c);
				p->state = S_AATTRNAME;
			} else if (c == '=') {
				tk_attr(p);
				s_ch(&p->buf, '=');
				p->state = S_ATTRNAME;
			} else {
				tk_attr(p);
				tk_back(p, c);
				p->state = S_ATTRNAME;
			}
			break;
		case S_ATTRNAME:
			if (tk_ws(c) || c == '/' || c == '>' || c == HL_EOF) {
				tk_back(p, c);
				p->state = S_AATTRNAME;
			} else if (c == '=')
				p->state = S_BATTRVAL;
			else
				hl_putcp(&p->buf, c ? (unsigned)tk_low(c) : 0xFFFD);
			break;
		case S_AATTRNAME:
			if (tk_ws(c))
				break;
			if (c == '/') {
				tk_attrdone(p);
				p->state = S_SELFCLOSE;
			} else if (c == '=')
				p->state = S_BATTRVAL;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else {
				tk_attrdone(p);
				tk_attr(p);
				tk_back(p, c);
				p->state = S_ATTRNAME;
			}
			break;
		case S_BATTRVAL:
			if (tk_ws(c))
				break;
			if (c == '"')
				p->state = S_ATTRVALDQ;
			else if (c == '\'')
				p->state = S_ATTRVALSQ;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else {
				tk_back(p, c);
				p->state = S_ATTRVALUQ;
			}
			break;
		case S_ATTRVALDQ:
		case S_ATTRVALSQ:
			if (c == (p->state == S_ATTRVALDQ ? '"' : '\''))
				p->state = S_AATTRVALQ;
			else if (c == '&')
				tk_ref(p, 1, &p->tmp);
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tmp, c ? (unsigned)c : 0xFFFD);
			break;
		case S_ATTRVALUQ:
			if (tk_ws(c)) {
				tk_attrdone(p);
				p->state = S_BATTRNAME;
			} else if (c == '&')
				tk_ref(p, 1, &p->tmp);
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tmp, c ? (unsigned)c : 0xFFFD);
			break;
		case S_AATTRVALQ:
			tk_attrdone(p);
			if (tk_ws(c))
				p->state = S_BATTRNAME;
			else if (c == '/')
				p->state = S_SELFCLOSE;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else {
				tk_back(p, c);
				p->state = S_BATTRNAME;
			}
			break;
		case S_SELFCLOSE:
			if (c == '>') {
				p->tok.self = 1;
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else {
				tk_back(p, c);
				p->state = S_BATTRNAME;
			}
			break;
		case S_BOGUSCOMMENT:
			if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tok.data, c ? (unsigned)c : 0xFFFD);
			break;
		case S_MARKUP:
			tk_back(p, c);
			if (tk_ahead(p, "--", 0)) {
				tk_new(p, HL_TCOMMENT);
				p->state = S_CSTART;
			} else if (tk_ahead(p, "DOCTYPE", 1)) {
				p->state = S_DOCTYPE;
			} else if (tk_ahead(p, "[CDATA[", 0)) {
				if (hl_foreign(p)) {
					p->state = S_CDATA;
				} else {
					tk_new(p, HL_TCOMMENT);
					s_cat(&p->tok.data, "[CDATA[");
					p->state = S_BOGUSCOMMENT;
				}
			} else {
				tk_new(p, HL_TCOMMENT);
				p->state = S_BOGUSCOMMENT;
			}
			break;
		case S_CSTART:
			if (c == '-')
				p->state = S_CSTARTDASH;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else {
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_CSTARTDASH:
			if (c == '-')
				p->state = S_CEND;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				s_ch(&p->tok.data, '-');
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_COMMENT:
			if (c == '<') {
				s_ch(&p->tok.data, '<');
				p->state = S_CLT;
			} else if (c == '-')
				p->state = S_CENDDASH;
			else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tok.data, c ? (unsigned)c : 0xFFFD);
			break;
		case S_CLT:
			if (c == '!') {
				s_ch(&p->tok.data, '!');
				p->state = S_CLTBANG;
			} else if (c == '<')
				s_ch(&p->tok.data, '<');
			else {
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_CLTBANG:
			if (c == '-')
				p->state = S_CLTBANGDASH;
			else {
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_CLTBANGDASH:
			if (c == '-')
				p->state = S_CLTBANGDASHDASH;
			else {
				tk_back(p, c);
				p->state = S_CENDDASH;
			}
			break;
		case S_CLTBANGDASHDASH:
			tk_back(p, c);
			p->state = S_CEND;
			break;
		case S_CENDDASH:
			if (c == '-')
				p->state = S_CEND;
			else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				s_ch(&p->tok.data, '-');
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_CEND:
			if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == '!')
				p->state = S_CENDBANG;
			else if (c == '-')
				s_ch(&p->tok.data, '-');
			else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				s_cat(&p->tok.data, "--");
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_CENDBANG:
			if (c == '-') {
				s_cat(&p->tok.data, "--!");
				p->state = S_CENDDASH;
			} else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				s_cat(&p->tok.data, "--!");
				tk_back(p, c);
				p->state = S_COMMENT;
			}
			break;
		case S_DOCTYPE:
			tk_new(p, HL_TDOCTYPE);
			if (tk_ws(c))
				p->state = S_BDTNAME;
			else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				tk_back(p, c);
				p->state = S_BDTNAME;
			}
			break;
		case S_BDTNAME:
			if (tk_ws(c))
				break;
			if (c == '>') {
				p->tok.quirks = 1;
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				hl_putcp(&p->tok.nm, c ? (unsigned)tk_low(c) : 0xFFFD);
				p->tok.hasnm = 1;
				p->state = S_DTNAME;
			}
			break;
		case S_DTNAME:
			if (tk_ws(c))
				p->state = S_ADTNAME;
			else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else
				hl_putcp(&p->tok.nm, c ? (unsigned)tk_low(c) : 0xFFFD);
			break;
		case S_ADTNAME:
			if (tk_ws(c))
				break;
			if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				tk_back(p, c);
				if (tk_ahead(p, "PUBLIC", 1))
					p->state = S_ADTPUBKW;
				else if (tk_ahead(p, "SYSTEM", 1))
					p->state = S_ADTSYSKW;
				else {
					tk_nx(p);
					p->tok.quirks = 1;
					p->state = S_BOGUSDT;
				}
			}
			break;
		case S_ADTPUBKW:
		case S_BDTPUBID:
			if (tk_ws(c)) {
				p->state = S_BDTPUBID;
			} else if (c == '"' || c == '\'') {
				p->tok.haspub = 1;
				p->state = c == '"' ? S_DTPUBIDDQ : S_DTPUBIDSQ;
			} else if (c == '>') {
				p->tok.quirks = 1;
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				p->tok.quirks = 1;
				p->state = S_BOGUSDT;
			}
			break;
		case S_DTPUBIDDQ:
		case S_DTPUBIDSQ:
		case S_DTSYSIDDQ:
		case S_DTSYSIDSQ:
			if (c == ((p->state == S_DTPUBIDDQ || p->state == S_DTSYSIDDQ) ? '"' : '\''))
				p->state = (p->state == S_DTPUBIDDQ || p->state == S_DTPUBIDSQ) ?
					   S_ADTPUBID : S_ADTSYSID;
			else if (c == '>') {
				p->tok.quirks = 1;
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else
				hl_putcp((p->state == S_DTPUBIDDQ || p->state == S_DTPUBIDSQ) ?
					 &p->tok.pub : &p->tok.sys, c ? (unsigned)c : 0xFFFD);
			break;
		case S_ADTPUBID:
		case S_BETWEENDTIDS:
			if (tk_ws(c)) {
				p->state = S_BETWEENDTIDS;
			} else if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == '"' || c == '\'') {
				p->tok.hassys = 1;
				p->state = c == '"' ? S_DTSYSIDDQ : S_DTSYSIDSQ;
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				p->tok.quirks = 1;
				p->state = S_BOGUSDT;
			}
			break;
		case S_ADTSYSKW:
		case S_BDTSYSID:
			if (tk_ws(c)) {
				p->state = S_BDTSYSID;
			} else if (c == '"' || c == '\'') {
				p->tok.hassys = 1;
				p->state = c == '"' ? S_DTSYSIDDQ : S_DTSYSIDSQ;
			} else if (c == '>') {
				p->tok.quirks = 1;
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else {
				p->tok.quirks = 1;
				p->state = S_BOGUSDT;
			}
			break;
		case S_ADTSYSID:
			if (tk_ws(c))
				break;
			if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				p->tok.quirks = 1;
				tk_emittok(p);
				tk_eof(p);
				return;
			} else
				p->state = S_BOGUSDT;
			break;
		case S_BOGUSDT:
			if (c == '>') {
				p->state = S_DATA;
				tk_emittok(p);
			} else if (c == HL_EOF) {
				tk_emittok(p);
				tk_eof(p);
				return;
			}
			break;
		case S_CDATA:
			if (c == ']')
				p->state = S_CDATABR;
			else if (c == HL_EOF) {
				tk_eof(p);
				return;
			} else
				tk_char(p, (unsigned)c);
			break;
		case S_CDATABR:
			if (c == ']')
				p->state = S_CDATAEND;
			else {
				tk_char(p, ']');
				tk_back(p, c);
				p->state = S_CDATA;
			}
			break;
		case S_CDATAEND:
			if (c == ']')
				tk_char(p, ']');
			else if (c == '>')
				p->state = S_DATA;
			else {
				tk_chars(p, "]]");
				tk_back(p, c);
				p->state = S_CDATA;
			}
			break;
		}
	}
}
