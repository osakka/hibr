#include "pri.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jp { const char *p; int bad; };

/* Skip JSON whitespace. */
void j_ws(struct jp *j)
{
	while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')
		j->p++;
}

/* Encode one code point as UTF-8. */
void j_utf8(str *o, unsigned c)
{
	if (c < 0x80) {
		s_ch(o, (int)c);
	} else if (c < 0x800) {
		s_ch(o, (int)(0xC0 | (c >> 6)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	} else {
		s_ch(o, (int)(0xE0 | (c >> 12)));
		s_ch(o, (int)(0x80 | ((c >> 6) & 0x3F)));
		s_ch(o, (int)(0x80 | (c & 0x3F)));
	}
}

/* Read a quoted JSON string, resolving escapes. */
char *j_str(struct jp *j)
{
	str o;
	unsigned u;
	int i;

	s_init(&o);
	if (*j->p != '"') {
		j->bad = 1;
		return o.p ? o.p : xs("");
	}
	j->p++;
	while (*j->p && *j->p != '"') {
		if (*j->p != '\\') {
			s_ch(&o, *j->p++);
			continue;
		}
		j->p++;
		switch (*j->p) {
		case 'n': s_ch(&o, '\n'); j->p++; break;
		case 't': s_ch(&o, '\t'); j->p++; break;
		case 'r': s_ch(&o, '\r'); j->p++; break;
		case 'b': s_ch(&o, '\b'); j->p++; break;
		case 'f': s_ch(&o, '\f'); j->p++; break;
		case 'u':
			j->p++;
			u = 0;
			for (i = 0; i < 4 && isxdigit((unsigned char)*j->p); i++) {
				u = u * 16 + (unsigned)(isdigit((unsigned char)*j->p) ?
							*j->p - '0' :
							(tolower(*j->p) - 'a' + 10));
				j->p++;
			}
			j_utf8(&o, u);
			break;
		default:
			if (*j->p)
				s_ch(&o, *j->p++);
			break;
		}
	}
	if (*j->p == '"')
		j->p++;
	else
		j->bad = 1;
	return o.p ? o.p : xs("");
}

/* Parse any JSON value into an entry. */
void j_val(struct jp *j, ent *e)
{
	str ix;
	char *k;
	ent *c;
	const char *b;

	j_ws(j);
	if (*j->p == '{' || *j->p == '[') {
		int arr = *j->p == '[';
		long n = 0;
		j->p++;
		e->ty = arr ? J_ARR : J_OBJ;
		j_ws(j);
		if (*j->p == (arr ? ']' : '}')) {
			j->p++;
			return;
		}
		for (;;) {
			j_ws(j);
			if (arr) {
				s_init(&ix);
				s_num(&ix, n++);
				k = ix.p;
			} else {
				k = j_str(j);
				j_ws(j);
				if (*j->p == ':')
					j->p++;
				else
					j->bad = 1;
			}
			c = mp_add(&e->map, &e->n, k);
			free(k);
			j_val(j, c);
			j_ws(j);
			if (*j->p == ',') {
				j->p++;
				continue;
			}
			if (*j->p == (arr ? ']' : '}')) {
				j->p++;
				return;
			}
			j->bad = 1;
			return;
		}
	}
	if (*j->p == '"') {
		e->ty = J_STR;
		free(e->s);
		e->s = j_str(j);
		return;
	}
	b = j->p;
	while (*j->p && !strchr(" \t\n\r,}]", *j->p))
		j->p++;
	free(e->s);
	e->s = xm((size_t)(j->p - b) + 1);
	memcpy(e->s, b, (size_t)(j->p - b));
	e->s[j->p - b] = 0;
	if (!strcmp(e->s, "true") || !strcmp(e->s, "false"))
		e->ty = J_BOOL;
	else if (!strcmp(e->s, "null"))
		e->ty = J_NULL;
	else if (*e->s == '-' || isdigit((unsigned char)*e->s))
		e->ty = J_NUM;
	else
		j->bad = 1;
}

/* Split a jq-style path into subscript keys. */
int j_path(sh *s, const char *p, char ***out)
{
	vec *k = vb_get(s);
	str b;
	char **r;
	int n;

	s_init(&b);
	while (*p) {
		if (*p == '.') {
			p++;
			continue;
		}
		if (*p == '[') {
			p++;
			b.n = 0;
			while (*p && *p != ']')
				s_ch(&b, *p++);
			if (*p == ']')
				p++;
			v_add(k, ar_dup(s->xa, b.p ? b.p : "", b.n));
			continue;
		}
		b.n = 0;
		while (*p && *p != '.' && *p != '[')
			s_ch(&b, *p++);
		v_add(k, ar_dup(s->xa, b.p ? b.p : "", b.n));
	}
	s_free(&b);
	n = (int)k->n;
	r = ar_alloc(s->xa, ((size_t)n + 1) * sizeof *r);
	memcpy(r, k->p, (size_t)n * sizeof *r);
	vb_put(s, k);
	*out = r;
	return n;
}

/* Decide whether a map should print as an array. */
int j_isarr(ent *m, size_t n, short ty)
{
	size_t i = 0;
	ent *e;

	if (ty == J_ARR)
		return 1;
	if (ty == J_OBJ)
		return 0;
	for (e = m; e; e = e->nx, i++) {
		const char *k = e->k;
		size_t d = 0;
		while (k[d] && isdigit((unsigned char)k[d]))
			d++;
		if (!d || k[d] || (size_t)atol(e->k) != i)
			return 0;
	}
	return n > 0;
}

/* Append a JSON-quoted string. */
void j_quote(str *o, const char *p)
{
	s_ch(o, '"');
	for (; *p; p++) {
		switch (*p) {
		case '"': s_cat(o, "\\\""); break;
		case '\\': s_cat(o, "\\\\"); break;
		case '\n': s_cat(o, "\\n"); break;
		case '\t': s_cat(o, "\\t"); break;
		case '\r': s_cat(o, "\\r"); break;
		default:
			if ((unsigned char)*p < 0x20) {
				char *t = xm(8);
				sprintf(t, "\\u%04x", (unsigned char)*p);
				s_cat(o, t);
				free(t);
			} else {
				s_ch(o, *p);
			}
		}
	}
	s_ch(o, '"');
}

/* Write one value, recursing into containers. */
void j_out(str *o, ent *m, size_t n, const char *sc, short ty, int pretty,
	   int depth)
{
	ent *e;
	int arr;
	int i;

	if (!m) {
		if (ty == J_NUM || ty == J_BOOL || ty == J_NULL)
			s_cat(o, sc && *sc ? sc : "null");
		else if (ty == J_ARR)
			s_cat(o, "[]");
		else if (ty == J_OBJ)
			s_cat(o, "{}");
		else
			j_quote(o, sc ? sc : "");
		return;
	}
	arr = j_isarr(m, n, ty);
	s_ch(o, arr ? '[' : '{');
	for (e = m; e; e = e->nx) {
		if (pretty) {
			s_ch(o, '\n');
			for (i = 0; i <= depth; i++)
				s_cat(o, "  ");
		}
		if (!arr) {
			j_quote(o, e->k);
			s_ch(o, ':');
			if (pretty)
				s_ch(o, ' ');
		}
		j_out(o, e->map, e->n, e->s, e->ty, pretty, depth + 1);
		if (e->nx)
			s_ch(o, ',');
	}
	if (pretty && m) {
		s_ch(o, '\n');
		for (i = 0; i < depth; i++)
			s_cat(o, "  ");
	}
	s_ch(o, arr ? ']' : '}');
}

/* Name a JSON type. */
const char *j_tname(short ty, ent *m)
{
	if (m)
		return ty == J_ARR ? "array" : "object";
	switch (ty) {
	case J_NUM: return "number";
	case J_BOOL: return "boolean";
	case J_NULL: return "null";
	case J_ARR: return "array";
	case J_OBJ: return "object";
	}
	return "string";
}

/* Guess the JSON type of a literal. */
short j_guess(const char *v)
{
	const char *p = v;

	if (!strcmp(v, "true") || !strcmp(v, "false"))
		return J_BOOL;
	if (!strcmp(v, "null"))
		return J_NULL;
	if (*p == '-')
		p++;
	if (!*p)
		return J_STR;
	for (; *p; p++)
		if (!isdigit((unsigned char)*p) && *p != '.' && *p != 'e' &&
		    *p != 'E' && *p != '+' && *p != '-')
			return J_STR;
	return J_NUM;
}

/* Read every byte of a stream. */
char *j_slurp(FILE *f)
{
	str b;
	char *buf = xm(HIBR_IOCH);
	size_t n;

	s_init(&b);
	while ((n = fread(buf, 1, HIBR_IOCH, f)) > 0)
		s_add(&b, buf, n);
	free(buf);
	if (!b.p)
		s_cat(&b, "");
	return b.p;
}

/* Parse, query, edit and print JSON documents. */
int b_json(sh *s, int ac, char **av)
{
	struct jp j;
	var *v;
	ent *e, root;
	char **ks;
	int nk, pretty = 0, i = 2, force = 0;
	char *txt;
	str o;
	const char *sub = ac > 1 ? av[1] : "";

	if (ac < 3) {
		lg(HIBR_LERR, "usage: json parse|get|set|emit|keys|len|type var ...");
		return 2;
	}
	if (!strcmp(sub, "parse")) {
		txt = ac > 3 ? xs(av[3]) : j_slurp(stdin);
		memset(&root, 0, sizeof root);
		j.p = txt;
		j.bad = 0;
		j_val(&j, &root);
		if (j.bad) {
			lg(HIBR_LERR, "json: malformed input near '%.16s'", j.p);
			mp_free(root.map);
			free(root.s);
			free(txt);
			return HIBR_FAIL;
		}
		hibr_set(s, av[2], root.s ? root.s : "", 0);
		v = v_find(s, av[2]);
		if (v) {
			v_free_el(v);
			v->map = root.map;
			v->n = root.n;
			v->ty = root.ty;
			v->am = root.map ? 1 : 0;
		} else {
			mp_free(root.map);
		}
		free(root.s);
		free(txt);
		lg(HIBR_LDBG, "json parsed into %s", av[2]);
		return HIBR_OK;
	}
	v = v_find(s, av[2]);
	if (!strcmp(sub, "emit")) {
		for (; i < ac; i++)
			if (!strcmp(av[i], "-p"))
				pretty = 1;
		if (!v) {
			lg(HIBR_LERR, "json: %s is not set", av[2]);
			return HIBR_FAIL;
		}
		s_init(&o);
		j_out(&o, v->map, v->n, v->v, v->ty, pretty, 0);
		printf("%s\n", o.p ? o.p : "null");
		s_free(&o);
		return HIBR_OK;
	}
	if (ac < 4) {
		lg(HIBR_LERR, "json %s: a path is required", sub);
		return 2;
	}
	nk = j_path(s, av[3], &ks);
	if (!strcmp(sub, "set")) {
		if (ac < 5) {
			lg(HIBR_LERR, "json set: a value is required");
			return 2;
		}
		for (i = 5; i < ac; i++)
			if (!strcmp(av[i], "-s"))
				force = 1;
		v_setp(s, av[2], ks, nk, av[4]);
		e = v_path(s, av[2], ks, nk, 0);
		if (e)
			e->ty = force ? J_STR : j_guess(av[4]);
		return HIBR_OK;
	}
	if (!v) {
		lg(HIBR_LERR, "json: %s is not set", av[2]);
		return HIBR_FAIL;
	}
	e = nk ? v_path(s, av[2], ks, nk, 0) : 0;
	if (nk && !e)
		return HIBR_FAIL;
	if (!strcmp(sub, "get")) {
		const char *sc = e ? e->s : v->v;
		ent *m = e ? e->map : v->map;
		size_t n = e ? e->n : v->n;
		short ty = e ? e->ty : v->ty;
		s_init(&o);
		if (m)
			j_out(&o, m, n, sc, ty, 0, 0);
		else
			s_cat(&o, sc ? sc : "");
		if (ac > 4 && av[4][0] != '-')
			hibr_set(s, av[4], o.p ? o.p : "", 0);
		else if (!s->bind)
			printf("%s\n", o.p ? o.p : "");
		hibr_set(s, "RET", o.p ? o.p : "", 0);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "keys")) {
		ent *m = e ? e->map : v->map;
		s_init(&o);
		for (; m; m = m->nx) {
			if (o.n)
				s_ch(&o, ' ');
			s_cat(&o, m->k);
		}
		if (ac > 4)
			hibr_set(s, av[4], o.p ? o.p : "", 0);
		else
			printf("%s\n", o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "len")) {
		printf("%lu\n", (unsigned long)(e ? e->n : v->n));
		return HIBR_OK;
	}
	if (!strcmp(sub, "type")) {
		printf("%s\n", e ? j_tname(e->ty, e->map) : j_tname(v->ty, v->map));
		return HIBR_OK;
	}
	lg(HIBR_LERR, "json: %s: unknown subcommand", sub);
	return 2;
}
