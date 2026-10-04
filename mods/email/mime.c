#define _GNU_SOURCE

#include "ml.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifndef ML_DEPTH
#define ML_DEPTH 32
#endif

static const char ml_b64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64, wrapped at 76 columns for a message body. */
void ml_b64enc(str *o, const char *p, size_t n)
{
	const unsigned char *u = (const unsigned char *)p;
	size_t i;
	int col = 0;

	for (i = 0; i < n; i += 3) {
		unsigned v = (unsigned)u[i] << 16;

		if (i + 1 < n)
			v |= (unsigned)u[i + 1] << 8;
		if (i + 2 < n)
			v |= u[i + 2];
		s_ch(o, ml_b64[(v >> 18) & 63]);
		s_ch(o, ml_b64[(v >> 12) & 63]);
		s_ch(o, i + 1 < n ? ml_b64[(v >> 6) & 63] : '=');
		s_ch(o, i + 2 < n ? ml_b64[v & 63] : '=');
		col += 4;
		if (col >= 76) {
			s_cat(o, "\r\n");
			col = 0;
		}
	}
	if (col)
		s_cat(o, "\r\n");
}

/* Base64 decoded, anything not of the alphabet skipped. */
void ml_b64dec(str *o, const char *p, size_t n)
{
	unsigned v = 0;
	int bits = 0;
	size_t i;
	const char *q;

	for (i = 0; i < n; i++) {
		if (p[i] == '=')
			break;
		q = strchr(ml_b64, p[i]);
		if (!q || !p[i])
			continue;
		v = (v << 6) | (unsigned)(q - ml_b64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(o, (char)((v >> bits) & 255));
		}
	}
}

/* The value of a hex digit, or -1. */
int ml_hex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Quoted-printable decoded; in a header, an underscore is a space. */
void ml_qpdec(str *o, const char *p, size_t n, int header)
{
	size_t i;
	int a, b;

	for (i = 0; i < n; i++) {
		if (p[i] == '=') {
			if (i + 1 < n && (p[i + 1] == '\n' || p[i + 1] == '\r')) {
				i++;
				if (p[i] == '\r' && i + 1 < n && p[i + 1] == '\n')
					i++;
				continue;
			}
			if (i + 2 < n && (a = ml_hex(p[i + 1])) >= 0 && (b = ml_hex(p[i + 2])) >= 0) {
				s_ch(o, (char)(a * 16 + b));
				i += 2;
				continue;
			}
			s_ch(o, '=');
		} else if (header && p[i] == '_') {
			s_ch(o, ' ');
		} else {
			s_ch(o, p[i]);
		}
	}
}

/* Quoted-printable for a body, lines kept under 76 columns. */
void ml_qpenc(str *o, const char *p, size_t n)
{
	size_t i;
	int col = 0;
	unsigned char c;
	static const char hx[] = "0123456789ABCDEF";

	for (i = 0; i < n; i++) {
		c = (unsigned char)p[i];
		if (c == '\n') {
			s_cat(o, "\r\n");
			col = 0;
			continue;
		}
		if (c == '\r')
			continue;
		if (col >= 73) {
			s_cat(o, "=\r\n");
			col = 0;
		}
		if ((c >= 33 && c <= 126 && c != '=') ||
		    ((c == ' ' || c == '\t') && i + 1 < n && p[i + 1] != '\n' && p[i + 1] != '\r')) {
			s_ch(o, (char)c);
			col++;
		} else {
			s_ch(o, '=');
			s_ch(o, hx[c >> 4]);
			s_ch(o, hx[c & 15]);
			col += 3;
		}
	}
}

/* Append a code point as UTF-8. */
void ml_putcp(str *o, unsigned c)
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

