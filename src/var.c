#include "pri.h"
#include <stdlib.h>
#include <string.h>

extern char **environ;

/* FNV-1a hash of a variable name. */
unsigned vh(const char *k)
{
	unsigned h = 2166136261u;

	while (*k) {
		h ^= (unsigned char)*k++;
		h *= 16777619u;
	}
	return h;
}

/* Locate a variable record or NULL. */
var *v_find(sh *s, const char *k)
{
	var *v;

	if (!s->tab)
		return 0;
	for (v = s->tab[vh(k) & (s->tsz - 1)]; v; v = v->nx)
		if (!strcmp(v->k, k))
			return v;
	return 0;
}

/* Grow and rehash the variable table. */
void v_grow(sh *s)
{
	size_t ns = s->tsz ? s->tsz * 2 : HIBR_TAB0;
	var **nt = xm(ns * sizeof *nt);
	var *v, *n;
	size_t i;

	memset(nt, 0, ns * sizeof *nt);
	for (i = 0; i < s->tsz; i++)
		for (v = s->tab[i]; v; v = n) {
			unsigned b = vh(v->k) & (ns - 1);
			n = v->nx;
			v->nx = nt[b];
			nt[b] = v;
		}
	free(s->tab);
	s->tab = nt;
	s->tsz = ns;
	lg(HIBR_LDBG, "var table now %lu slots", (unsigned long)ns);
}

/* Read a variable value or NULL. */
const char *hibr_get(sh *s, const char *k)
{
	var *v = v_find(s, k);

	return v ? v->v : 0;
}

/* Store a variable value, optionally marking it exported. */
int hibr_set(sh *s, const char *k, const char *v, int ex)
{
	var *e;
	unsigned b;

	if (!s->tab || s->tn * 4 >= s->tsz * 3)
		v_grow(s);
	e = v_find(s, k);
	if (e) {
		if (e->ro) {
			lg(HIBR_LERR, "%s: readonly variable", k);
			return HIBR_FAIL;
		}
		v_free_el(e);
		free(e->v);
		e->v = xs(v);
		if (ex)
			e->ex = 1;
		return HIBR_OK;
	}
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->k = xs(k);
	e->v = xs(v);
	e->ex = ex ? 1 : 0;
	b = vh(k) & (s->tsz - 1);
	e->nx = s->tab[b];
	s->tab[b] = e;
	s->tn++;
	return HIBR_OK;
}

/* Remove a variable. */
void v_del(sh *s, const char *k)
{
	var **pp;
	var *v;

	if (!s->tab)
		return;
	pp = &s->tab[vh(k) & (s->tsz - 1)];
	while ((v = *pp)) {
		if (!strcmp(v->k, k)) {
			*pp = v->nx;
			v_free_el(v);
			free(v->k);
			free(v->v);
			free(v);
			s->tn--;
			return;
		}
		pp = &v->nx;
	}
}

/* Import the process environment into the variable table. */
void v_env(sh *s)
{
	char **e;
	char *q;
	size_t n;
	char *k;

	for (e = environ; e && *e; e++) {
		q = strchr(*e, '=');
		if (!q)
			continue;
		n = (size_t)(q - *e);
		k = xm(n + 1);
		memcpy(k, *e, n);
		k[n] = 0;
		hibr_set(s, k, q + 1, 1);
		free(k);
	}
	lg(HIBR_LDBG, "imported %lu environment entries", (unsigned long)s->tn);
}

/* Collect the names of every set variable starting with a prefix. */
void v_names(sh *s, const char *pre, vec *out)
{
	size_t i, n = strlen(pre);
	var *v;

	for (i = 0; i < s->tsz; i++)
		for (v = s->tab[i]; v; v = v->nx)
			if (!strncmp(v->k, pre, n))
				v_add(out, xs(v->k));
	lg(HIBR_LTRC, "%lu names start with '%s'", (unsigned long)out->n,
	   pre);
}

/* Build a NULL terminated envp from exported variables plus extras. */
char **v_envp(sh *s, vec *extra)
{
	vec o = { 0, 0, 0 };
	size_t i;
	var *v;
	char **r;
	str b;

	for (i = 0; i < s->tsz; i++)
		for (v = s->tab[i]; v; v = v->nx) {
			if (!v->ex)
				continue;
			s_init(&b);
			s_cat(&b, v->k);
			s_ch(&b, '=');
			s_cat(&b, v->v);
			v_add(&o, b.p);
		}
	if (extra)
		for (i = 0; i < extra->n; i++)
			v_add(&o, xs((char *)extra->p[i]));
	v_add(&o, 0);
	r = (char **)o.p;
	return r;
}

