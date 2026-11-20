/* varwatch -- which watched variables moved, told by the variable table.
 *
 * The desktop's state reaches each app's own process as a copy (ADR 0050),
 * so what moved has to be known without any writer remembering to say so.
 * Every write passes the shell's variable table, which asks this module
 * first when the variable carries A_WATCH, and says whether the write
 * changes anything; nothing else can move one.
 *
 *   varwatch add NAME...    watch these globals, kept across unset
 *   varwatch drop NAME...   stop
 *   varwatch seq            a number that moves when a watched one changes
 *   varwatch moved          what changed since last asked, NAME<TAB>KEY a
 *                           line (KEY empty: the whole of it); then cleared
 *   varwatch list           the names watched
 *   varwatch asks           how many writes the table has asked about,
 *                           changed or not: what watching costs
 *   varwatch guard on|off   refuse every write to a watched variable --
 *                           an app's copies of the desktop's state
 *   varwatch apply on|off   except while the copy itself is being updated
 *
 * Into the result slot, and printed unless := is catching it.
 */
#include "hibr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

var *v_find(sh *s, const char *k);
int asg_local(sh *s, const char *k);
extern int (*v_watchfn)(sh *s, const char *nm, const char *key, int ev);
extern int v_watchpend;
extern unsigned short v_watchbits[256];
unsigned vh(const char *k);

/* More first-level keys of one variable than this in one round is the whole of it. */
#define VR_KEYMAX 32
/* Buckets of the name table: a power of two. */
#define VR_NB 256

typedef struct vr_name {
	struct vr_name *nx;
	char *nm;
	int gone, whole, nk, moved;
	vec keys;
} vr_name;

vr_name *vr_tab[VR_NB];
vec vr_all;
unsigned long vr_seq, vr_asks;
int vr_guard, vr_apply;

/* A name's bucket. */
unsigned vr_hash(const char *k)
{
	unsigned h = 2166136261u;

	while (*k)
		h = (h ^ (unsigned char)*k++) * 16777619u;
	return h & (VR_NB - 1);
}

/* The record of a watched name, or NULL. */
vr_name *vr_of(const char *nm)
{
	vr_name *w;

	for (w = vr_tab[vr_hash(nm)]; w; w = w->nx)
		if (!strcmp(w->nm, nm))
			return w;
	return 0;
}

/* Forget the keys a name has moved at. */
void vr_clearkeys(vr_name *w)
{
	size_t i;

	for (i = 0; i < w->keys.n; i++)
		free(w->keys.p[i]);
	w->keys.n = 0;
	w->nk = 0;
	w->whole = 0;
	w->moved = 0;
}

/* Note that w moved at key (NULL: all of it). */
void vr_note(vr_name *w, const char *key)
{
	size_t i;

	vr_seq++;
	w->moved = 1;
	if (w->whole)
		return;
	if (!key || w->nk >= VR_KEYMAX) {
		vr_clearkeys(w);
		w->whole = 1;
		w->moved = 1;
		return;
	}
	for (i = 0; i < w->keys.n; i++)
		if (!strcmp(w->keys.p[i], key))
			return;
	v_add(&w->keys, xs(key));
	w->nk++;
}

/* w waits to be declared again, or no longer: counted where its hash falls, so only a creation there asks. */
void vr_pend(vr_name *w, int on)
{
	unsigned short *c = &v_watchbits[vh(w->nm) & 255];

	if (on == w->gone)
		return;
	w->gone = on;
	if (on) {
		(*c)++;
		v_watchpend++;
	} else {
		(*c)--;
		v_watchpend--;
	}
}

/* What the variable table asks before a write: refuse it under the guard, else note what changed. */
int vr_told(sh *s, const char *nm, const char *key, int ev)
{
	vr_name *w = vr_of(nm);
	var *v;

	vr_asks++;
	if (!w)
		return 0;
	if (ev == 2) {
		if (!w->gone || asg_local(s, nm))
			return 0;
		v = v_find(s, nm);
		if (!v)
			return 0;
		v->at |= A_WATCH;
		vr_pend(w, 0);
		lg(HIBR_LTRC, "varwatch: %s declared again, watched again", nm);
		vr_note(w, 0);
		return 0;
	}
	if (ev == 3)
		return 0;
	if (vr_guard && !vr_apply) {
		lg(HIBR_LERR, "%s: the desktop's, read only in an app's own process", nm);
		return 1;
	}
	if (ev == 1 && !w->gone)
		vr_pend(w, 1);
	vr_note(w, ev == 1 ? 0 : key);
	return 0;
}

/* Watch nm, a global that exists: status 1 for one that does not. */
int vr_add(sh *s, const char *nm)
{
	vr_name *w = vr_of(nm);
	var *v;
	unsigned b;

	if (w)
		return 0;
	v = asg_local(s, nm) ? 0 : v_find(s, nm);
	if (!v) {
		lg(HIBR_LERR, "varwatch: %s: no such global to watch", nm);
		return 1;
	}
	w = xm(sizeof *w);
	memset(w, 0, sizeof *w);
	w->nm = xs(nm);
	b = vr_hash(nm);
	w->nx = vr_tab[b];
	vr_tab[b] = w;
	v_add(&vr_all, w);
	v->at |= A_WATCH;
	return 0;
}