/* Whether bytes are valid UTF-8. */
int ml_isutf8(const char *p, size_t n)
{
	const unsigned char *u = (const unsigned char *)p;
	size_t i = 0;
	int k, j;

	while (i < n) {
		if (u[i] < 0x80) {
			i++;
			continue;
		}
		k = (u[i] & 0xE0) == 0xC0 ? 2 : (u[i] & 0xF0) == 0xE0 ? 3 : (u[i] & 0xF8) == 0xF0 ? 4 : 0;
		if (!k || i + (size_t)k > n)
			return 0;
		for (j = 1; j < k; j++)
			if ((u[i + j] & 0xC0) != 0x80)
				return 0;
		i += (size_t)k;
	}
	return 1;
}

/* iconv, found at run time: glibc's own, or libiconv where it is a library
   of its own, as on macOS. */
typedef struct ml_ic ml_ic;
struct ml_ic {
	void *(*open)(const char *, const char *);
	size_t (*conv)(void *, char **, size_t *, char **, size_t *);
	int (*close)(void *);
	int tried;
};
ml_ic ml_icv;

/* Load iconv once; whether it is there. */
int ml_icload(void)
{
	void *h;

	if (ml_icv.tried)
		return ml_icv.open != 0;
	ml_icv.tried = 1;
	ml_icv.open = (void *(*)(const char *, const char *))dlsym(RTLD_DEFAULT, "iconv_open");
	ml_icv.conv = (size_t (*)(void *, char **, size_t *, char **, size_t *))dlsym(RTLD_DEFAULT, "iconv");
	ml_icv.close = (int (*)(void *))dlsym(RTLD_DEFAULT, "iconv_close");
	if (ml_icv.open && ml_icv.conv && ml_icv.close)
		return 1;
	h = dlopen("libiconv.2.dylib", RTLD_NOW | RTLD_LOCAL);
	if (!h)
		h = dlopen("libiconv.dylib", RTLD_NOW | RTLD_LOCAL);
	if (!h)
		h = dlopen("libiconv.so.2", RTLD_NOW | RTLD_LOCAL);
	if (h) {
		ml_icv.open = (void *(*)(const char *, const char *))dlsym(h, "iconv_open");
		ml_icv.conv = (size_t (*)(void *, char **, size_t *, char **, size_t *))dlsym(h, "iconv");
		ml_icv.close = (int (*)(void *))dlsym(h, "iconv_close");
		if (!ml_icv.open) {
			ml_icv.open = (void *(*)(const char *, const char *))dlsym(h, "libiconv_open");
			ml_icv.conv = (size_t (*)(void *, char **, size_t *, char **, size_t *))dlsym(h, "libiconv");
			ml_icv.close = (int (*)(void *))dlsym(h, "libiconv_close");
		}
	}
	if (!ml_icv.open || !ml_icv.conv || !ml_icv.close) {
		ml_icv.open = 0;
		lg(HIBR_LDBG, "email: no iconv; only UTF-8, ASCII, Latin-1 and windows-1252 are read");
		return 0;
	}
	return 1;
}

/* Text in a charset, made UTF-8: UTF-8, ASCII, Latin-1 and windows-1252
   here, anything else through iconv; what cannot be read becomes U+FFFD,
   and text that is valid UTF-8 whatever it claims is taken as it is. */
