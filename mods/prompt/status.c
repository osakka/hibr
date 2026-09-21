#include "pr.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* True when the file's type or permission bits no longer match the index. */
int st_modediff(struct ie *e, struct stat *st, int filemode)
{
	unsigned m = e->mode & 0170000;

	if (m == 0120000)
		return !S_ISLNK(st->st_mode);
	if (m == 0160000)
		return !S_ISDIR(st->st_mode);
	if (!S_ISREG(st->st_mode))
		return 1;
	if (filemode &&
	    ((e->mode & 0111) != 0) != ((st->st_mode & 0111) != 0))
		return 1;
	return 0;
}

/* True when the recorded stat data still describes the file exactly. */
int st_statsame(gidx *x, struct ie *e, struct stat *st)
{
	if (e->size != (unsigned)st->st_size)
		return 0;
	if (e->mtim != (unsigned)st->st_mtime)
		return 0;
	if (e->mtin && e->mtin != (unsigned)st->st_mtim.tv_nsec)
		return 0;
	if (x->mtim && (x->mtim < e->mtim ||
			(x->mtim == e->mtim && x->mtimns <= e->mtin)))
		return 0;
	return 1;
}

/* Compare every index entry against the working tree. */
void st_worktree(grepo *g, gidx *x, gstat *o, int filemode)
{
	size_t i;
	str p;
	size_t tl;

	s_init(&p);
	s_cat(&p, g->top);
	s_ch(&p, '/');
	tl = p.n;
	for (i = 0; i < x->ents.n; i++) {
		struct ie *e = (struct ie *)x->ents.p[i];
		struct stat st;
		unsigned char sha[20];
		if (e->stage) {
			if (i + 1 < x->ents.n &&
			    !strcmp(((struct ie *)x->ents.p[i + 1])->path,
				    e->path))
				continue;
			o->conflicted++;
			continue;
		}
		p.n = tl;
		p.p[p.n] = 0;
		s_cat(&p, e->path);
		if (lstat(p.p, &st) != 0) {
			o->deleted++;
			continue;
		}
		if (st_modediff(e, &st, filemode)) {
			o->modified++;
			continue;
		}
		if ((e->mode & 0170000) == 0160000)
			continue;
		if (st_statsame(x, e, &st))
			continue;
		if (!sh_blob(p.p, S_ISLNK(st.st_mode), sha) ||
		    memcmp(sha, e->sha, 20))
			o->modified++;
	}
	s_free(&p);
}

/* Read a tree object into a flat list of paths below a prefix. */
void st_flatten(grepo *g, const unsigned char *sha, const char *prefix,
		vec *out, int depth)
{
	str body;
	int type;
	size_t pos = 0;

	if (depth > 64)
		return;
	if (!ob_get(g, sha, &type, &body) || type != OB_TREE) {
		s_free(&body);
		return;
	}
	while (pos < body.n) {
		char *e;
		long mode = strtol(body.p + pos, &e, 8);
		size_t np;
		struct te *t;
		const char *nm;
		if (e == body.p + pos || *e != ' ')
			break;
		pos = (size_t)(e - body.p) + 1;
		nm = body.p + pos;
		np = strnlen(nm, body.n - pos);
		if (pos + np + 21 > body.n)
			break;
		t = xm(sizeof *t);
		memset(t, 0, sizeof *t);
		{
			str full;
			s_init(&full);
			s_cat(&full, prefix);
			s_add(&full, nm, np);
			t->path = full.p;
		}
		t->mode = (unsigned)mode;
		memcpy(t->sha, body.p + pos + np + 1, 20);
		pos += np + 21;
		if ((t->mode & 0170000) == 0040000 || t->mode == 040000) {
			str sub;
			s_init(&sub);
			s_cat(&sub, t->path);
			s_ch(&sub, '/');
			st_flatten(g, t->sha, sub.p, out, depth + 1);
			s_free(&sub);
			free(t->path);
			free(t);
			continue;
		}
		v_add(out, t);
	}
	s_free(&body);
}

/* Order tree records by path. */
int st_tecmp(const void *a, const void *b)
{
	return strcmp((*(struct te *const *)a)->path,
		      (*(struct te *const *)b)->path);
}

