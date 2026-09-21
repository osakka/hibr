#include "pr.h"
#include <stdlib.h>
#include <string.h>

/* Hash an object name into a table slot. */
size_t wk_hash(const unsigned char *sha, size_t mask)
{
	return (((size_t)sha[0] << 24) | ((size_t)sha[1] << 16) |
		((size_t)sha[2] << 8) | (size_t)sha[3]) &
	       mask;
}

/* Grow the flag table, rehashing what it holds. */
void wk_grow(wmap *m)
{
	size_t ns = m->cap ? m->cap * 2 : 256;
	struct went *nt = xm(ns * sizeof *nt);
	size_t i;

	memset(nt, 0, ns * sizeof *nt);
	for (i = 0; i < m->cap; i++) {
		size_t j;
		if (!m->t[i].used)
			continue;
		j = wk_hash(m->t[i].sha, ns - 1);
		while (nt[j].used)
			j = (j + 1) & (ns - 1);
		nt[j] = m->t[i];
	}
	free(m->t);
	m->t = nt;
	m->cap = ns;
}

/* Find or create the flag record for one object name. */
struct went *wk_slot(wmap *m, const unsigned char *sha)
{
	size_t i;

	if (m->n * 4 >= m->cap * 3)
		wk_grow(m);
	i = wk_hash(sha, m->cap - 1);
	while (m->t[i].used) {
		if (!memcmp(m->t[i].sha, sha, 20))
			return &m->t[i];
		i = (i + 1) & (m->cap - 1);
	}
	m->t[i].used = 1;
	memcpy(m->t[i].sha, sha, 20);
	m->t[i].flags = 0;
	m->n++;
	return &m->t[i];
}

/* Read a commit's date and parents. */
int wk_commit(grepo *g, const unsigned char *sha, long *date, vec *parents)
{
	str body;
	int type;
	const char *p, *end;

	if (!ob_get(g, sha, &type, &body))
		return 0;
	if (type != OB_COMMIT) {
		s_free(&body);
		return 0;
	}
	*date = 0;
	p = body.p;
	end = body.p + body.n;
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		if (!len)
			break;
		if (len > 7 && !strncmp(p, "parent ", 7)) {
			unsigned char *ps = xm(20);
			if (len >= 47 && ob_hex(p + 7, ps))
				v_add(parents, ps);
			else
				free(ps);
		} else if (len > 10 && !strncmp(p, "committer ", 10)) {
			const char *q = p + len;
			int fields = 0;
			while (q > p && fields < 2) {
				q--;
				if (*q == ' ') {
					fields++;
					if (fields == 2)
						break;
				}
			}
			*date = atol(q + 1);
		}
		if (!nl)
			break;
		p = nl + 1;
	}
	s_free(&body);
	return 1;
}

/* Insert into the queue, newest commit first. */
void wk_push(vec *q, const unsigned char *sha, long date)
{
	struct wq *e = xm(sizeof *e);
	size_t i;

	memcpy(e->sha, sha, 20);
	e->date = date;
	v_add(q, e);
	for (i = q->n - 1; i > 0; i--) {
		struct wq *a = (struct wq *)q->p[i - 1];
		if (a->date >= e->date)
			break;
		q->p[i] = q->p[i - 1];
	}
	q->p[i] = e;
}