void ml_toutf8(str *o, const char *cs, const char *p, size_t n)
{
	static const unsigned w1252[32] = {
		0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
		0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C,
		0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD,
		0x017E, 0x0178
	};
	size_t i;
	unsigned char c;

	if (!cs || !*cs || !strcasecmp(cs, "utf-8") || !strcasecmp(cs, "utf8") ||
	    !strcasecmp(cs, "us-ascii") || !strcasecmp(cs, "ascii") || ml_isutf8(p, n)) {
		if (ml_isutf8(p, n)) {
			s_add(o, p, n);
			return;
		}
		cs = "windows-1252";
	}
	if (!strcasecmp(cs, "iso-8859-1") || !strcasecmp(cs, "latin1") ||
	    !strcasecmp(cs, "windows-1252") || !strcasecmp(cs, "cp1252") ||
	    !strcasecmp(cs, "iso-8859-15")) {
		for (i = 0; i < n; i++) {
			c = (unsigned char)p[i];
			if (c >= 0x80 && c <= 0x9F)
				ml_putcp(o, w1252[c - 0x80]);
			else if (c == 0xA4 && !strcasecmp(cs, "iso-8859-15"))
				ml_putcp(o, 0x20AC);
			else
				ml_putcp(o, c);
		}
		return;
	}
	if (ml_icload()) {
		void *cd = ml_icv.open("UTF-8", cs);

		if (cd != (void *)-1 && cd) {
			char *in = (char *)p, *out, *buf;
			size_t il = n, cap = n * 4 + 16, ol = cap;

			buf = xm(cap);
			out = buf;
			while (il) {
				if (ml_icv.conv(cd, &in, &il, &out, &ol) == (size_t)-1) {
					if (errno == E2BIG)
						break;
					in++;
					il--;
					if (ol >= 3) {
						memcpy(out, "\xef\xbf\xbd", 3);
						out += 3;
						ol -= 3;
					}
				}
			}
			s_add(o, buf, (size_t)(out - buf));
			free(buf);
			ml_icv.close(cd);
			return;
		}
	}
	for (i = 0; i < n; i++) {
		c = (unsigned char)p[i];
		ml_putcp(o, c < 0x80 ? c : 0xFFFD);
	}
}

/* A header's value with its RFC 2047 encoded words decoded to UTF-8, and
   the whitespace between two adjacent encoded words dropped, as the RFC
   says. */
void ml_hdec(str *o, const char *p, size_t n)
{
	size_t i = 0, j, ws = 0;
	int lastenc = 0;
	str tmp, raw;

	s_init(&tmp);
	s_init(&raw);
	while (i < n) {
		if (p[i] == '=' && i + 1 < n && p[i + 1] == '?') {
			const char *cs = p + i + 2, *q1, *q2, *end;
			size_t rest = n - i - 2;

			q1 = memchr(cs, '?', rest);
			q2 = q1 && (size_t)(q1 - p) + 2 < n && q1[2] == '?' ? q1 + 2 : 0;
			end = q2 ? strstr(q2 + 1, "?=") : 0;
			if (q2 && end && (size_t)(end - p) < n) {
				str c;
				char *star;

				s_init(&c);
				s_add(&c, cs, (size_t)(q1 - cs));
				if ((star = strchr(c.p, '*')))
					*star = 0;
				if (lastenc)
					o->n -= ws;
				ml_clr(&raw);
				if (q1[1] == 'B' || q1[1] == 'b')
					ml_b64dec(&raw, q2 + 1, (size_t)(end - q2 - 1));
				else
					ml_qpdec(&raw, q2 + 1, (size_t)(end - q2 - 1), 1);
				ml_toutf8(o, c.p, raw.p ? raw.p : "", raw.n);
				s_free(&c);
				i = (size_t)(end - p) + 2;
				lastenc = 1;
				ws = 0;
				continue;
			}
		}
		if (p[i] == ' ' || p[i] == '\t') {
			s_ch(o, ' ');
			ws++;
			i++;
			continue;
		}
		for (j = i; j < n && p[j] != ' ' && p[j] != '\t' && !(p[j] == '=' && j + 1 < n && p[j + 1] == '?'); j++)
			;
		if (j == i)
			j = i + 1;
		ml_clr(&tmp);
		ml_toutf8(&tmp, 0, p + i, j - i);
		s_add(o, tmp.p ? tmp.p : "", tmp.n);
		i = j;
		lastenc = 0;
		ws = 0;
	}
	if (!o->p)
		s_cat(o, "");
	s_free(&tmp);
	s_free(&raw);
}