/* Stop watching w, taking its bit off. */
void vr_del(sh *s, vr_name *w)
{
	vr_name **pp;
	size_t i;
	var *v;

	if (w->gone)
		vr_pend(w, 0);
	else if (!asg_local(s, w->nm) && (v = v_find(s, w->nm)))
		v->at &= ~A_WATCH;
	for (pp = &vr_tab[vr_hash(w->nm)]; *pp; pp = &(*pp)->nx)
		if (*pp == w) {
			*pp = w->nx;
			break;
		}
	for (i = 0; i < vr_all.n; i++)
		if (vr_all.p[i] == w) {
			vr_all.p[i] = vr_all.p[--vr_all.n];
			break;
		}
	vr_clearkeys(w);
	free(w->keys.p);
	free(w->nm);
	free(w);
}

/* Hand back lines: a list in the slot, a line each, printed unless one is bound. */
void vr_out(sh *s, str *o, int list)
{
	vec v;
	char *p, *nl;

	if (o->n && o->p[o->n - 1] == '\n')
		o->p[--o->n] = 0;
	if (!s->bind && o->n)
		printf("%s\n", o->p);
	if (!list) {
		hibr_ret(s, o->p ? o->p : "");
		return;
	}
	memset(&v, 0, sizeof v);
	for (p = o->n ? o->p : 0; p; p = nl ? nl + 1 : 0) {
		nl = strchr(p, '\n');
		if (nl)
			*nl = 0;
		v_add(&v, p);
	}
	hibr_retn(s, (char **)v.p, v.n);
	free(v.p);
}

/* Whether a word says on, for guard and apply; -1 for neither. */
int vr_onoff(const char *w)
{
	if (!strcmp(w, "on"))
		return 1;
	if (!strcmp(w, "off"))
		return 0;
	return -1;
}

/* varwatch add|drop|seq|moved|list|guard|apply. */
int m_varwatch(sh *s, int ac, char **av)
{
	str o;
	size_t i, j;
	int k, st = HIBR_OK, on;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: varwatch add|drop NAME... | seq | moved | list | guard|apply on|off");
		return 2;
	}
	if (!strcmp(av[1], "add") || !strcmp(av[1], "drop")) {
		for (k = 2; k < ac; k++) {
			if (av[1][0] == 'a') {
				if (vr_add(s, av[k]))
					st = HIBR_FAIL;
			} else if (vr_of(av[k])) {
				vr_del(s, vr_of(av[k]));
			}
		}
		return st;
	}
	if (!strcmp(av[1], "guard") || !strcmp(av[1], "apply")) {
		on = ac == 3 ? vr_onoff(av[2]) : -1;
		if (on < 0) {
			lg(HIBR_LERR, "usage: varwatch %s on|off", av[1]);
			return 2;
		}
		if (av[1][0] == 'g')
			vr_guard = on;
		else
			vr_apply = on;
		return HIBR_OK;
	}
	s_init(&o);
	if (!strcmp(av[1], "seq")) {
		s_num(&o, (long)vr_seq);
	} else if (!strcmp(av[1], "asks")) {
		s_num(&o, (long)vr_asks);
	} else if (!strcmp(av[1], "moved")) {
		for (i = 0; i < vr_all.n; i++) {
			vr_name *w = vr_all.p[i];

			if (!w->moved)
				continue;
			if (w->whole) {
				s_cat(&o, w->nm);
				s_cat(&o, "\t\n");
			}
			for (j = 0; !w->whole && j < w->keys.n; j++) {
				s_cat(&o, w->nm);
				s_ch(&o, '\t');
				s_cat(&o, w->keys.p[j]);
				s_ch(&o, '\n');
			}
			vr_clearkeys(w);
		}
	} else if (!strcmp(av[1], "list")) {
		for (i = 0; i < vr_all.n; i++) {
			s_cat(&o, ((vr_name *)vr_all.p[i])->nm);
			s_ch(&o, '\n');
		}
	} else {
		lg(HIBR_LERR, "varwatch: %s: not a verb", av[1]);
		s_free(&o);
		return 2;
	}
	vr_out(s, &o, strcmp(av[1], "seq") && strcmp(av[1], "asks"));
	s_free(&o);
	return HIBR_OK;
}

const hibr_bi varwatch_bi[] = {
	{ "varwatch", m_varwatch, "which watched variables moved" },
	HIBR_BI_END
};

/* Take the variable table's question. */
int vr_ini(sh *s)
{
	(void)s;
	v_watchfn = vr_told;
	return HIBR_OK;
}

/* Give it back, and every bit with it. */
void vr_fin(sh *s)
{
	v_watchfn = 0;
	while (vr_all.n)
		vr_del(s, vr_all.p[vr_all.n - 1]);
	free(vr_all.p);
	vr_all.p = 0;
	vr_all.cap = 0;
	v_watchpend = 0;
	memset(v_watchbits, 0, sizeof v_watchbits);
	vr_guard = 0;
	vr_apply = 0;
}

HIBR_MODULE("varwatch", "1.0", "which watched variables moved", varwatch_bi,
	    vr_ini, vr_fin);
