#define _GNU_SOURCE

#include "hibr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct la_ent la_ent;
struct la_ent {
	char *k, *v;
	char *pf[6];
};

static la_ent *la_tab;
static size_t la_cap, la_n;
static char *la_code, *la_name, *la_dir, *la_rule;

int sx_out(sh *s, const char *nm, const char *v);
void u8put(str *o, unsigned c);

static const char *la_cats[6] = { "zero", "one", "two", "few", "many", "other" };

/* FNV-1a of a key. */
size_t la_hash(const char *k)
{
	size_t h = 2166136261u;

	for (; *k; k++)
		h = (h ^ (unsigned char)*k) * 16777619u;
	return h;
}

/* The slot a key lives in, or the empty one it would take. */
la_ent *la_slot(const char *k)
{
	size_t i = la_hash(k) & (la_cap - 1);

	while (la_tab[i].k && strcmp(la_tab[i].k, k))
		i = (i + 1) & (la_cap - 1);
	return &la_tab[i];
}

/* Forget the catalogue. */
void la_clear(void)
{
	size_t i;
	int j;

	for (i = 0; i < la_cap; i++) {
		free(la_tab[i].k);
		free(la_tab[i].v);
		for (j = 0; j < 6; j++)
			free(la_tab[i].pf[j]);
	}
	free(la_tab);
	la_tab = 0;
	la_cap = la_n = 0;
	free(la_code);
	free(la_name);
	free(la_dir);
	free(la_rule);
	la_code = la_name = la_dir = la_rule = 0;
}

/* Make room for one more entry, doubling the table at half full. */
void la_grow(void)
{
	la_ent *old = la_tab;
	size_t oc = la_cap, i;

	if (la_cap && (la_n + 1) * 2 <= la_cap)
		return;
	la_cap = la_cap ? la_cap * 2 : 256;
	la_tab = xm(sizeof *la_tab * la_cap);
	memset(la_tab, 0, sizeof *la_tab * la_cap);
	for (i = 0; i < oc; i++)
		if (old[i].k)
			*la_slot(old[i].k) = old[i];
	free(old);
}

/* Skip blanks in JSON text. */
const char *la_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/* Four hex digits as a number, or 0 if they are not. */
unsigned la_hex4(const char *p)
{
	unsigned c = 0, d;
	int i;

	for (i = 0; i < 4; i++) {
		d = (unsigned char)p[i];
		if (d >= '0' && d <= '9')
			c = c * 16 + d - '0';
		else if ((d | 32) >= 'a' && (d | 32) <= 'f')
			c = c * 16 + (d | 32) - 'a' + 10;
		else
			return 0;
	}
	return c;
}

/* Read a JSON string at p into o; returns past it, or NULL. */
const char *la_str(const char *p, str *o)
{
	unsigned c, d;
	int i;

	o->n = 0;
	if (*p != '"')
		return 0;
	for (p++; *p && *p != '"'; p++) {
		if (*p != '\\') {
			s_ch(o, *p);
			continue;
		}
		switch (*++p) {
		case 'n': s_ch(o, '\n'); break;
		case 't': s_ch(o, '\t'); break;
		case 'r': s_ch(o, '\r'); break;
		case 'b': s_ch(o, '\b'); break;
		case 'f': s_ch(o, '\f'); break;
		case 'u':
			for (c = 0, i = 1; i <= 4; i++) {
				d = (unsigned char)p[i];
				if (d >= '0' && d <= '9')
					c = c * 16 + d - '0';
				else if ((d | 32) >= 'a' && (d | 32) <= 'f')
					c = c * 16 + (d | 32) - 'a' + 10;
				else
					return 0;
			}
			p += 4;
			if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
				unsigned lo = la_hex4(p + 3);
				if (lo >= 0xDC00 && lo < 0xE000) {
					c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
					p += 6;
				}
			}
			u8put(o, c);
			break;
		case 0: return 0;
		default: s_ch(o, *p);
		}
	}
	if (*p != '"')
		return 0;
	s_grow(o, 1);
	o->p[o->n] = 0;
	return p + 1;
}

/* A copy of a str's text. */
char *la_dup(str *t)
{
	return xs(t->p ? t->p : "");
}