/* A header value encoded for sending: as it is when it is plain ASCII,
   else as UTF-8 encoded words, each short enough for its line. */
void ml_henc(str *o, const char *p)
{
	size_t i, n = strlen(p), k;
	int plain = 1;
	const unsigned char *u = (const unsigned char *)p;

	for (i = 0; i < n; i++)
		if (u[i] >= 0x80 || u[i] < 0x20)
			plain = 0;
	if (plain) {
		s_cat(o, p);
		return;
	}
	i = 0;
	while (i < n) {
		k = 0;
		while (i + k < n && k < 45) {
			int len = u[i + k] < 0x80 ? 1 : (u[i + k] & 0xE0) == 0xC0 ? 2 :
				  (u[i + k] & 0xF0) == 0xE0 ? 3 : 4;

			if (k + (size_t)len > 45 && k)
				break;
			k += (size_t)len;
		}
		if (i)
			s_cat(o, "\r\n ");
		s_cat(o, "=?UTF-8?B?");
		{
			str b;

			s_init(&b);
			ml_b64enc(&b, p + i, k);
			while (b.n && (b.p[b.n - 1] == '\n' || b.p[b.n - 1] == '\r'))
				b.p[--b.n] = 0;
			s_cat(o, b.p ? b.p : "");
			s_free(&b);
		}
		s_cat(o, "?=");
		i += k;
	}
}

/* IMAP's modified UTF-7 folder name made UTF-8: & starts base64 of UTF-16
   with , for /, - ends it, &- is an ampersand. */
