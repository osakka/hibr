#include "wb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct jr jr;
struct jr {
	const char *p, *e;
	int depth;
};

jv *jv_val(jr *r);

/* A value of type t, zeroed. */
jv *jv_new(int t)
{
	jv *v = xm(sizeof *v);

	memset(v, 0, sizeof *v);
	v->t = t;
	s_init(&v->s);
	return v;
}

/* Free a value and everything in it. */
void jv_free(jv *v)
{
	size_t i;

	if (!v)
		return;
	for (i = 0; i < v->k.n; i++)
		free(v->k.p[i]);
	for (i = 0; i < v->v.n; i++)
		jv_free(v->v.p[i]);
	v_free(&v->k);
	v_free(&v->v);
	s_free(&v->s);
	free(v);
}

/* Skip blanks. */
void jr_ws(jr *r)
{
	while (r->p < r->e && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' ||
			       *r->p == '\r'))
		r->p++;
}

/* Four hex digits as a number, or -1. */
long jr_hex4(const char *p, const char *e)
{
	long v = 0;
	int i;

	if (e - p < 4)
		return -1;
	for (i = 0; i < 4; i++) {
		char c = p[i];
		v <<= 4;
		if (c >= '0' && c <= '9')
			v |= c - '0';
		else if ((c | 32) >= 'a' && (c | 32) <= 'f')
			v |= (c | 32) - 'a' + 10;
		else
			return -1;
	}
	return v;
}

/* Append a code point as UTF-8. */
void jr_utf8(str *o, unsigned long c)
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

/* A string, after its opening quote, into o; fails on a bad one. */
int jr_str(jr *r, str *o)
{
	long c, lo;

	while (r->p < r->e && *r->p != '"') {
		if (*r->p != '\\') {
			s_ch(o, *r->p++);
			continue;
		}
		r->p++;
		if (r->p >= r->e)
			return 0;
		switch (*r->p++) {
		case '"': s_ch(o, '"'); break;
		case '\\': s_ch(o, '\\'); break;
		case '/': s_ch(o, '/'); break;
		case 'b': s_ch(o, '\b'); break;
		case 'f': s_ch(o, '\f'); break;
		case 'n': s_ch(o, '\n'); break;
		case 'r': s_ch(o, '\r'); break;
		case 't': s_ch(o, '\t'); break;
		case 'u':
			c = jr_hex4(r->p, r->e);
			if (c < 0)
				return 0;
			r->p += 4;
			if (c >= 0xD800 && c < 0xDC00 && r->e - r->p >= 6 &&
			    r->p[0] == '\\' && r->p[1] == 'u') {
				lo = jr_hex4(r->p + 2, r->e);
				if (lo >= 0xDC00 && lo < 0xE000) {
					c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
					r->p += 6;
				}
			}
			if (c >= 0xD800 && c < 0xE000)
				c = 0xFFFD;
			jr_utf8(o, (unsigned long)c);
			break;
		default:
			return 0;
		}
	}
	if (r->p >= r->e)
		return 0;
	r->p++;
	return 1;
}

/* An object, after its {. */
jv *jr_obj(jr *r)
{
	jv *o = jv_new(JV_OBJ), *v;
	str k;

	jr_ws(r);
	if (r->p < r->e && *r->p == '}') {
		r->p++;
		return o;
	}
	for (;;) {
		jr_ws(r);
		if (r->p >= r->e || *r->p != '"')
			break;
		r->p++;
		s_init(&k);
		if (!jr_str(r, &k)) {
			s_free(&k);
			break;
		}
		jr_ws(r);
		if (r->p >= r->e || *r->p != ':') {
			s_free(&k);
			break;
		}
		r->p++;
		v = jv_val(r);
		if (!v) {
			s_free(&k);
			break;
		}
		v_add(&o->k, xs(k.p ? k.p : ""));
		s_free(&k);
		v_add(&o->v, v);
		jr_ws(r);
		if (r->p < r->e && *r->p == ',') {
			r->p++;
			continue;
		}
		if (r->p < r->e && *r->p == '}') {
			r->p++;
			return o;
		}
		break;
	}
	jv_free(o);
	return 0;
}

/* An array, after its [. */
jv *jr_arr(jr *r)
{
	jv *a = jv_new(JV_ARR), *v;

	jr_ws(r);
	if (r->p < r->e && *r->p == ']') {
		r->p++;
		return a;
	}
	for (;;) {
		v = jv_val(r);
		if (!v)
			break;
		v_add(&a->v, v);
		jr_ws(r);
		if (r->p < r->e && *r->p == ',') {
			r->p++;
			continue;
		}
		if (r->p < r->e && *r->p == ']') {
			r->p++;
			return a;
		}
		break;
	}
	jv_free(a);
	return 0;
}

