#include "pri.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

extern char **environ;

int v_refused;

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

/* A variable the shell keeps a copy of has been set or removed: IFS, FUNCNEST. Asked by its first letter, so any other name pays one comparison. */
void v_named(sh *s, const char *k)
{
	if (k[0] == 'I' && !strcmp(k, "IFS"))
		s->ifsok = 0;
	else if (k[0] == 'F' && !strcmp(k, "FUNCNEST"))
		ex_fnok = 0;
	else if (k[0] == 'H' && !strcmp(k, "HIBR_MODULES"))
		m_ldmode = -1;
}

/* Read a variable value or NULL. */
const char *hibr_get(sh *s, const char *k)
{
	var *v = v_find(s, k);
	int hop = 0;

	while (v && (v->at & A_REF) && v->v && *v->v && hop++ < 16)
		v = v_find(s, v->v);
	return v ? v->v : 0;
}

/* The declared type names, in the order their codes use. */
const char *v_tynames[] = { 0, "int", "num", "str", "path", "arr", "map",
			    "any", 0 };

/* The code for a declared type name, or zero when it is not one. */
unsigned v_tycode(const char *nm)
{
	unsigned i;

	for (i = 1; v_tynames[i]; i++)
		if (!strcmp(nm, v_tynames[i]))
			return i;
	return 0;
}

/* The declared type of a variable, or an empty string when it has none. */
const char *v_tyname(unsigned at)
{
	unsigned c = (at & A_TYMASK) >> A_TYSH;

	return c && v_tynames[c] ? v_tynames[c] : "";
}

/* Apply a variable's declared attributes to a value about to be stored. */
const char *v_coerce(sh *s, var *e, const char *v, str *tmp)
{
	const char *ty = v_tyname(e->at);

	if (e->at & (A_LOW | A_UPP)) {
		tmp->n = 0;
		u8cased(tmp, v, (e->at & A_UPP) != 0, 0);
		v = tmp->p ? tmp->p : "";
	}
	if (e->at & A_INT) {
		s_num(tmp, ax_run(s, v));
		lg(HIBR_LTRC, "%s takes %s as %s", e->k, v, tmp->p);
		return tmp->p;
	}
	if (*ty && !ty_ok(ty, v)) {
		lg(HIBR_LERR, "%s: declared %s, got '%s'", e->k, ty, v);
		v_refused = 1;
		return 0;
	}
	return v;
}

/* The value of IFS, looked up once and kept until it is assigned. */
const char *sh_ifs(sh *s)
{
	if (!s->ifsok) {
		s->ifsc = hibr_get(s, "IFS");
		s->ifsok = 1;
	}
	return s->ifsc;
}

/* Seed RANDOM, restart SECONDS, ignore the EPOCH clocks: store none. */
int v_dyn(sh *s, const char *k, const char *v)
{
	if (!strcmp(k, "RANDOM")) {
		srand((unsigned)strtol(v, 0, 10));
		return 1;
	}
	if (!strcmp(k, "SECONDS")) {
		s->t0 = (long)time(0) - strtol(v, 0, 10);
		return 1;
	}
	if (!strcmp(k, "EPOCHSECONDS") || !strcmp(k, "EPOCHREALTIME")) {
		lg(HIBR_LDBG, "%s is a clock; the assignment is ignored", k);
		return 1;
	}
	return 0;
}

/* Names the shell itself fills in -- a result, a match, what read or
   getopts found, a caught failure -- which a function sets without its
   author ever having written an assignment. */
int v_shellvar(const char *k)
{
	static const char *nm[] = { "RET", "M", "REPLY", "OPTARG", "OPTIND",
				    "ERR", "ERRMSG", "ERRSTATUS", "CMD",
				    "REMOTE", "PIPESTATUS", "MAPFILE",
				    "BASH_REMATCH", 0 };
	int i;

	for (i = 0; nm[i]; i++)
		if (!strcmp(k, nm[i]))
			return 1;
	return 0;
}

/* Whether strict vars refuses creating k here: in a function, from a file
   that asked, outside a declaration, and not a name the shell manages --
   nor a special parameter such as $!, which & sets. */