void ml_mutf7dec(str *o, const char *p)
{
	static const char al[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	const char *q;
	unsigned v, hi = 0;
	int bits;

	while (*p) {
		if (*p != '&') {
			s_ch(o, *p++);
			continue;
		}
		p++;
		if (*p == '-') {
			s_ch(o, '&');
			p++;
			continue;
		}
		v = 0;
		bits = 0;
		while (*p && *p != '-') {
			q = strchr(al, *p++);
			if (!q)
				break;
			v = (v << 6) | (unsigned)(q - al);
			bits += 6;
			if (bits >= 16) {
				unsigned u16 = (v >> (bits - 16)) & 0xFFFF;

				bits -= 16;
				if (u16 >= 0xD800 && u16 <= 0xDBFF) {
					hi = u16;
				} else if (u16 >= 0xDC00 && u16 <= 0xDFFF && hi) {
					ml_putcp(o, 0x10000 + ((hi - 0xD800) << 10) + (u16 - 0xDC00));
					hi = 0;
				} else {
					ml_putcp(o, u16);
				}
			}
		}
		if (*p == '-')
			p++;
	}
	if (!o->p)
		s_cat(o, "");
}

/* A UTF-8 folder name in IMAP's modified UTF-7. */
void ml_mutf7enc(str *o, const char *p)
{
	static const char al[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	const unsigned char *u = (const unsigned char *)p;
	unsigned v = 0, c;
	int bits = 0, in = 0, k;

	while (*u) {
		if (*u >= 0x20 && *u < 0x7F) {
			if (in) {
				if (bits)
					s_ch(o, al[(v << (6 - bits)) & 63]);
				s_ch(o, '-');
				in = 0;
				bits = 0;
				v = 0;
			}
			if (*u == '&')
				s_cat(o, "&-");
			else
				s_ch(o, (char)*u);
			u++;
			continue;
		}
		if (!in) {
			s_ch(o, '&');
			in = 1;
		}
		k = (*u & 0xE0) == 0xC0 ? 2 : (*u & 0xF0) == 0xE0 ? 3 : (*u & 0xF8) == 0xF0 ? 4 : 1;
		c = k == 1 ? *u : k == 2 ? *u & 0x1F : k == 3 ? *u & 0x0F : *u & 0x07;
		{
			int j;

			for (j = 1; j < k && u[j]; j++)
				c = (c << 6) | (u[j] & 0x3F);
			u += j;
		}
		{
			unsigned units[2];
			int nu = 1, j;

			if (c >= 0x10000) {
				c -= 0x10000;
				units[0] = 0xD800 + (c >> 10);
				units[1] = 0xDC00 + (c & 0x3FF);
				nu = 2;
			} else {
				units[0] = c;
			}
			for (j = 0; j < nu; j++) {
				v = (v << 16) | units[j];
				bits += 16;
				while (bits >= 6) {
					bits -= 6;
					s_ch(o, al[(v >> bits) & 63]);
				}
			}
		}
	}
	if (in) {
		if (bits)
			s_ch(o, al[(v << (6 - bits)) & 63]);
		s_ch(o, '-');
	}
	if (!o->p)
		s_cat(o, "");
}

/* A date from a Date header or an IMAP INTERNALDATE, as seconds since the
   epoch; 0 when it cannot be read. */
long ml_date(const char *s)
{
	static const char *mon[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug",
				     "sep", "oct", "nov", "dec" };
	struct tm tm;
	int d = 0, y = 0, hh = 0, mm = 0, ss = 0, m = -1, i, sign = 1, off = 0;
	const char *p = s;
	time_t t;

	if (!s)
		return 0;
	while (*p && !isdigit((unsigned char)*p))
		p++;
	d = atoi(p);
	while (*p && isdigit((unsigned char)*p))
		p++;
	while (*p && !isalpha((unsigned char)*p))
		p++;
	for (i = 0; i < 12; i++)
		if (!strncasecmp(p, mon[i], 3))
			m = i;
	while (*p && !isdigit((unsigned char)*p))
		p++;
	y = atoi(p);
	while (*p && isdigit((unsigned char)*p))
		p++;
	if (y < 100)
		y += y < 50 ? 2000 : 1900;
	while (*p == ' ' || *p == '-')
		p++;
	hh = atoi(p);
	if ((p = strchr(p, ':'))) {
		mm = atoi(++p);
		while (isdigit((unsigned char)*p))
			p++;
		if (*p == ':')
			ss = atoi(++p);
		while (*p && *p != '+' && *p != '-' && !isalpha((unsigned char)*p))
			p++;
		if (*p == '+' || *p == '-') {
			sign = *p == '-' ? -1 : 1;
			off = atoi(p + 1);
			off = sign * ((off / 100) * 3600 + (off % 100) * 60);
		}
	}
	if (m < 0 || !d || !y)
		return 0;
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = m;
	tm.tm_mday = d;
	tm.tm_hour = hh;
	tm.tm_min = mm;
	tm.tm_sec = ss;
	t = timegm(&tm);
	return (long)t - off;
}

/* The raw value of a header in a block of headers, unfolded, or empty. */
void ml_hraw(const char *raw, size_t n, const char *name, str *o)
{
	size_t i = 0, nl = strlen(name), e;

	ml_clr(o);
	while (i < n) {
		e = i;
		while (e < n && raw[e] != '\n')
			e++;
		if (e == i || (e == i + 1 && raw[i] == '\r'))
			break;
		if (e - i > nl && raw[i + nl] == ':' && !strncasecmp(raw + i, name, nl)) {
			size_t v = i + nl + 1;

			for (;;) {
				size_t end = e;

				while (v < end && (raw[v] == ' ' || raw[v] == '\t'))
					v++;
				if (end > v && raw[end - 1] == '\r')
					end--;
				if (o->n)
					s_ch(o, ' ');
				s_add(o, raw + v, end - v);
				if (e + 1 < n && (raw[e + 1] == ' ' || raw[e + 1] == '\t')) {
					v = e + 1;
					e = v;
					while (e < n && raw[e] != '\n')
						e++;
					continue;
				}
				break;
			}
			if (!o->p)
				s_cat(o, "");
			return;
		}
		i = e + 1;
	}
	if (!o->p)
		s_cat(o, "");
}

/* A parameter of a Content-Type or Content-Disposition value: quoted or
   bare, and RFC 2231's charset''percent-encoding and *0 *1 continuations. */
char *ml_param(const char *v, const char *name)
{
	str o, key, cont;
	const char *p = v;
	size_t nl = strlen(name);
	int any = 0, ext = 0;

	s_init(&o);
	s_init(&key);
	s_init(&cont);
	while ((p = strchr(p, ';'))) {
		const char *k, *ke, *val;
		int star = 0, idx = -1;

		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		k = p;
		while (*p && *p != '=' && *p != ';')
			p++;
		if (*p != '=')
			continue;
		ke = p;
		while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
			ke--;
		if (ke > k && ke[-1] == '*') {
			star = 1;
			ke--;
		}
		{
			const char *ast = memchr(k, '*', (size_t)(ke - k));

			if (ast && isdigit((unsigned char)ast[1])) {
				idx = atoi(ast + 1);
				ke = ast;
			}
		}
		if ((size_t)(ke - k) != nl || strncasecmp(k, name, nl)) {
			p++;
			if (*p == '"') {
				p++;
				while (*p && *p != '"') {
					if (*p == '\\' && p[1])
						p++;
					p++;
				}
			}
			continue;
		}
		val = ++p;
		ml_clr(&cont);
		if (*val == '"') {
			val++;
			p = val;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1])
					p++;
				s_ch(&cont, *p++);
			}
		} else {
			p = val;
			while (*p && *p != ';' && *p != ' ' && *p != '\t')
				s_ch(&cont, *p++);
		}
		if (star) {
			const char *c = cont.p ? cont.p : "", *q;
			str dec, cs;

			s_init(&dec);
			s_init(&cs);
			q = idx <= 0 ? strchr(c, '\'') : 0;
			if (q) {
				s_add(&cs, c, (size_t)(q - c));
				q = strchr(q + 1, '\'');
				c = q ? q + 1 : c;
				if (cs.n)
					s_cat(&key, cs.p);
			}
			for (; *c; c++) {
				if (*c == '%' && ml_hex(c[1]) >= 0 && ml_hex(c[2]) >= 0) {
					s_ch(&dec, (char)(ml_hex(c[1]) * 16 + ml_hex(c[2])));
					c += 2;
				} else {
					s_ch(&dec, *c);
				}
			}
			s_add(&o, dec.p ? dec.p : "", dec.n);
			ext = 1;
			s_free(&dec);
			s_free(&cs);
		} else {
			s_add(&o, cont.p ? cont.p : "", cont.n);
		}
		any = 1;
	}
	s_free(&cont);
	if (!any) {
		s_free(&o);
		s_free(&key);
		return 0;
	}
	if (ext) {
		str u;

		s_init(&u);
		ml_toutf8(&u, key.n ? key.p : "utf-8", o.p ? o.p : "", o.n);
		s_free(&o);
		s_free(&key);
		return u.p ? u.p : xs("");
	}
	s_free(&key);
	{
		str u;

		s_init(&u);
		ml_hdec(&u, o.p ? o.p : "", o.n);
		s_free(&o);
		return u.p;
	}
}