/* Any value; null on bad input or nesting past a bound. */
jv *jv_val(jr *r)
{
	jv *v;
	char *end;

	jr_ws(r);
	if (r->p >= r->e || ++r->depth > 200)
		return 0;
	switch (*r->p) {
	case '{':
		r->p++;
		v = jr_obj(r);
		break;
	case '[':
		r->p++;
		v = jr_arr(r);
		break;
	case '"':
		r->p++;
		v = jv_new(JV_STR);
		if (!jr_str(r, &v->s)) {
			jv_free(v);
			v = 0;
		}
		break;
	case 't':
	case 'f':
	case 'n':
		if (r->e - r->p >= 4 && !strncmp(r->p, "true", 4)) {
			v = jv_new(JV_BOOL);
			v->n = 1;
			r->p += 4;
		} else if (r->e - r->p >= 5 && !strncmp(r->p, "false", 5)) {
			v = jv_new(JV_BOOL);
			r->p += 5;
		} else if (r->e - r->p >= 4 && !strncmp(r->p, "null", 4)) {
			v = jv_new(JV_NULL);
			r->p += 4;
		} else {
			v = 0;
		}
		break;
	default:
		v = jv_new(JV_NUM);
		v->n = strtod(r->p, &end);
		if (end == r->p || end > r->e) {
			jv_free(v);
			v = 0;
		} else {
			r->p = end;
		}
	}
	r->depth--;
	return v;
}

/* Parse n bytes of JSON; null when it is not JSON. */
jv *jv_parse(const char *p, size_t n)
{
	jr r;

	r.p = p;
	r.e = p + n;
	r.depth = 0;
	return jv_val(&r);
}

/* A member of an object, or null. */
jv *jv_get(jv *o, const char *k)
{
	size_t i;

	if (!o || o->t != JV_OBJ)
		return 0;
	for (i = 0; i < o->k.n; i++)
		if (!strcmp(o->k.p[i], k))
			return o->v.p[i];
	return 0;
}

/* A member reached through dotted keys, "result.result.value"; a number
   in the path indexes an array. */
jv *jv_path(jv *o, const char *path)
{
	const char *p = path, *d;
	char *k;
	size_t n;

	while (o && *p) {
		d = strchr(p, '.');
		n = d ? (size_t)(d - p) : strlen(p);
		k = xm(n + 1);
		memcpy(k, p, n);
		k[n] = 0;
		if (o->t == JV_ARR && k[0] >= '0' && k[0] <= '9') {
			size_t i = (size_t)strtoul(k, 0, 10);
			o = i < o->v.n ? o->v.p[i] : 0;
		} else {
			o = jv_get(o, k);
		}
		free(k);
		p = d ? d + 1 : p + n;
	}
	return o;
}

/* A string value's text, or "" for anything else. */
const char *jv_str(jv *v)
{
	return v && v->t == JV_STR && v->s.p ? v->s.p : "";
}

/* A number value, or 0. */
double jv_num(jv *v)
{
	return v && (v->t == JV_NUM || v->t == JV_BOOL) ? v->n : 0;
}

/* Text as a JSON string, quotes and all. */
void jv_quote(str *o, const char *p)
{
	char b[8];
	unsigned char c;

	s_ch(o, '"');
	for (; *p; p++) {
		c = (unsigned char)*p;
		if (c == '"' || c == '\\') {
			s_ch(o, '\\');
			s_ch(o, c);
		} else if (c == '\n') {
			s_cat(o, "\\n");
		} else if (c == '\r') {
			s_cat(o, "\\r");
		} else if (c == '\t') {
			s_cat(o, "\\t");
		} else if (c < 0x20) {
			snprintf(b, sizeof b, "\\u%04x", c);
			s_cat(o, b);
		} else {
			s_ch(o, c);
		}
	}
	s_ch(o, '"');
}

/* A value written back out as JSON. */
void jv_write(str *o, jv *v)
{
	char b[40];
	size_t i;

	if (!v) {
		s_cat(o, "null");
		return;
	}
	switch (v->t) {
	case JV_NULL:
		s_cat(o, "null");
		break;
	case JV_BOOL:
		s_cat(o, v->n ? "true" : "false");
		break;
	case JV_NUM:
		if (v->n == (double)(long long)v->n && v->n < 1e15 && v->n > -1e15)
			snprintf(b, sizeof b, "%lld", (long long)v->n);
		else
			snprintf(b, sizeof b, "%.17g", v->n);
		s_cat(o, b);
		break;
	case JV_STR:
		jv_quote(o, v->s.p ? v->s.p : "");
		break;
	case JV_ARR:
		s_ch(o, '[');
		for (i = 0; i < v->v.n; i++) {
			if (i)
				s_ch(o, ',');
			jv_write(o, v->v.p[i]);
		}
		s_ch(o, ']');
		break;
	case JV_OBJ:
		s_ch(o, '{');
		for (i = 0; i < v->v.n; i++) {
			if (i)
				s_ch(o, ',');
			jv_quote(o, v->k.p[i]);
			s_ch(o, ':');
			jv_write(o, v->v.p[i]);
		}
		s_ch(o, '}');
		break;
	}
}
