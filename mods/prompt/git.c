#include "pr.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Join a directory and a relative name. */
char *gt_join(const char *dir, const char *sub)
{
	str o;

	s_init(&o);
	s_cat(&o, dir);
	if (o.n && o.p[o.n - 1] != '/')
		s_ch(&o, '/');
	s_cat(&o, sub);
	return o.p;
}

/* True when a path names a directory. */
int gt_isdir(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* True when a path exists. */
int gt_has(const char *dir, const char *sub)
{
	char *p = gt_join(dir, sub);
	int r = access(p, F_OK) == 0;

	free(p);
	return r;
}

/* Read a file under a directory, trimming trailing newlines. */
char *gt_read(const char *dir, const char *sub, size_t max)
{
	char *p = gt_join(dir, sub);
	char *t = pr_slurp(p, max);

	free(p);
	if (!t)
		return 0;
	while (*t && (t[strlen(t) - 1] == '\n' || t[strlen(t) - 1] == '\r'))
		t[strlen(t) - 1] = 0;
	return t;
}

/* Walk up from a directory looking for the repository marker. */
int gt_find(pctx *c, grepo *g)
{
	str b;
	size_t base;

	if (!c->cwd)
		return 0;
	s_init(&b);
	s_cat(&b, c->cwd);
	for (;;) {
		char *dot;
		base = b.n;
		s_cat(&b, "/.git");
		dot = b.p;
		if (gt_isdir(dot)) {
			g->dir = xs(dot);
			b.n = base;
			b.p[b.n] = 0;
			g->top = xs(b.n ? b.p : "/");
			s_free(&b);
			return 1;
		}
		if (access(dot, F_OK) == 0) {
			char *t = pr_slurp(dot, 4096);
			char *gd = t ? pr_field(t, "gitdir", 0) : 0;
			free(t);
			if (gd && *gd) {
				g->dir = *gd == '/' ? xs(gd) : gt_join(b.p, gd);
				b.n = base;
				b.p[b.n] = 0;
				g->top = xs(b.n ? b.p : "/");
				free(gd);
				s_free(&b);
				return 1;
			}
			free(gd);
		}
		b.n = base;
		b.p[b.n] = 0;
		if (!b.n)
			break;
		while (b.n && b.p[b.n - 1] != '/')
			b.n--;
		if (b.n)
			b.n--;
		b.p[b.n] = 0;
		if (!b.n) {
			s_cat(&b, "");
			if (gt_isdir("/.git")) {
				g->dir = xs("/.git");
				g->top = xs("/");
				s_free(&b);
				return 1;
			}
			break;
		}
	}
	s_free(&b);
	return 0;
}

/* Resolve the directory holding refs shared across worktrees. */
void gt_common(grepo *g)
{
	char *cd = gt_read(g->dir, "commondir", 4096);

	if (cd && *cd)
		g->common = *cd == '/' ? xs(cd) : gt_join(g->dir, cd);
	else
		g->common = xs(g->dir);
	free(cd);
}

/* Read HEAD, giving either a branch name or a short object name. */
void gt_head(grepo *g)
{
	char *h = gt_read(g->dir, "HEAD", 4096);

	if (!h)
		return;
	if (!strncmp(h, "ref: ", 5)) {
		const char *r = h + 5;
		if (!strncmp(r, "refs/heads/", 11))
			r += 11;
		g->branch = xs(r);
	} else if (strlen(h) >= 7) {
		g->sha = xs(h);
		g->sha[7] = 0;
	}
	free(h);
}

/* Describe an interrupted operation in progress. */
void gt_state(grepo *g)
{
	str o;

	s_init(&o);
	if (gt_has(g->dir, "rebase-merge") || gt_has(g->dir, "rebase-apply")) {
		const char *d = gt_has(g->dir, "rebase-merge") ? "rebase-merge"
							      : "rebase-apply";
		char *sub = gt_join(d, "msgnum");
		char *num = gt_read(g->dir, sub, 64);
		char *sub2, *end;
		free(sub);
		sub2 = gt_join(d, "end");
		end = gt_read(g->dir, sub2, 64);
		free(sub2);
		if (!num) {
			free(num);
			sub = gt_join(d, "next");
			num = gt_read(g->dir, sub, 64);
			free(sub);
			sub = gt_join(d, "last");
			free(end);
			end = gt_read(g->dir, sub, 64);
			free(sub);
		}
		s_cat(&o, "REBASING");
		if (num && end) {
			s_ch(&o, ' ');
			s_cat(&o, num);
			s_ch(&o, '/');
			s_cat(&o, end);
		}
		free(num);
		free(end);
	} else if (gt_has(g->dir, "MERGE_HEAD")) {
		s_cat(&o, "MERGING");
	} else if (gt_has(g->dir, "CHERRY_PICK_HEAD")) {
		s_cat(&o, "CHERRY-PICKING");
	} else if (gt_has(g->dir, "REVERT_HEAD")) {
		s_cat(&o, "REVERTING");
	} else if (gt_has(g->dir, "BISECT_LOG")) {
		s_cat(&o, "BISECTING");
	}
	g->state = o.n ? o.p : 0;
	if (!o.n)
		s_free(&o);
}

/* Count entries in the stash reflog. */
void gt_stash(grepo *g)
{
	char *t = gt_read(g->common, "logs/refs/stash", 1 << 20);
	char *p = t;

	if (!t)
		return;
	while (*p)
		if (*p++ == '\n')
			g->stash++;
	if (p > t && p[-1] != '\n' && *t)
		g->stash++;
	free(t);
}

/* Discover the repository containing the working directory. */
int gt_open(pctx *c)
{
	grepo *g;

	if (c->gdone)
		return c->g != 0;
	c->gdone = 1;
	g = xm(sizeof *g);
	memset(g, 0, sizeof *g);
	if (!gt_find(c, g)) {
		free(g);
		return 0;
	}
	g->omax = (size_t)pr_cfgi(c, "git", "max_object", 4096) * 1024;
	if (!g->omax || g->omax > OB_MAX)
		g->omax = OB_MAX;
	gt_common(g);
	gt_head(g);
	gt_state(g);
	gt_stash(g);
	g->ok = 1;
	c->g = g;
	lg(HIBR_LDBG, "prompt: repository at %s, branch %s", g->dir,
	   g->branch ? g->branch : "(detached)");
	return 1;
}

/* Release the repository record. */
void gt_close(pctx *c)
{
	grepo *g = c->g;

	if (!g)
		return;
	ob_free(g);
	free(g->cfgtxt);
	free(g->dir);
	free(g->common);
	free(g->top);
	free(g->branch);
	free(g->sha);
	free(g->state);
	free(g);
	c->g = 0;
}

/* Give the working directory relative to the repository root. */
char *gt_rel(pctx *c)
{
	grepo *g;
	str o;
	size_t tl;

	if (!pr_cfgi(c, "dir", "repo_root", 1))
		return 0;
	if (!gt_open(c))
		return 0;
	g = c->g;
	tl = strlen(g->top);
	if (strncmp(c->cwd, g->top, tl))
		return 0;
	s_init(&o);
	s_cat(&o, strrchr(g->top, '/') && g->top[1] ? strrchr(g->top, '/') + 1
						    : g->top);
	s_cat(&o, c->cwd + tl);
	return o.p;
}

/* Show the git segment inside a repository. */
int gt_act(pctx *c, const seg *g)
{
	(void)g;
	return gt_open(c);
}

/* Supply the git fields. */
char *gt_val(pctx *c, const seg *sg, const char *f)
{
	grepo *g;
	str o;

	(void)sg;
	if (!gt_open(c))
		return 0;
	g = c->g;
	if (!strcmp(f, "branch"))
		return xs(g->branch ? g->branch : (g->sha ? g->sha : "?"));
	if (!strcmp(f, "state"))
		return g->state ? xs(g->state) : 0;
	if (!strcmp(f, "stash")) {
		if (!g->stash)
			return 0;
		s_init(&o);
		s_num(&o, g->stash);
		return o.p;
	}
	return gt_stat(c, sg, f);
}

/* Read a configured symbol for a status count, or its default. */
const char *gt_sym(pctx *c, const char *key, const char *dflt)
{
	const char *v = pr_cfg(c, "git", key);

	return v ? v : dflt;
}

/* Append one count to a status summary when it is not zero. */
void gt_count(pctx *c, str *o, int n, const char *key, const char *dflt)
{
	if (!n)
		return;
	if (o->n)
		s_ch(o, ' ');
	s_cat(o, gt_sym(c, key, dflt));
	s_num(o, n);
}

/* Supply the fields that come from the repository status. */
char *gt_stat(pctx *c, const seg *sg, const char *f)
{
	gstat *t;
	str o;

	(void)sg;
	if (strcmp(f, "status") && strcmp(f, "ahead_behind") &&
	    strcmp(f, "staged") && strcmp(f, "modified") &&
	    strcmp(f, "deleted") && strcmp(f, "untracked") &&
	    strcmp(f, "conflicted") && strcmp(f, "renamed") &&
	    strcmp(f, "ahead") && strcmp(f, "behind"))
		return 0;
	t = st_get(c);
	if (!t)
		return 0;
	s_init(&o);
	if (!strcmp(f, "status")) {
		gt_count(c, &o, t->conflicted, "conflicted_symbol", "=");
		gt_count(c, &o, t->staged, "staged_symbol", "+");
		gt_count(c, &o, t->modified, "modified_symbol", "!");
		gt_count(c, &o, t->renamed, "renamed_symbol", "\302\273");
		gt_count(c, &o, t->deleted, "deleted_symbol", "-");
		gt_count(c, &o, t->untracked, "untracked_symbol", "?");
		gt_count(c, &o, c->g->stash, "stash_symbol", "$");
	} else if (!strcmp(f, "ahead_behind")) {
		gt_count(c, &o, t->ahead, "ahead_symbol", "\342\206\221");
		gt_count(c, &o, t->behind, "behind_symbol", "\342\206\223");
	} else {
		int n = !strcmp(f, "staged")	  ? t->staged
			: !strcmp(f, "modified")  ? t->modified
			: !strcmp(f, "deleted")	  ? t->deleted
			: !strcmp(f, "untracked") ? t->untracked
			: !strcmp(f, "conflicted") ? t->conflicted
			: !strcmp(f, "renamed")	  ? t->renamed
			: !strcmp(f, "ahead")	  ? t->ahead
						  : t->behind;
		if (n)
			s_num(&o, n);
	}
	if (!o.n) {
		s_free(&o);
		return 0;
	}
	return o.p;
}

/* Read one value from the repository configuration file. */
char *gt_cfg(grepo *g, const char *sect, const char *sub, const char *key)
{
	const char *p;
	int in = 0;
	char *found = 0;

	if (!g->cfgdone) {
		g->cfgdone = 1;
		g->cfgtxt = gt_read(g->common, "config", 1 << 20);
	}
	if (!g->cfgtxt)
		return 0;
	p = g->cfgtxt;
	while (*p) {
		const char *b = p, *e;
		while (*p && *p != '\n')
			p++;
		e = p;
		if (*p)
			p++;
		while (b < e && (*b == ' ' || *b == '\t'))
			b++;
		while (e > b && (e[-1] == ' ' || e[-1] == '\t' ||
				 e[-1] == '\r'))
			e--;
		if (b >= e || *b == '#' || *b == ';')
			continue;
		if (*b == '[') {
			str nm;
			const char *q = b + 1;
			s_init(&nm);
			while (q < e && *q != ']' && *q != ' ' && *q != '\t')
				s_ch(&nm, *q++);
			in = !strcmp(nm.p ? nm.p : "", sect);
			s_free(&nm);
			if (in && sub) {
				const char *s1 = memchr(b, '"', (size_t)(e - b));
				const char *s2 = s1 ? memchr(s1 + 1, '"',
							     (size_t)(e - s1 - 1))
						    : 0;
				in = s1 && s2 &&
				     (size_t)(s2 - s1 - 1) == strlen(sub) &&
				     !strncmp(s1 + 1, sub, strlen(sub));
			} else if (in && !sub) {
				in = !memchr(b, '"', (size_t)(e - b));
			}
			continue;
		}
		if (!in)
			continue;
		{
			const char *eq = memchr(b, '=', (size_t)(e - b));
			const char *ke = eq ? eq : e;
			while (ke > b && (ke[-1] == ' ' || ke[-1] == '\t'))
				ke--;
			if ((size_t)(ke - b) != strlen(key) ||
			    strncmp(b, key, strlen(key)))
				continue;
			if (!eq) {
				found = xs("true");
				break;
			}
			eq++;
			while (eq < e && (*eq == ' ' || *eq == '\t'))
				eq++;
			if (eq < e && *eq == '"' && e[-1] == '"') {
				eq++;
				e--;
			}
			found = pr_span(eq, e);
			break;
		}
	}
	return found;
}

/* Resolve one reference to an object name, loose or packed. */
int gt_ref(grepo *g, const char *ref, unsigned char *sha)
{
	char *t = gt_read(g->dir, ref, 4096);
	char *pr;
	const char *p;

	if (!t && strncmp(ref, "refs/", 5) == 0)
		t = gt_read(g->common, ref, 4096);
	if (t && !strncmp(t, "ref: ", 5)) {
		char *nx = xs(t + 5);
		int r;
		free(t);
		r = gt_ref(g, nx, sha);
		free(nx);
		return r;
	}
	if (t && strlen(t) >= 40 && ob_hex(t, sha)) {
		free(t);
		return 1;
	}
	free(t);
	pr = gt_read(g->common, "packed-refs", 1 << 22);
	if (!pr)
		return 0;
	p = pr;
	while (*p) {
		const char *b = p, *e;
		while (*p && *p != '\n')
			p++;
		e = p;
		if (*p)
			p++;
		if (e - b < 42 || *b == '#' || *b == '^')
			continue;
		if ((size_t)(e - b) == 41 + strlen(ref) &&
		    b[40] == ' ' && !strncmp(b + 41, ref, strlen(ref))) {
			int ok = ob_hex(b, sha);
			free(pr);
			return ok;
		}
	}
	free(pr);
	return 0;
}

/* Resolve HEAD, reporting whether the branch has any commit yet. */
int gt_headsha(grepo *g, unsigned char *sha)
{
	return gt_ref(g, "HEAD", sha);
}