/* Read the "strings" object: each key a string, or an object of plural forms. */
const char *la_strings(const char *p, str *k, str *v)
{
	la_ent *e;
	int j;

	p = la_ws(p);
	if (*p != '{')
		return 0;
	p = la_ws(p + 1);
	while (*p && *p != '}') {
		if (!(p = la_str(p, k)))
			return 0;
		p = la_ws(p);
		if (*p != ':')
			return 0;
		p = la_ws(p + 1);
		la_grow();
		e = la_slot(k->p);
		if (!e->k) {
			e->k = la_dup(k);
			la_n++;
		}
		if (*p == '"') {
			if (!(p = la_str(p, v)))
				return 0;
			free(e->v);
			e->v = la_dup(v);
		} else if (*p == '{') {
			p = la_ws(p + 1);
			while (*p && *p != '}') {
				if (!(p = la_str(p, v)))
					return 0;
				for (j = 0; j < 6 && strcmp(v->p, la_cats[j]); j++)
					;
				p = la_ws(p);
				if (*p != ':')
					return 0;
				p = la_ws(p + 1);
				if (!(p = la_str(p, v)))
					return 0;
				if (j < 6) {
					free(e->pf[j]);
					e->pf[j] = la_dup(v);
				}
				p = la_ws(p);
				if (*p == ',')
					p = la_ws(p + 1);
			}
			if (*p != '}')
				return 0;
			p++;
		} else
			return 0;
		p = la_ws(p);
		if (*p == ',')
			p = la_ws(p + 1);
	}
	return *p == '}' ? p + 1 : 0;
}

/* Load a catalogue: {"language", "name", "dir", "plural", "strings": {...}}. */
int la_load(const char *path)
{
	FILE *f = fopen(path, "r");
	str t, k, v;
	const char *p;
	int ok = 0;
	char **slot;

	if (!f) {
		lg(HIBR_LERR, "lang load: %s: cannot be read", path);
		return HIBR_FAIL;
	}
	s_init(&t);
	s_init(&k);
	s_init(&v);
	for (;;) {
		s_grow(&t, HIBR_IOCH);
		size_t r = fread(t.p + t.n, 1, t.cap - t.n - 1, f);
		if (!r)
			break;
		t.n += r;
	}
	fclose(f);
	s_grow(&t, 1);
	t.p[t.n] = 0;
	la_clear();
	p = la_ws(t.p);
	if (*p != '{')
		goto out;
	p = la_ws(p + 1);
	while (*p && *p != '}') {
		if (!(p = la_str(p, &k)))
			goto out;
		p = la_ws(p);
		if (*p != ':')
			goto out;
		p = la_ws(p + 1);
		if (!strcmp(k.p, "strings")) {
			if (!(p = la_strings(p, &k, &v)))
				goto out;
		} else {
			if (!(p = la_str(p, &v)))
				goto out;
			slot = !strcmp(k.p, "language") ? &la_code : !strcmp(k.p, "name") ? &la_name :
			       !strcmp(k.p, "dir") ? &la_dir : !strcmp(k.p, "plural") ? &la_rule : 0;
			if (slot) {
				free(*slot);
				*slot = la_dup(&v);
			}
		}
		p = la_ws(p);
		if (*p == ',')
			p = la_ws(p + 1);
	}
	ok = *p == '}' && la_code;
out:
	s_free(&t);
	s_free(&k);
	s_free(&v);
	if (!ok) {
		la_clear();
		lg(HIBR_LERR, "lang load: %s: not a catalogue", path);
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* The CLDR cardinal category of a whole number n under a language's rule. */
int la_cat(const char *rule, long n)
{
	long m10 = n % 10, m100 = n % 100;

	if (!rule)
		rule = "";
	if (!strcmp(rule, "ar"))
		return n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 2 : m100 >= 3 && m100 <= 10 ? 3 :
		       m100 >= 11 && m100 <= 99 ? 4 : 5;
	if (!strcmp(rule, "he"))
		return n == 1 ? 1 : n == 2 ? 2 : 5;
	if (!strcmp(rule, "fr") || !strcmp(rule, "fa") || !strcmp(rule, "hi"))
		return n == 0 || n == 1 ? 1 : 5;
	if (!strcmp(rule, "ru") || !strcmp(rule, "uk"))
		return m10 == 1 && m100 != 11 ? 1 :
		       m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14) ? 3 : 4;
	if (!strcmp(rule, "pl"))
		return n == 1 ? 1 : m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14) ? 3 : 4;
	if (!strcmp(rule, "ja") || !strcmp(rule, "zh") || !strcmp(rule, "ko"))
		return 5;
	return n == 1 ? 1 : 5;
}