int v_strict(sh *s, const char *k)
{
	if (!(s->sfl & SF_VAR) || s->decl || s->scope.n <= s->srcdep ||
	    !(isalpha((unsigned char)*k) || *k == '_') || v_shellvar(k) ||
	    asg_local(s, k))
		return 0;
	lg(HIBR_LERR, "%s:%u: %s is not declared -- local %s, or declare -g "
	   "%s (strict vars)", sh_where(s), s->ln, k, k, k);
	s->srefuse = 1;
	return 1;
}

/* Store a variable value, optionally marking it exported. */
int hibr_set(sh *s, const char *k, const char *v, int ex)
{
	var *e;
	unsigned b;

	if ((k[0] == 'R' || k[0] == 'S' || k[0] == 'E') && v_dyn(s, k, v))
		return HIBR_OK;
	if (!s->tab || s->tn * 4 >= s->tsz * 3)
		v_grow(s);
	e = v_find(s, k);
	if (e) {
		str t;
		if (e->ro) {
			lg(HIBR_LERR, "%s: readonly variable", k);
			s->st = 1;
			v_refused = 1;
			return HIBR_FAIL;
		}
		if (e->at & A_REF)
			return hibr_set(s, e->v, v, ex);
		s_init(&t);
		v = v_coerce(s, e, v, &t);
		if (!v) {
			s_free(&t);
			s->st = 1;
			return HIBR_FAIL;
		}
		v_free_el(e);
		free(e->v);
		e->v = xs(v);
		if (ex)
			e->ex = 1;
		if (e->ex)
			setenv(k, e->v, 1);
		if (!strcmp(k, "PATH"))
			hsh_clear(s, 0);
		v_named(s, k);
		s_free(&t);
		return HIBR_OK;
	}
	if (s->sfl && v_strict(s, k)) {
		s->st = 1;
		return HIBR_FAIL;
	}
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->k = xs(k);
	e->v = xs(v);
	e->ex = ex ? 1 : 0;
	v_named(s, k);
	if (e->ex)
		setenv(k, e->v, 1);
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
			s->ifsok = 0;
			ex_fnok = 0;
			m_ldmode = -1;
			if (v->ex)
				unsetenv(k);
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

/* Lift a variable out of the table whole -- value, map, type and flags --
   so a local can shadow it and v_back can put it back exactly.  Copying the
   scalar instead lost every array a local of the same name ever hid.  The
   environment is left alone: until the local is given a value, a child
   still sees the outer one, as it does in bash. */
var *v_take(sh *s, const char *k)
{
	var **pp;
	var *v;

	if (!s->tab)
		return 0;
	pp = &s->tab[vh(k) & (s->tsz - 1)];
	while ((v = *pp)) {
		if (!strcmp(v->k, k)) {
			*pp = v->nx;
			v->nx = 0;
			s->tn--;
			v_named(s, k);
			if (!strcmp(k, "PATH"))
				hsh_clear(s, 0);
			return v;
		}
		pp = &v->nx;
	}
	return 0;
}

/* Put back a variable v_take lifted out, replacing whatever has the name. */
void v_back(sh *s, var *v)
{
	unsigned b;

	v_del(s, v->k);
	if (!s->tab || s->tn * 4 >= s->tsz * 3)
		v_grow(s);
	b = vh(v->k) & (s->tsz - 1);
	v->nx = s->tab[b];
	s->tab[b] = v;
	s->tn++;
	if (v->ex && v->v)
		setenv(v->k, v->v, 1);
	v_named(s, v->k);
	if (!strcmp(v->k, "PATH"))
		hsh_clear(s, 0);
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

/* True when a name is assigned by one of a command's own assignments. */
int v_shadowed(vec *extra, const char *k)
{
	size_t i, n = strlen(k);
	const char *e;

	for (i = 0; i < extra->n; i++) {
		e = (const char *)extra->p[i];
		if (!strncmp(e, k, n) && e[n] == '=')
			return 1;
	}
	return 0;
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
			if (extra && v_shadowed(extra, v->k)) {
				lg(HIBR_LTRC, "%s overridden for this command",
				   v->k);
				continue;
			}
			s_init(&b);
			s_cat(&b, v->k);
			s_ch(&b, '=');
			s_cat(&b, v->v);
			v_add(&o, b.p);
		}
	if (extra)
		for (i = 0; i < extra->n; i++)
			if (strchr((char *)extra->p[i], '='))
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

/* A map is a list, so a lookup walked it and an append walked it twice --
   5000 inserts took 155 ms and 20,000 took 2.7 s (Gitea #73). Past
   HIBR_MPHASH entries a chain keeps an index, and every chain keeps its own
   tail, both in its first entry: mp_find and mp_add are the only two
   callers that have to know, and nothing above them changed.

   The rules every path that changes a chain must keep: the head holds tl and
   ix; removing an entry takes it out of the index (a tombstone) and moves tl
   and ix to the new head when the head itself goes; and anything that frees
   or rebuilds a chain frees the index. mp_free, v_delp, m_clone and v_copyp
   are those paths. */

/* An index sized to hold n entries at three quarters load: a slot is eight
   bytes, so half load would cost 26 bytes an entry on top of the 16 the
   record grew, and memory is this project's first priority. At 0.75 the
   average probe is still under two. */
mix *mp_ixnew(size_t n)
{
	size_t cap = 32;
	mix *x;

	while (cap < n + n / 2)
		cap *= 2;
	x = xm(sizeof *x + cap * sizeof(ent *));
	memset(x, 0, sizeof *x + cap * sizeof(ent *));
	x->cap = cap;
	return x;
}

/* Put an entry in an index that has room for it. */
void mp_ixput(mix *x, ent *e)
{
	size_t i = vh(e->k) & (x->cap - 1);

	while (x->t[i] && x->t[i] != MP_GONE) {
		if (!strcmp(x->t[i]->k, e->k)) {
			x->t[i] = e;
			return;
		}
		i = (i + 1) & (x->cap - 1);
	}
	if (x->t[i] == MP_GONE)
		x->dead--;
	x->t[i] = e;
	x->n++;
}

/* Build or rebuild a chain's index from the chain itself. */
void mp_ixbuild(ent *h, size_t n)
{
	ent *e;

	if (!h)
		return;
	free(h->ix);
	h->ix = mp_ixnew(n);
	for (e = h; e; e = e->nx)
		mp_ixput(h->ix, e);
}

/* The entry a key names, through an index. */
ent *mp_ixfind(mix *x, const char *k)
{
	size_t i = vh(k) & (x->cap - 1), j;

	for (j = 0; j < x->cap; j++) {
		if (!x->t[i])
			return 0;
		if (x->t[i] != MP_GONE && !strcmp(x->t[i]->k, k))
			return x->t[i];
		i = (i + 1) & (x->cap - 1);
	}
	return 0;
}

/* Take an entry out of its chain's index, leaving a tombstone: the probe
   that found it has to go on finding what is past it. */
void mp_ixdel(ent *h, ent *e)
{
	size_t i, j;

	if (!h || !h->ix)
		return;
	i = vh(e->k) & (h->ix->cap - 1);
	for (j = 0; j < h->ix->cap; j++) {
		if (h->ix->t[i] == e) {
			h->ix->t[i] = MP_GONE;
			h->ix->n--;
			h->ix->dead++;
			return;
		}
		if (!h->ix->t[i])
			return;
		i = (i + 1) & (h->ix->cap - 1);
	}
}

/* The chain's last entry, written down in its head so an append is O(1). A
   chain a module linked itself has no tail yet, so it is found once. */
ent *mp_tail(ent *h)
{
	ent *t;

	if (!h)
		return 0;
	if (h->tl)
		return h->tl;
	for (t = h; t->nx; t = t->nx)
		;
	h->tl = t;
	return t;
}

/* Find an entry by key. */
ent *mp_find(ent *m, const char *k)
{
	if (m && m->ix)
		return mp_ixfind(m->ix, k);
	for (; m; m = m->nx)
		if (!strcmp(m->k, k))
			return m;
	return 0;
}

/* Find an entry by key, appending it when absent. */
ent *mp_add(ent **m, size_t *n, const char *k)
{
	ent *e = mp_find(*m, k), *h = *m;

	if (e)
		return e;
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->k = xs(k);
	if (!h) {
		*m = e;
		e->tl = e;
	} else {
		mp_tail(h)->nx = e;
		h->tl = e;
	}
	(*n)++;
	h = *m;
	if (h->ix) {
		if ((h->ix->n + h->ix->dead) * 4 >= h->ix->cap * 3)
			mp_ixbuild(h, *n);
		else
			mp_ixput(h->ix, e);
	} else if (*n >= HIBR_MPHASH) {
		mp_ixbuild(h, *n);
	}
	return e;
}

/* Remove the entry a key names, keeping the tail, the index and the head's
   ownership of both right. 0 when there is no such key. */
int mp_del(ent **head, size_t *n, const char *k)
{
	ent *h = *head, *e, *prev = 0;

	if (!h)
		return 0;
	e = mp_find(h, k);
	if (!e)
		return 0;
	if (e != h)
		for (prev = h; prev->nx != e; prev = prev->nx)
			;
	mp_ixdel(h, e);
	if (prev)
		prev->nx = e->nx;
	else
		*head = e->nx;
	if (e == h) {
		/* the head itself: the new one takes the tail and the index */
		ent *nh = *head;

		if (nh) {
			nh->tl = e->tl == e ? nh : e->tl;
			nh->ix = e->ix;
		} else {
			free(e->ix);
		}
		e->ix = 0;
	} else if (h->tl == e) {
		h->tl = prev;
	}
	mp_free(e->map);
	free(e->k);
	free(e->s);
	free(e);
	(*n)--;
	return 1;
}

/* Release a map and everything below it. */
void mp_free(ent *m)
{
	ent *n;

	while (m) {
		n = m->nx;
		mp_free(m->map);
		free(m->ix);
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
	v = v_find(s, nm);
	if (v && v->ro) {
		lg(HIBR_LERR, "%s: readonly variable", nm);
		s->st = 1;
		v_refused = 1;
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
	long nx = 0;
	str ix;
	char *kv, *q, *end;

	if (hibr_set(s, k, "", 0) != HIBR_OK)
		return;
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
				long at = strtol(key, &end, 10);
				if (*key && !*end)
					nx = at + 1;
				v_setp(s, k, &key, 1, q + 2);
			}
			*q = ']';
			continue;
		}
		s_init(&ix);
		s_num(&ix, nx++);
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
	ent *h = 0, **t = &h, *e, *last = 0;
	size_t n = 0;

	for (; m; m = m->nx) {
		e = xm(sizeof *e);
		memset(e, 0, sizeof *e);
		e->k = xs(m->k);
		e->s = m->s ? xs(m->s) : 0;
		e->map = m_clone(m->map);
		e->n = m->n;
		e->ty = m->ty;
		*t = e;
		t = &e->nx;
		last = e;
		n++;
	}
	/* The copy is its own chain: its own tail, and its own index once it is
	   long enough -- the original's belongs to the original. */
	if (h) {
		h->tl = last;
		if (n >= HIBR_MPHASH)
			mp_ixbuild(h, n);
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
	b->ty = a->ty;
	lg(HIBR_LTRC, "copied %s to %s", src, dst);
}

/* Copy one variable, map and all, onto the entry at a subscript path of
   another, creating the levels it crosses: what a subscripted := does with
   a result that is a map, so a function's map can be filed under a key. */
void v_copyp(sh *s, const char *dst, char **ks, int nk, const char *src)
{
	var *a = v_find(s, src);
	ent *e;

	v_setp(s, dst, ks, nk, a && a->v ? a->v : "");
	if (!a || !a->map || !nk)
		return;
	e = v_path(s, dst, ks, nk, 0);
	if (!e)
		return;
	e->map = m_clone(a->map);
	e->n = a->n;
	e->ty = a->ty;
	lg(HIBR_LTRC, "copied %s's map under %s", src, dst);
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
	ent **mp, *e;
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
	return mp_del(mp, np, ks[nk - 1]) ? HIBR_OK : HIBR_FAIL;
}