/* Count how far two commits have diverged, or report that it is unknown. */
int wk_divergence(grepo *g, const unsigned char *local,
		  const unsigned char *up, int max, int *ahead, int *behind)
{
	wmap m;
	vec q = { 0, 0, 0 };
	size_t i;
	int visited = 0, ok = 1, wf;

	memset(&m, 0, sizeof m);
	wk_grow(&m);
	*ahead = 0;
	*behind = 0;
	if (!memcmp(local, up, 20)) {
		free(m.t);
		return 1;
	}
	wk_slot(&m, local)->flags = 1;
	wk_slot(&m, up)->flags = 2;
	{
		long d = 0;
		vec tmp = { 0, 0, 0 };
		wk_commit(g, local, &d, &tmp);
		wk_push(&q, local, d);
		for (i = 0; i < tmp.n; i++)
			free(tmp.p[i]);
		v_free(&tmp);
		d = 0;
		wk_commit(g, up, &d, &tmp);
		wk_push(&q, up, d);
		for (i = 0; i < tmp.n; i++)
			free(tmp.p[i]);
		v_free(&tmp);
	}
	while (q.n) {
		struct wq *e;
		struct went *w;
		vec parents = { 0, 0, 0 };
		long d = 0;
		int interesting = 0;
		for (i = 0; i < q.n; i++)
			if (wk_slot(&m, ((struct wq *)q.p[i])->sha)->flags != 3) {
				interesting = 1;
				break;
			}
		if (!interesting)
			break;
		if (++visited > max) {
			ok = 0;
			break;
		}
		e = (struct wq *)q.p[0];
		for (i = 1; i < q.n; i++)
			q.p[i - 1] = q.p[i];
		q.n--;
		w = wk_slot(&m, e->sha);
		wf = w->flags;
		if (!wk_commit(g, e->sha, &d, &parents)) {
			free(e);
			v_free(&parents);
			continue;
		}
		for (i = 0; i < parents.n; i++) {
			unsigned char *ps = (unsigned char *)parents.p[i];
			struct went *pw = wk_slot(&m, ps);
			int nf = pw->flags | wf;
			if (nf != pw->flags) {
				long pd = 0;
				vec sub = { 0, 0, 0 };
				pw->flags = nf;
				wk_commit(g, ps, &pd, &sub);
				{
					size_t k;
					for (k = 0; k < sub.n; k++)
						free(sub.p[k]);
				}
				v_free(&sub);
				wk_push(&q, ps, pd);
			}
			free(ps);
		}
		v_free(&parents);
		free(e);
	}
	for (i = 0; i < q.n; i++)
		free(q.p[i]);
	v_free(&q);
	if (ok)
		for (i = 0; i < m.cap; i++) {
			if (!m.t[i].used)
				continue;
			if (m.t[i].flags == 1)
				(*ahead)++;
			else if (m.t[i].flags == 2)
				(*behind)++;
		}
	free(m.t);
	if (!ok)
		lg(HIBR_LDBG, "ahead/behind: gave up after %d commits", max);
	return ok;
}

/* Name the branch this one tracks, if it tracks one. */
char *wk_upstream(grepo *g)
{
	char *rem, *mrg;
	str o;

	if (!g->branch)
		return 0;
	rem = gt_cfg(g, "branch", g->branch, "remote");
	mrg = gt_cfg(g, "branch", g->branch, "merge");
	s_init(&o);
	if (rem && mrg) {
		const char *m = mrg;
		if (!strncmp(m, "refs/heads/", 11))
			m += 11;
		if (!strcmp(rem, ".")) {
			s_cat(&o, mrg);
		} else {
			s_cat(&o, "refs/remotes/");
			s_cat(&o, rem);
			s_ch(&o, '/');
			s_cat(&o, m);
		}
	} else {
		s_cat(&o, "refs/remotes/origin/");
		s_cat(&o, g->branch);
	}
	free(rem);
	free(mrg);
	return o.p;
}

/* Fill in how far the branch is ahead of and behind its upstream. */
void wk_track(pctx *c, grepo *g, gstat *o)
{
	unsigned char lo[20], up[20];
	char *ref = wk_upstream(g);
	int max = pr_cfgi(c, "git", "max_walk", 512);

	if (!ref)
		return;
	if (!gt_ref(g, ref, up) || !gt_headsha(g, lo)) {
		free(ref);
		return;
	}
	free(ref);
	if (!wk_divergence(g, lo, up, max > 0 ? max : 512, &o->ahead,
			   &o->behind)) {
		o->ahead = 0;
		o->behind = 0;
	}
}