/* Free a parsed message. */
void ml_msgfree(ml_msg *m)
{
	size_t i;

	for (i = 0; i < m->hn.n; i++) {
		free(m->hn.p[i]);
		free(m->hv.p[i]);
	}
	v_free(&m->hn);
	v_free(&m->hv);
	for (i = 0; i < m->parts.n; i++) {
		ml_part *p = m->parts.p[i];

		free(p->type);
		free(p->charset);
		free(p->name);
		free(p->cid);
		free(p->disp);
		free(p->enc);
		free(p);
	}
	v_free(&m->parts);
	s_free(&m->text);
	s_free(&m->html);
}

/* A decoded header of a parsed message, or none. */
const char *ml_hget(ml_msg *m, const char *name)
{
	size_t i;

	for (i = 0; i < m->hn.n; i++)
		if (!strcasecmp(m->hn.p[i], name))
			return m->hv.p[i];
	return 0;
}

/* Where a part's body starts: after its headers' blank line. */
size_t ml_bodyat(const char *raw, size_t s, size_t e)
{
	size_t i;

	for (i = s; i < e; i++) {
		if (raw[i] == '\n' && i + 1 < e && raw[i + 1] == '\n')
			return i + 2;
		if (raw[i] == '\n' && i + 2 < e && raw[i + 1] == '\r' && raw[i + 2] == '\n')
			return i + 3;
	}
	if (s < e && (raw[s] == '\n' || (raw[s] == '\r' && s + 1 < e && raw[s + 1] == '\n')))
		return raw[s] == '\n' ? s + 1 : s + 2;
	return e;
}