/* Compare the index against the tree recorded in HEAD. */
void st_staged(grepo *g, gidx *x, gstat *o)
{
	unsigned char hsha[20], tsha[20];
	str body;
	int type;
	vec tree = { 0, 0, 0 }, adds = { 0, 0, 0 }, dels = { 0, 0, 0 };
	struct ct *root;
	size_t i = 0, j = 0;
	char *e;

	if (!gt_headsha(g, hsha)) {
		o->unborn = 1;
		for (i = 0; i < x->ents.n; i++)
			if (!((struct ie *)x->ents.p[i])->stage)
				o->staged++;
		return;
	}
	if (!ob_get(g, hsha, &type, &body))
		return;
	if (type == OB_TAG || type != OB_COMMIT) {
		s_free(&body);
		return;
	}
	if (body.n < 46 || strncmp(body.p, "tree ", 5) ||
	    !ob_hex(body.p + 5, tsha)) {
		s_free(&body);
		return;
	}
	s_free(&body);
	root = ix_ct(x, "");
	if (root && root->cnt >= 0 && !memcmp(root->sha, tsha, 20)) {
		lg(HIBR_LDBG, "staged: cached tree matches HEAD, nothing staged");
		return;
	}
	st_flatten(g, tsha, "", &tree, 0);
	if (tree.n)
		qsort(tree.p, tree.n, sizeof *tree.p, st_tecmp);
	(void)e;
	while (i < x->ents.n || j < tree.n) {
		struct ie *ie = i < x->ents.n ? (struct ie *)x->ents.p[i] : 0;
		struct te *te = j < tree.n ? (struct te *)tree.p[j] : 0;
		int c = !ie ? 1 : !te ? -1 : strcmp(ie->path, te->path);
		if (ie && ie->stage && c <= 0) {
			char *cp = ie->path;
			while (i < x->ents.n &&
			       !strcmp(((struct ie *)x->ents.p[i])->path, cp))
				i++;
			if (!c)
				j++;
			continue;
		}
		if (c < 0) {
			v_add(&adds, ie->sha);
			i++;
		} else if (c > 0) {
			v_add(&dels, te->sha);
			j++;
		} else {
			if (memcmp(ie->sha, te->sha, 20) ||
			    (ie->mode & 0177777) != (te->mode & 0177777))
				o->staged++;
			i++;
			j++;
		}
	}
	for (i = 0; i < adds.n; i++) {
		for (j = 0; j < dels.n; j++)
			if (dels.p[j] && !memcmp(adds.p[i], dels.p[j], 20)) {
				dels.p[j] = 0;
				adds.p[i] = 0;
				o->renamed++;
				o->staged++;
				break;
			}
	}
	for (i = 0; i < adds.n; i++)
		if (adds.p[i])
			o->staged++;
	for (j = 0; j < dels.n; j++)
		if (dels.p[j])
			o->staged++;
	v_free(&adds);
	v_free(&dels);
	for (j = 0; j < tree.n; j++) {
		struct te *t = (struct te *)tree.p[j];
		free(t->path);
		free(t);
	}
	v_free(&tree);
}

/* Find the first index entry at or after a path. */
size_t ix_lower(gidx *x, const char *path)
{
	size_t lo = 0, hi = x->ents.n, mid;

	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		if (strcmp(((struct ie *)x->ents.p[mid])->path, path) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* True when the index holds exactly this path. */
int ix_has(gidx *x, const char *path)
{
	size_t i = ix_lower(x, path);

	return i < x->ents.n &&
	       !strcmp(((struct ie *)x->ents.p[i])->path, path);
}

/* True when the index holds anything below this prefix. */
int ix_under(gidx *x, const char *prefix)
{
	size_t i = ix_lower(x, prefix);

	return i < x->ents.n &&
	       !strncmp(((struct ie *)x->ents.p[i])->path, prefix,
			strlen(prefix));
}

/* Order directory names, ignoring the type tag each carries. */
int st_namecmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a + 1, *(char *const *)b + 1);
}