/* Replace the positional parameters. */
void v_pos(sh *s, int ac, char **av)
{
	char **n = xm(((size_t)ac + 1) * sizeof *n);
	int i;

	for (i = 0; i < ac; i++)
		n[i] = xs(av[i]);
	n[ac] = 0;
	if (s->avo && s->av) {
		for (i = 0; i < s->ac; i++)
			free(s->av[i]);
		free(s->av);
	}
	s->av = n;
	s->ac = ac;
	s->avo = 1;
}

/* Find an entry by key. */
ent *mp_find(ent *m, const char *k)
{
	for (; m; m = m->nx)
		if (!strcmp(m->k, k))
			return m;
	return 0;
}

/* Find an entry by key, appending it when absent. */
ent *mp_add(ent **m, size_t *n, const char *k)
{
	ent *e = mp_find(*m, k), **t = m;

	if (e)
		return e;
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->k = xs(k);
	while (*t)
		t = &(*t)->nx;
	*t = e;
	(*n)++;
	return e;
}

/* Release a map and everything below it. */
void mp_free(ent *m)
{
	ent *n;

	while (m) {
		n = m->nx;
		mp_free(m->map);
		free(m->k);
		free(m->s);
		free(m);
		m = n;
	}
}

/* Release a variable's map. */
void v_free_el(var *v)
{
	mp_free(v->map);
	v->map = 0;
	v->n = 0;
}

/* Walk a subscript path, optionally creating the levels it crosses. */
ent *v_path(sh *s, const char *nm, char **ks, int nk, int make)
{
	var *v = v_find(s, nm);
	ent *e = 0;
	ent **mp;
	size_t *np;
	int i;

	if (!v) {
		if (!make)
			return 0;
		hibr_set(s, nm, "", 0);
		v = v_find(s, nm);
		if (!v)
			return 0;
	}
	mp = &v->map;
	np = &v->n;
	for (i = 0; i < nk; i++) {
		e = make ? mp_add(mp, np, ks[i]) : mp_find(*mp, ks[i]);
		if (!e)
			return 0;
		mp = &e->map;
		np = &e->n;
	}
	return e;
}

/* Read the scalar at a subscript path. */
const char *v_getp(sh *s, const char *nm, char **ks, int nk)
{
	var *v;
	ent *e;

	if (!nk) {
		v = v_find(s, nm);
		return v ? v->v : 0;
	}
	e = v_path(s, nm, ks, nk, 0);
	return e ? e->s : 0;
}

/* Assign the scalar at a subscript path, creating levels as needed. */
void v_setp(sh *s, const char *nm, char **ks, int nk, const char *val)
{
	ent *e;
	var *v;

	if (!nk) {
		hibr_set(s, nm, val, 0);
		return;
	}
	e = v_path(s, nm, ks, nk, 1);
	if (!e)
		return;
	{
		var *vv = v_find(s, nm);
		if (vv)
			vv->am = 1;
	}
	mp_free(e->map);
	e->map = 0;
	e->n = 0;
	free(e->s);
	e->s = xs(val);
	if (nk == 1 && !strcmp(ks[0], "0")) {
		v = v_find(s, nm);
		if (v) {
			free(v->v);
			v->v = xs(val);
		}
	}
	lg(HIBR_LTRC, "set %s depth %d", nm, nk);
}

/* Count the entries directly below a subscript path. */
size_t v_count(sh *s, const char *nm, char **ks, int nk)
{
	var *v = v_find(s, nm);
	ent *e;

	if (!v)
		return 0;
	if (!nk)
		return v->map || v->am ? v->n : 1;
	e = v_path(s, nm, ks, nk, 0);
	if (!e)
		return 0;
	return e->map ? e->n : 1;
}

/* Collect the keys or the values directly below a subscript path. */
void v_list(sh *s, const char *nm, char **ks, int nk, vec *out, int keys)
{
	var *v = v_find(s, nm);
	ent *m, *e;

	if (!v)
		return;
	if (!nk) {
		m = v->map;
	} else {
		e = v_path(s, nm, ks, nk, 0);
		if (!e)
			return;
		m = e->map;
		if (!m) {
			if (!keys && e->s)
				v_add(out, e->s);
			return;
		}
	}
	if (!m) {
		if (!keys && v->v && !v->am)
			v_add(out, v->v);
		return;
	}
	for (e = m; e; e = e->nx)
		v_add(out, keys ? e->k : (e->s ? e->s : ""));
}