/* Decode a part's body: its transfer encoding undone, and, for text, made
   UTF-8. */
int ml_partbody(ml_msg *m, size_t i, str *o)
{
	ml_part *p;
	str raw;

	if (i >= m->parts.n)
		return HIBR_FAIL;
	p = m->parts.p[i];
	s_init(&raw);
	if (p->enc && !strcasecmp(p->enc, "base64"))
		ml_b64dec(&raw, m->raw + p->bs, p->be - p->bs);
	else if (p->enc && !strcasecmp(p->enc, "quoted-printable"))
		ml_qpdec(&raw, m->raw + p->bs, p->be - p->bs, 0);
	else
		s_add(&raw, m->raw + p->bs, p->be - p->bs);
	if (p->type && !strncasecmp(p->type, "text/", 5))
		ml_toutf8(o, p->charset, raw.p ? raw.p : "", raw.n);
	else
		s_add(o, raw.p ? raw.p : "", raw.n);
	s_free(&raw);
	return HIBR_OK;
}

/* Walk a part's MIME structure, recording each leaf; a multipart's parts
   are found between its boundary lines. */
void ml_walk(ml_msg *m, size_t hs, size_t e, int dep)
{
	str ct, cte, cd, cid, b;
	size_t bs = ml_bodyat(m->raw, hs, e);
	char *type, *semi, *bnd;
	ml_part *p;

	s_init(&ct);
	s_init(&cte);
	s_init(&cd);
	s_init(&cid);
	ml_hraw(m->raw + hs, bs - hs, "Content-Type", &ct);
	ml_hraw(m->raw + hs, bs - hs, "Content-Transfer-Encoding", &cte);
	ml_hraw(m->raw + hs, bs - hs, "Content-Disposition", &cd);
	ml_hraw(m->raw + hs, bs - hs, "Content-ID", &cid);
	type = xs(ct.n ? ct.p : "text/plain");
	if ((semi = strchr(type, ';')))
		*semi = 0;
	{
		char *t = type + strlen(type);

		while (t > type && (t[-1] == ' ' || t[-1] == '\t'))
			*--t = 0;
		for (t = type; *t; t++)
			*t = (char)tolower((unsigned char)*t);
	}
	bnd = !strncmp(type, "multipart/", 10) ? ml_param(ct.p ? ct.p : "", "boundary") : 0;
	if (bnd && *bnd && dep < ML_DEPTH) {
		size_t i = bs, start = 0, bl = strlen(bnd);
		int in = 0;

		s_init(&b);
		s_cat(&b, "--");
		s_cat(&b, bnd);
		while (i < e) {
			size_t le = i;

			while (le < e && m->raw[le] != '\n')
				le++;
			if (le - i >= bl + 2 && !memcmp(m->raw + i, b.p, bl + 2)) {
				int last = le - i >= bl + 4 && m->raw[i + bl + 2] == '-' && m->raw[i + bl + 3] == '-';

				if (in) {
					size_t pe = i;

					if (pe > start && m->raw[pe - 1] == '\n')
						pe--;
					if (pe > start && m->raw[pe - 1] == '\r')
						pe--;
					ml_walk(m, start, pe, dep + 1);
				}
				if (last)
					break;
				in = 1;
				start = le + 1;
			}
			i = le + 1;
		}
		s_free(&b);
		free(bnd);
		free(type);
		s_free(&ct);
		s_free(&cte);
		s_free(&cd);
		s_free(&cid);
		return;
	}
	free(bnd);
	p = xm(sizeof *p);
	memset(p, 0, sizeof *p);
	p->type = type;
	p->charset = ml_param(ct.p ? ct.p : "", "charset");
	p->name = ml_param(cd.p ? cd.p : "", "filename");
	if (!p->name)
		p->name = ml_param(ct.p ? ct.p : "", "name");
	{
		char *d = xs(cd.n ? cd.p : "inline"), *sc = strchr(d, ';');

		if (sc)
			*sc = 0;
		p->disp = d;
	}
	{
		str c;

		s_init(&c);
		s_add(&c, cid.p ? cid.p : "", cid.n);
		if (c.n && c.p[0] == '<')
			memmove(c.p, c.p + 1, c.n--);
		if (c.n && c.p[c.n - 1] == '>')
			c.p[--c.n] = 0;
		p->cid = xs(c.p ? c.p : "");
		s_free(&c);
	}
	{
		char *t = xs(cte.n ? cte.p : "7bit"), *u;

		for (u = t; *u; u++)
			*u = (char)tolower((unsigned char)*u);
		while (u > t && (u[-1] == ' ' || u[-1] == '\t'))
			*--u = 0;
		p->enc = t;
	}
	p->hs = hs;
	p->bs = bs;
	p->be = e;
	p->leaf = 1;
	v_add(&m->parts, p);
	if (strcasecmp(p->disp, "attachment") && !p->name) {
		if (!strcmp(type, "text/plain") && !m->text.n)
			ml_partbody(m, m->parts.n - 1, &m->text);
		else if (!strcmp(type, "text/html") && !m->html.n)
			ml_partbody(m, m->parts.n - 1, &m->html);
	}
	s_free(&ct);
	s_free(&cte);
	s_free(&cd);
	s_free(&cid);
}