/* Fill a translation's %s, %d and %N$s from args, %% as itself. */
void la_fill(const char *t, int ac, char **av, str *o)
{
	int next = 0, n;
	const char *p;

	o->n = 0;
	for (p = t; *p; p++) {
		if (*p != '%' || !p[1]) {
			s_ch(o, *p);
			continue;
		}
		if (p[1] == '%') {
			s_ch(o, '%');
			p++;
			continue;
		}
		if (p[1] >= '1' && p[1] <= '9' && p[2] == '$' && (p[3] == 's' || p[3] == 'd')) {
			n = p[1] - '1';
			if (n < ac)
				s_cat(o, av[n]);
			p += 3;
			continue;
		}
		if (p[1] == 's' || p[1] == 'd') {
			if (next < ac)
				s_cat(o, av[next]);
			next++;
			p++;
			continue;
		}
		s_ch(o, *p);
	}
	s_grow(o, 1);
	o->p[o->n] = 0;
}

/* The translation of a key, or the key itself. */
const char *la_look(const char *k)
{
	la_ent *e;

	if (!la_cap)
		return k;
	e = la_slot(k);
	return e->k && e->v ? e->v : k;
}

/* lang load|get|plural|has|info|keys|off ... */
int la_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "", *t;
	str o;
	la_ent *e;
	size_t i;
	int c, rc;
	long n;

	if (!strcmp(sub, "load") && ac == 3)
		return la_load(av[2]);
	if (!strcmp(sub, "off")) {
		la_clear();
		return HIBR_OK;
	}
	if (!strcmp(sub, "has") && ac == 3)
		return la_cap && la_slot(av[2])->k ? HIBR_OK : HIBR_FAIL;
	s_init(&o);
	if (!strcmp(sub, "get") && ac >= 3) {
		la_fill(la_look(av[2]), ac - 3, av + 3, &o);
	} else if (!strcmp(sub, "plural") && ac >= 4) {
		n = atol(av[2]);
		t = av[3];
		if (la_cap && (e = la_slot(av[3]))->k) {
			c = la_cat(la_rule, n);
			t = e->pf[c] ? e->pf[c] : e->pf[5] ? e->pf[5] : e->v ? e->v : av[3];
		}
		la_fill(t, ac - 4, av + 4, &o);
	} else if (!strcmp(sub, "info")) {
		s_cat(&o, la_code ? la_code : "");
		s_ch(&o, '\t');
		s_cat(&o, la_name ? la_name : "");
		s_ch(&o, '\t');
		s_cat(&o, la_dir ? la_dir : "ltr");
		s_ch(&o, '\t');
		s_num(&o, (long)la_n);
	} else if (!strcmp(sub, "keys")) {
		for (i = 0; i < la_cap; i++)
			if (la_tab[i].k)
				printf("%s\n", la_tab[i].k);
		s_free(&o);
		return HIBR_OK;
	} else {
		s_free(&o);
		lg(HIBR_LERR, "usage: lang load file | get key [arg...] | plural n key [arg...] | has key | info | keys | off");
		return 2;
	}
	s_grow(&o, 1);
	o.p[o.n] = 0;
	rc = sx_out(s, 0, o.p);
	s_free(&o);
	return rc;
}

/* Forget the catalogue when the module goes. */
void la_fini(sh *s)
{
	(void)s;
	la_clear();
}

const hibr_bi la_bis[] = {
	{ "lang", la_bi, "a translation catalogue: lang load|get|plural|has|info|keys|off" },
	HIBR_BI_END
};

HIBR_MODULE("lang", "1.0", "translation catalogues, with CLDR plural rules",
	    la_bis, 0, la_fini);