/* Walk one directory counting untracked entries, or probing for any. */
int st_scan(grepo *g, gidx *x, gstat *o, str *rel, vec *stack, int depth,
	    int probe)
{
	str full;
	DIR *d;
	struct dirent *de;
	vec names = { 0, 0, 0 };
	struct iglev *lev = 0;
	char *gi;
	size_t i;
	int found = 0;

	if (depth > 64)
		return 0;
	s_init(&full);
	s_cat(&full, g->top);
	s_ch(&full, '/');
	s_add(&full, rel->p ? rel->p : "", rel->n);
	{
		str gp;
		s_init(&gp);
		s_add(&gp, full.p, full.n);
		s_cat(&gp, ".gitignore");
		gi = pr_slurp(gp.p, 1 << 20);
		s_free(&gp);
	}
	if (gi) {
		lev = xm(sizeof *lev);
		memset(lev, 0, sizeof *lev);
		lev->base = rel->n;
		ig_load(gi, &lev->pats);
		free(gi);
		v_add(stack, lev);
	}
	d = opendir(full.p);
	while (d && (de = readdir(d)) != 0) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (!rel->n && !strcmp(de->d_name, ".git"))
			continue;
		{
			size_t l = strlen(de->d_name);
			char *t = xm(l + 2);
			t[0] = de->d_type == DT_DIR	? 'd'
			       : de->d_type == DT_UNKNOWN ? '?'
							  : 'f';
			memcpy(t + 1, de->d_name, l + 1);
			v_add(&names, t);
		}
	}
	if (d)
		closedir(d);
	if (names.n)
		qsort(names.p, names.n, sizeof *names.p, st_namecmp);
	for (i = 0; i < names.n && !(probe && found); i++) {
		char *nm = (char *)names.p[i] + 1;
		str sub;
		int isdir = names.p[i] && *(char *)names.p[i] == 'd';
		size_t keep = full.n;
		if (*(char *)names.p[i] == '?') {
			struct stat st;
			s_cat(&full, nm);
			if (lstat(full.p, &st) != 0) {
				full.n = keep;
				full.p[full.n] = 0;
				continue;
			}
			isdir = S_ISDIR(st.st_mode);
			full.n = keep;
			full.p[full.n] = 0;
		}
		s_init(&sub);
		s_add(&sub, rel->p ? rel->p : "", rel->n);
		s_cat(&sub, nm);
		if (isdir) {
			str pre;
			if (ix_has(x, sub.p)) {
				s_free(&sub);
				continue;
			}
			s_init(&pre);
			s_add(&pre, sub.p, sub.n);
			s_ch(&pre, '/');
			if (ix_under(x, pre.p)) {
				found |= st_scan(g, x, o, &pre, stack,
						 depth + 1, probe);
			} else if (ig_test(stack, sub.p, nm, 1)) {
				/* ignored directory, not descended */
			} else if (st_scan(g, x, 0, &pre, stack, depth + 1,
					   1)) {
				found = 1;
				if (o)
					o->untracked++;
			}
			s_free(&pre);
			s_free(&sub);
			continue;
		}
		if (!ix_has(x, sub.p) && !ig_test(stack, sub.p, nm, 0)) {
			found = 1;
			if (o)
				o->untracked++;
		}
		s_free(&sub);
	}
	for (i = 0; i < names.n; i++)
		free(names.p[i]);
	v_free(&names);
	if (lev) {
		stack->n--;
		ig_free(&lev->pats);
		free(lev);
	}
	s_free(&full);
	return found;
}

/* Count untracked files, honouring the ignore files that apply. */
void st_untracked(grepo *g, gidx *x, gstat *o)
{
	vec stack = { 0, 0, 0 };
	struct iglev *base = xm(sizeof *base);
	str rel;
	char *t;

	memset(base, 0, sizeof *base);
	base->base = 0;
	t = gt_read(g->dir, "info/exclude", 1 << 20);
	if (t) {
		ig_load(t, &base->pats);
		free(t);
	}
	{
		char *cf = gt_cfg(g, "core", 0, "excludesFile");
		char *home = getenv("HOME");
		str p;
		s_init(&p);
		if (cf && *cf == '~' && home) {
			s_cat(&p, home);
			s_cat(&p, cf + 1);
		} else if (cf) {
			s_cat(&p, cf);
		} else if (home) {
			s_cat(&p, home);
			s_cat(&p, "/.config/git/ignore");
		}
		free(cf);
		if (p.n) {
			char *e = pr_slurp(p.p, 1 << 20);
			if (e) {
				ig_load(e, &base->pats);
				free(e);
			}
		}
		s_free(&p);
	}
	v_add(&stack, base);
	s_init(&rel);
	s_grow(&rel, 1);
	rel.p[0] = 0;
	st_scan(g, x, o, &rel, &stack, 0, 0);
	s_free(&rel);
	ig_free(&base->pats);
	free(base);
	v_free(&stack);
}

/* Work out the repository status, once per prompt. */
gstat *st_get(pctx *c)
{
	grepo *g = c->g;
	gidx *x;
	char *fm;
	int filemode = 1;

	if (!g)
		return 0;
	if (g->st.ok)
		return &g->st;
	g->st.ok = 1;
	x = ix_read(g);
	if (!x)
		return &g->st;
	fm = gt_cfg(g, "core", 0, "filemode");
	if (fm && (!strcmp(fm, "false") || !strcmp(fm, "0")))
		filemode = 0;
	free(fm);
	st_worktree(g, x, &g->st, filemode);
	if (pr_cfgi(c, "git", "staged", 1))
		st_staged(g, x, &g->st);
	if (pr_cfgi(c, "git", "untracked", 1))
		st_untracked(g, x, &g->st);
	ix_free(x);
	if (pr_cfgi(c, "git", "track", 1))
		wk_track(c, g, &g->st);
	lg(HIBR_LDBG,
	   "status: %d staged, %d modified, %d deleted, %d untracked, %d conflicted",
	   g->st.staged, g->st.modified, g->st.deleted, g->st.untracked,
	   g->st.conflicted);
	return &g->st;
}