/* Replace a variable with an array of values. */
void v_arr(sh *s, const char *k, vec *vals)
{
	var *e;
	size_t i;
	str ix;
	char *kv, *q;

	hibr_set(s, k, "", 0);
	e = v_find(s, k);
	if (!e)
		return;
	v_free_el(e);
	e->am = 1;
	for (i = 0; i < vals->n; i++) {
		kv = (char *)vals->p[i];
		q = 0;
		if (kv[0] == '[' && (q = strstr(kv, "]=")) != 0) {
			*q = 0;
			{
				char *key = kv + 1;
				v_setp(s, k, &key, 1, q + 2);
			}
			*q = ']';
			continue;
		}
		s_init(&ix);
		s_num(&ix, (long)i);
		{
			char *key = ix.p;
			v_setp(s, k, &key, 1, kv);
		}
		s_free(&ix);
	}
	lg(HIBR_LDBG, "map %s has %lu entries", k, (unsigned long)e->n);
}

/* Assign one element of an array, growing it as needed. */
void v_setel(sh *s, const char *k, long i, const char *val)
{
	str ix;
	char *key;

	s_init(&ix);
	s_num(&ix, i);
	key = ix.p;
	v_setp(s, k, &key, 1, val);
	s_free(&ix);
}

/* Read one element of an array. */
const char *v_getel(sh *s, const char *k, long i)
{
	str ix;
	char *key;
	const char *r;

	s_init(&ix);
	s_num(&ix, i);
	key = ix.p;
	r = v_getp(s, k, &key, 1);
	s_free(&ix);
	return r;
}

/* Report how many elements an array holds. */
size_t v_alen(sh *s, const char *k)
{
	return v_count(s, k, 0, 0);
}

/* Clone a map recursively. */
ent *m_clone(ent *m)
{
	ent *h = 0, **t = &h, *e;

	for (; m; m = m->nx) {
		e = xm(sizeof *e);
		memset(e, 0, sizeof *e);
		e->k = xs(m->k);
		e->s = m->s ? xs(m->s) : 0;
		e->map = m_clone(m->map);
		e->n = m->n;
		*t = e;
		t = &e->nx;
	}
	return h;
}

/* Copy one variable, map and all, onto another name. */
void v_copy(sh *s, const char *dst, const char *src)
{
	var *a = v_find(s, src), *b;

	hibr_set(s, dst, a && a->v ? a->v : "", 0);
	b = v_find(s, dst);
	if (!a || !b)
		return;
	v_free_el(b);
	b->map = m_clone(a->map);
	b->n = a->n;
	b->am = a->am;
	lg(HIBR_LTRC, "copied %s to %s", src, dst);
}

/* Public map access for modules. */
const char *hibr_getp(sh *s, const char *nm, char **ks, int nk)
{
	return v_getp(s, nm, ks, nk);
}

/* Public map assignment for modules. */
void hibr_setp(sh *s, const char *nm, char **ks, int nk, const char *v)
{
	v_setp(s, nm, ks, nk, v);
}

/* Public entry count for modules. */
size_t hibr_count(sh *s, const char *nm, char **ks, int nk)
{
	return v_count(s, nm, ks, nk);
}

/* Public key or value listing for modules. */
void hibr_list(sh *s, const char *nm, char **ks, int nk, vec *out, int keys)
{
	v_list(s, nm, ks, nk, out, keys);
}

/* Place a scalar in the result slot. */
void hibr_ret(sh *s, const char *v)
{
	hibr_set(s, "RET", v ? v : "", 0);
}

/* Place several values in the result slot as an array. */
void hibr_retn(sh *s, char **vals, size_t n)
{
	vec o = { 0, 0, 0 };
	size_t i;

	for (i = 0; i < n; i++)
		v_add(&o, vals[i]);
	v_arr(s, "RET", &o);
	v_free(&o);
}

/* Record an error message the way fail does. */
void hibr_fail(sh *s, const char *msg)
{
	hibr_set(s, "ERRMSG", msg ? msg : "", 0);
}

/* Remove the entry at a subscript path. */
int v_delp(sh *s, const char *nm, char **ks, int nk)
{
	var *v = v_find(s, nm);
	ent **mp, *e, **pp;
	size_t *np;
	int i;

	if (!v || nk < 1)
		return HIBR_FAIL;
	mp = &v->map;
	np = &v->n;
	for (i = 0; i < nk - 1; i++) {
		e = mp_find(*mp, ks[i]);
		if (!e)
			return HIBR_FAIL;
		mp = &e->map;
		np = &e->n;
	}
	for (pp = mp; *pp; pp = &(*pp)->nx) {
		if (strcmp((*pp)->k, ks[nk - 1]))
			continue;
		e = *pp;
		*pp = e->nx;
		mp_free(e->map);
		free(e->k);
		free(e->s);
		free(e);
		(*np)--;
		return HIBR_OK;
	}
	return HIBR_FAIL;
}