/* Parse a whole message: its headers decoded, its parts found, its text
   and HTML bodies made UTF-8. The message stays where it is; parts are
   offsets into it. */
int ml_msgparse(const char *raw, size_t n, ml_msg *m)
{
	size_t i = 0, e, bs;
	str v, d;

	memset(m, 0, sizeof *m);
	s_init(&m->text);
	s_init(&m->html);
	m->raw = raw;
	m->rawn = n;
	bs = ml_bodyat(raw, 0, n);
	s_init(&v);
	while (i < bs) {
		size_t c;

		e = i;
		while (e < bs && raw[e] != '\n')
			e++;
		if (e == i || (e == i + 1 && raw[i] == '\r'))
			break;
		c = i;
		while (c < e && raw[c] != ':')
			c++;
		if (c < e && raw[i] != ' ' && raw[i] != '\t') {
			char *nm;

			ml_clr(&v);
			{
				str one;

				s_init(&one);
				s_add(&one, raw + i, c - i);
				nm = one.p;
				ml_hraw(raw + i, bs - i, nm, &v);
			}
			s_init(&d);
			ml_hdec(&d, v.p ? v.p : "", v.n);
			v_add(&m->hn, nm);
			v_add(&m->hv, d.p ? d.p : xs(""));
		}
		i = e + 1;
		while (i < bs && (raw[i] == ' ' || raw[i] == '\t')) {
			while (i < bs && raw[i] != '\n')
				i++;
			i++;
		}
	}
	s_free(&v);
	ml_walk(m, 0, n, 0);
	return HIBR_OK;
}
