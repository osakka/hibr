#include "pri.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

char *xnum(sh *s, long v);
char *xone(sh *s, word *w);

/* Translate a wait status into a shell exit code. */
int wstat(int w)
{
	if (WIFEXITED(w))
		return WEXITSTATUS(w);
	if (WIFSIGNALED(w))
		return 128 + WTERMSIG(w);
	return HIBR_FAIL;
}

/* Release a heap environment vector. */
void envfree(char **e)
{
	char **p = e;

	while (p && *p)
		free(*p++);
	free(e);
}

/* The remembered path for a command name, or null when there is none. */
const char *hsh_get(sh *s, const char *nm)
{
	size_t i, n = strlen(nm);
	const char *e;

	for (i = 0; i < s->cmds.n; i++) {
		e = (const char *)s->cmds.p[i];
		if (!strncmp(e, nm, n) && e[n] == '=')
			return e + n + 1;
	}
	return 0;
}

/* Forget one remembered command, or all of them. */
void hsh_clear(sh *s, const char *nm)
{
	size_t i, n = nm ? strlen(nm) : 0;
	const char *e;

	for (i = s->cmds.n; i--;) {
		e = (const char *)s->cmds.p[i];
		if (nm && (strncmp(e, nm, n) || e[n] != '='))
			continue;
		free(s->cmds.p[i]);
		s->cmds.p[i] = s->cmds.p[--s->cmds.n];
	}
	lg(HIBR_LDBG, "forgot %s", nm ? nm : "every remembered command");
}

/* Remember where a command was found. */
void hsh_put(sh *s, const char *nm, const char *path)
{
	str b;

	hsh_clear(s, nm);
	s_init(&b);
	s_cat(&b, nm);
	s_ch(&b, '=');
	s_cat(&b, path);
	v_add(&s->cmds, b.p);
}

/* Locate an executable along PATH. */
char *findx(sh *s, const char *nm)
{
	const char *p = hibr_get(s, "PATH");
	const char *q;
	str b;

	if (strchr(nm, '/'))
		return access(nm, X_OK) == 0 ? xs(nm) : 0;
	q = hsh_get(s, nm);
	if (q) {
		if (access(q, X_OK) == 0)
			return xs(q);
		lg(HIBR_LDBG, "%s moved; looking again", nm);
		hsh_clear(s, nm);
	}
	if (!p)
		p = "/usr/local/bin:/usr/bin:/bin";
	while (*p) {
		size_t n;
		q = strchr(p, ':');
		n = q ? (size_t)(q - p) : strlen(p);
		s_init(&b);
		if (n)
			s_add(&b, p, n);
		else
			s_ch(&b, '.');
		s_ch(&b, '/');
		s_cat(&b, nm);
		if (access(b.p, X_OK) == 0) {
			hsh_put(s, nm, b.p);
			return b.p;
		}
		s_free(&b);
		if (!q)
			break;
		p = q + 1;
	}
	return 0;
}

/* Give a here-document body a readable descriptor. */
int hdfd(const char *t, size_t n)
{
	int pf[2], fd;
	const char *td;
	str nm;

	if (n < HIBR_HDMAX) {
		if (pipe(pf) < 0) {
			lg(HIBR_LERR, "pipe: %s", strerror(errno));
			return -1;
		}
		if (write(pf[1], t, n) != (ssize_t)n) {
			lg(HIBR_LERR, "here-document write failed");
			close(pf[0]);
			close(pf[1]);
			return -1;
		}
		close(pf[1]);
		return pf[0];
	}
	td = getenv("TMPDIR");
	s_init(&nm);
	s_cat(&nm, td && *td ? td : "/tmp");
	s_cat(&nm, "/hibr-hd-XXXXXX");
	fd = mkstemp(nm.p);
	if (fd < 0) {
		lg(HIBR_LERR, "here-document: %s", strerror(errno));
		s_free(&nm);
		return -1;
	}
	unlink(nm.p);
	s_free(&nm);
	if (write(fd, t, n) != (ssize_t)n || lseek(fd, 0, SEEK_SET) < 0) {
		lg(HIBR_LERR, "here-document spill failed");
		close(fd);
		return -1;
	}
	lg(HIBR_LDBG, "here-document spilled to a temporary file (%lu bytes)",
	   (unsigned long)n);
	return fd;
}

/* Record a descriptor so a redirection can be undone. */
void rd_save(vec *sv, int fd)
{
	int *e = xm(2 * sizeof *e);

	e[0] = fd;
	e[1] = dup(fd);
	v_add(sv, e);
}

/* Apply a redirection list, optionally recording the old descriptors. */
int rd_do(sh *s, redir *r, vec *sv)
{
	amark m = ar_mark(s->xa);
	int st = HIBR_OK;
	int fd;
	char *t;

	if (r)
		fflush(0);
	for (; r; r = r->nx) {
		t = xone(s, r->w);
		fd = -1;
		if (r->k != R_DUP && r->k != R_HERE && net_is(s, t)) {
			fd = net_open(s, t);
			if (fd < 0) {
				st = HIBR_FAIL;
				break;
			}
			if (sv)
				rd_save(sv, r->fd);
			if (fd != r->fd) {
				dup2(fd, r->fd);
				close(fd);
			}
			continue;
		}
		switch (r->k) {
		case R_IN:
			fd = open(t, O_RDONLY);
			break;
		case R_OUT: {
			struct stat rs;
			int guard = s->noclob && !(r->fl & RF_CLOB) &&
				    !(stat(t, &rs) == 0 && !S_ISREG(rs.st_mode));
			fd = open(t, O_WRONLY | O_CREAT | O_TRUNC |
					    (guard ? O_EXCL : 0),
				  0666);
			if (fd < 0 && guard)
				lg(HIBR_LERR, "%s: exists (noclobber)", t);
			break;
		}
		case R_APP:
			fd = open(t, O_WRONLY | O_CREAT | O_APPEND, 0666);
			break;
		case R_RW:
			fd = open(t, O_RDWR | O_CREAT, 0666);
			break;
		case R_HERE:
			if (r->fl & RF_STR) {
				str hs;
				s_init(&hs);
				s_cat(&hs, t);
				s_ch(&hs, '\n');
				fd = hdfd(hs.p, hs.n);
				s_free(&hs);
				break;
			}
			fd = hdfd(t, strlen(t));
			if (fd < 0) {
				st = HIBR_FAIL;
				r = 0;
			}
			break;
		case R_DUP: {
			int tgt = r->fd;
			if (r->var) {
				const char *cur = hibr_get(s, r->var);
				if (cur && *cur)
					tgt = atoi(cur);
			}
			if (sv)
				rd_save(sv, tgt);
			if (!strcmp(t, "-")) {
				close(tgt);
				lg(HIBR_LTRC, "closed fd %d", tgt);
				continue;
			}
			fd = atoi(t);
			if (dup2(fd, tgt) < 0) {
				lg(HIBR_LERR, "%d: bad file descriptor", fd);
				st = HIBR_FAIL;
			}
			continue;
		}
		}
		if (fd < 0) {
			if (st == HIBR_OK)
				lg(HIBR_LERR, "%s: %s", t, strerror(errno));
			st = HIBR_FAIL;
			break;
		}
		if (r->var) {
			str nb;
			int hi = fcntl(fd, F_DUPFD, 10);
			if (hi >= 0) {
				close(fd);
				fd = hi;
			}
			s_init(&nb);
			s_num(&nb, (long)fd);
			hibr_set(s, r->var, nb.p, 0);
			s_free(&nb);
			lg(HIBR_LDBG, "descriptor %d in %s", fd, r->var);
			continue;
		}
		if (sv)
			rd_save(sv, r->fd);
		if (fd != r->fd) {
			dup2(fd, r->fd);
			close(fd);
		}
		if (r->fl & RF_BOTH) {
			if (sv)
				rd_save(sv, 2);
			dup2(r->fd, 2);
		}
		lg(HIBR_LTRC, "redirect fd %d -> %s", r->fd, t);
	}
	ar_rel(s->xa, m);
	return st;
}

/* Restore descriptors saved by a redirection. */
void rd_undo(vec *sv)
{
	size_t i = sv->n;
	int *e;

	if (i)
		fflush(0);
	while (i--) {
		e = (int *)sv->p[i];
		if (e[1] >= 0) {
			dup2(e[1], e[0]);
			close(e[1]);
		} else {
			close(e[0]);
		}
		free(e);
	}
	v_free(sv);
}

/* Find a shell function by name. */
node *fn_find(sh *s, const char *nm)
{
	size_t i;
	node *f;

	for (i = 0; i < s->fns.n; i++) {
		f = (node *)s->fns.p[i];
		if (!strcmp(f->s, nm))
			return f;
	}
	return 0;
}

/* Register or replace a shell function. */
void fn_add(sh *s, node *f)
{
	size_t i;
	node *o;

	for (i = 0; i < s->fns.n; i++) {
		o = (node *)s->fns.p[i];
		if (!strcmp(o->s, f->s)) {
			s->fns.p[i] = f;
			lg(HIBR_LDBG, "redefined function %s", f->s);
			return;
		}
	}
	v_add(&s->fns, f);
	lg(HIBR_LDBG, "defined function %s", f->s);
}

/* Check a value against a declared parameter type. */
int ty_ok(const char *ty, const char *v)
{
	size_t i = 0;

	if (!ty || !*ty || !strcmp(ty, "str") || !strcmp(ty, "any"))
		return 1;
	if (!strcmp(ty, "int") || !strcmp(ty, "num")) {
		int dot = 0;
		if (v[i] == '-' || v[i] == '+')
			i++;
		if (!v[i])
			return 0;
		for (; v[i]; i++) {
			if (v[i] == '.' && !dot && !strcmp(ty, "num")) {
				dot = 1;
				continue;
			}
			if (!isdigit((unsigned char)v[i]))
				return 0;
		}
		return 1;
	}
	if (!strcmp(ty, "path"))
		return *v != 0;
	if (!strcmp(ty, "arr") || !strcmp(ty, "map"))
		return 1;
	lg(HIBR_LWRN, "unknown parameter type %s, not checked", ty);
	return 1;
}

/* Bind declared parameters into the function's scope. */
int fn_bind(sh *s, node *f, vec *fr, int ac, char **av)
{
	node *pm;
	char *val;
	int i = 1;

	for (pm = f->x; pm; pm = pm->x) {
		if (pm->f) {
			vec *rest = vb_get(s);
			for (; i < ac; i++)
				v_add(rest, av[i]);
			asg_keep(s, fr, pm->s);
			v_arr(s, pm->s, rest);
			vb_put(s, rest);
			return HIBR_OK;
		}
		if (i < ac) {
			val = av[i++];
		} else if (pm->w) {
			val = xone(s, pm->w);
		} else {
			lg(HIBR_LERR, "%s: missing argument %s", f->s, pm->s);
			return HIBR_FAIL;
		}
		if (!ty_ok(pm->tx, val)) {
			lg(HIBR_LERR, "%s: %s expects %s, got '%s'", f->s, pm->s,
			   pm->tx, val);
			return HIBR_FAIL;
		}
		asg_keep(s, fr, pm->s);
		if (pm->tx && (!strcmp(pm->tx, "arr") || !strcmp(pm->tx, "map")))
			v_copy(s, pm->s, val);
		else
			hibr_set(s, pm->s, val, 0);
	}
	if (i < ac) {
		lg(HIBR_LERR, "%s: too many arguments (%d given)", f->s, ac - 1);
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Invoke a shell function with its own positional parameters. */
int fn_call(sh *s, node *f, int ac, char **av)
{
	char **oav = s->av;
	int oac = s->ac, oavo = s->avo, st;
	vec *fr = vb_get(s);

	char *orty = s->rty;

	v_add(&s->scope, fr);
	s->av = av + 1;
	s->ac = ac - 1;
	s->avo = 0;
	s->dep++;
	s->rty = f->rt;
	if (f->f && fn_bind(s, f, fr, ac, av) != HIBR_OK)
		st = 2;
	else
		st = ex(s, f->r);
	if (s->ret) {
		s->ret = 0;
		st = s->st;
	}
	s->dep--;
	s->scope.n--;
	asg_pop(s, fr);
	vb_put(s, fr);
	if (s->avo && s->av) {
		int i;
		for (i = 0; i < s->ac; i++)
			free(s->av[i]);
		free(s->av);
	}
	s->av = oav;
	s->ac = oac;
	s->avo = oavo;
	s->rty = orty;
	return st;
}

/* Capture the standard output of a command substitution. */
char *xcap(sh *s, const char *src)
{
	int pf[2];
	pid_t pid;
	str o;
	char *buf;
	ssize_t n;
	char *r;
	int w;

	if (pipe(pf) < 0) {
		lg(HIBR_LERR, "pipe: %s", strerror(errno));
		return ar_dup(s->xa, "", 0);
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		close(pf[0]);
		close(pf[1]);
		return ar_dup(s->xa, "", 0);
	}
	if (pid == 0) {
		close(pf[0]);
		dup2(pf[1], 1);
		close(pf[1]);
		signal(SIGINT, SIG_DFL);
		s->it = 0;
		tr_fork(s);
		hibr_run(s, src);
		w = s->st;
		tr_exit(s);
		fflush(0);
		_exit(w);
	}
	close(pf[1]);
	s_init(&o);
	buf = xm(HIBR_IOCH);
	while ((n = read(pf[0], buf, HIBR_IOCH)) > 0)
		s_add(&o, buf, (size_t)n);
	free(buf);
	close(pf[0]);
	waitpid(pid, &w, 0);
	s->st = wstat(w);
	while (o.n && o.p[o.n - 1] == '\n')
		o.n--;
	r = ar_dup(s->xa, o.p ? o.p : "", o.n);
	s_free(&o);
	return r;
}

/* Remember a variable's current binding so it can be restored. */
void asg_keep(sh *s, vec *old, const char *k)
{
	var *v = v_find(s, k);
	struct sav *sv = xm(sizeof *sv);

	sv->k = xs(k);
	sv->v = v ? xs(v->v) : 0;
	sv->ex = v ? v->ex : 0;
	v_add(old, sv);
}

/* Apply temporary assignments, remembering the previous values. */
void asg_push(sh *s, vec *asg, vec *old)
{
	size_t i;
	char *kv, *q;

	for (i = 0; i < asg->n; i++) {
		kv = (char *)asg->p[i];
		q = strchr(kv, '=');
		if (!q)
			continue;
		*q = 0;
		asg_keep(s, old, kv);
		hibr_set(s, kv, q + 1, 1);
		*q = '=';
	}
}

/* Restore variables saved by a temporary assignment. */
void asg_pop(sh *s, vec *old)
{
	size_t i = old->n;
	struct sav *sv;

	while (i--) {
		sv = (struct sav *)old->p[i];
		if (sv->v) {
			hibr_set(s, sv->k, sv->v, sv->ex);
			free(sv->v);
		} else {
			v_del(s, sv->k);
		}
		free(sv->k);
		free(sv);
	}
	v_free(old);
}

/* Run a process substitution and name the descriptor it reads or writes. */
char *xpsub(sh *s, part *p)
{
	int pf[2], fd, w;
	int *rec;
	pid_t pid;
	str nm;
	char *r;

	if (pipe(pf) < 0) {
		lg(HIBR_LERR, "pipe: %s", strerror(errno));
		return ar_dup(s->xa, "/dev/null", 9);
	}
	fflush(0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		close(pf[0]);
		close(pf[1]);
		return ar_dup(s->xa, "/dev/null", 9);
	}
	if (pid == 0) {
		if (p->op) {
			close(pf[0]);
			dup2(pf[1], 1);
			close(pf[1]);
		} else {
			close(pf[1]);
			dup2(pf[0], 0);
			close(pf[0]);
		}
		signal(SIGINT, SIG_DFL);
		s->it = 0;
		tr_fork(s);
		hibr_run(s, p->t);
		w = s->st;
		tr_exit(s);
		fflush(0);
		_exit(w);
	}
	if (p->op) {
		close(pf[1]);
		fd = pf[0];
	} else {
		close(pf[0]);
		fd = pf[1];
	}
	rec = xm(2 * sizeof *rec);
	rec[0] = fd;
	rec[1] = (int)pid;
	v_add(&s->psub, rec);
	s_init(&nm);
	s_cat(&nm, "/dev/fd/");
	s_num(&nm, fd);
	r = ar_dup(s->xa, nm.p, nm.n);
	s_free(&nm);
	lg(HIBR_LDBG, "process substitution on %s", r);
	return r;
}

/* Close the descriptors a command's process substitutions used. */
void xpsub_done(sh *s)
{
	size_t i;
	int *rec, w;

	for (i = 0; i < s->psub.n; i++) {
		rec = (int *)s->psub.p[i];
		close(rec[0]);
		waitpid((pid_t)rec[1], &w, WNOHANG);
		free(rec);
	}
	s->psub.n = 0;
}

/* Apply a name=value assignment to the shell variable table. */
int ex_asg(sh *s, char *kv, const char *mask, int ex_flag)
{
	char *q = strchr(kv, '=');
	char *br, *r, *plus = 0;
	vec *ks;
	str cat;

	if (!q)
		return HIBR_OK;
	*q = 0;
	if (q > kv && q[-1] == '+') {
		plus = q - 1;
		*plus = 0;
	}
	br = strchr(kv, '[');
	if (!br) {
		int rc;
		if (plus) {
			const char *old = hibr_get(s, kv);
			s_init(&cat);
			s_cat(&cat, old ? old : "");
			s_cat(&cat, q + 1);
			rc = hibr_set(s, kv, cat.p, ex_flag);
			s_free(&cat);
			*plus = '+';
		} else {
			rc = hibr_set(s, kv, q + 1, ex_flag);
		}
		*q = '=';
		return rc;
	}
	ks = vb_get(s);
	*br = 0;
	for (r = br + 1; r && *r;) {
		char *end = strchr(r, ']');
		if (!end)
			break;
		*end = 0;
		v_add(ks, xkey_q(s, r, mask ? mask + (r - kv) : 0));
		r = end + 1;
		if (*r == '[')
			r++;
		else
			break;
	}
	if (plus) {
		const char *old = v_getp(s, kv, (char **)ks->p, (int)ks->n);
		s_init(&cat);
		s_cat(&cat, old ? old : "");
		s_cat(&cat, q + 1);
		v_setp(s, kv, (char **)ks->p, (int)ks->n, cat.p);
		s_free(&cat);
		*plus = '+';
	} else {
		v_setp(s, kv, (char **)ks->p, (int)ks->n, q + 1);
	}
	vb_put(s, ks);
	*br = '[';
	*q = '=';
	return s->stop ? HIBR_FAIL : HIBR_OK;
}

/* Print an execution trace line. */
void ex_trace(char **av, int ac)
{
	int i;

	fputs("+ ", stderr);
	for (i = 0; i < ac; i++) {
		fputs(av[i], stderr);
		if (i + 1 < ac)
			fputc(' ', stderr);
	}
	fputc('\n', stderr);
}

/* Execute a simple command, builtin, function or external program. */
int ex_cmd(sh *s, node *n)
{
	amark m = ar_mark(s->xa);
	vec none = { 0, 0, 0 };
	vec *asg = &none, *asgm = &none;
	vec sv = { 0, 0, 0 };
	char **av, **env, **am = 0;
	char *path;
	job *jb = 0;
	node *f;
	const hibr_bi *b = 0;
	word *w;
	size_t i;
	int ac = 0, st = 0, w2;
	pid_t pid;

	if (s->dtrap)
		tr_debug(s, n->tx);
	if (n->aw) {
		asg = vb_get(s);
		asgm = vb_get(s);
		for (w = n->aw; w; w = w->nx) {
			char *mk = 0;
			v_add(asg, xone_q(s, w, &mk));
			v_add(asgm, mk);
		}
	}
	for (f = n->x; f; f = f->x) {
		vec *el = vb_get(s);
		for (w = f->w; w; w = w->nx)
			xw(s, w, el, 0);
		if (f->f == 3 && s->scope.n)
			asg_keep(s, (vec *)s->scope.p[s->scope.n - 1], f->s);
		{
			size_t nl = strlen(f->s);
			if (nl > 1 && f->s[nl - 1] == '+') {
				char *nm = ar_dup(s->xa, f->s, nl - 1);
				vec *cur = vb_get(s);
				size_t z;
				long next = 0;
				v_list(s, nm, 0, 0, cur, 1);
				for (z = 0; z < cur->n; z++) {
					long kk = atol((char *)cur->p[z]);
					if (kk >= next)
						next = kk + 1;
				}
				vb_put(s, cur);
				for (z = 0; z < el->n; z++)
					v_setel(s, nm, next + (long)z, (char *)el->p[z]);
				if (!el->n && !v_find(s, nm)) {
					vec empty = { 0, 0, 0 };
					v_arr(s, nm, &empty);
				}
			} else {
				v_arr(s, f->s, el);
			}
		}
		vb_put(s, el);
	}
	av = xargv(s, n->w, &ac, &am);
	if (s->xerr) {
		s->xerr = 0;
		st = HIBR_FAIL;
		goto out;
	}
	if (s->stop || s->quit) {
		st = s->st ? s->st : HIBR_FAIL;
		goto out;
	}
	if (!ac) {
		for (i = 0; i < asg->n; i++)
			if (ex_asg(s, (char *)asg->p[i],
				   i < asgm->n ? (const char *)asgm->p[i] : 0,
				   0) != HIBR_OK)
				st = s->st ? s->st : HIBR_FAIL;
		if (n->rd && rd_do(s, n->rd, &sv) == HIBR_OK)
			rd_undo(&sv);
		else if (n->rd)
			st = HIBR_FAIL;
		goto out;
	}
	if (s->xtr)
		ex_trace(av, ac);
	if (n->f == 2 && n->s) {
		hibr_set(s, "RET", "", 0);
		s->bind = 1;
	}
	{
		const char *al = al_get(s, av[0]);
		if (al && !al_busy(s, av[0])) {
			if (rd_do(s, n->rd, &sv) == HIBR_OK)
				st = al_run(s, al, ac, av);
			else
				st = HIBR_FAIL;
			rd_undo(&sv);
			goto out;
		}
	}
	f = fn_find(s, av[0]);
	if (f)
		s->bind = 0;
	if (!f) {
		b = m_find(s, av[0]);
		if (!b)
			b = bi_find(av[0]);
	}
	if (b && ac == 1 && !strcmp(av[0], "exec")) {
		st = rd_do(s, n->rd, 0);
		lg(HIBR_LDBG, "exec: redirections made permanent");
		goto out;
	}
	if (f || b) {
		vec old = { 0, 0, 0 };
		asg_push(s, asg, &old);
		if (rd_do(s, n->rd, &sv) == HIBR_OK) {
			if (f) {
				st = fn_call(s, f, ac, av);
			} else {
				char **oam = s->amask;
				s->amask = am;
				st = b->fn(s, ac, av);
				s->amask = oam;
			}
		} else
			st = HIBR_FAIL;
		rd_undo(&sv);
		asg_pop(s, &old);
		goto out;
	}
	path = findx(s, av[0]);
	if (!path) {
		lg(HIBR_LERR, "%s: command not found", av[0]);
		st = HIBR_NOCMD;
		goto out;
	}
	env = v_envp(s, asg);
	fflush(0);
	if (s->nofork) {
		if (rd_do(s, n->rd, 0) != HIBR_OK)
			_exit(HIBR_FAIL);
		execve(path, av, env);
		lg(HIBR_LERR, "%s: %s", path, strerror(errno));
		_exit(errno == ENOENT ? HIBR_NOCMD : HIBR_NOEXEC);
	}
	if (s->it)
		jb = jc_new(s, n->tx, 0);
	pid = fork();
	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		free(path);
		envfree(env);
		st = HIBR_FAIL;
		goto out;
	}
	if (pid == 0) {
		if (jb) {
			setpgid(0, 0);
			tcsetpgrp(s->tty, getpid());
			signal(SIGTSTP, SIG_DFL);
			signal(SIGTTIN, SIG_DFL);
			signal(SIGTTOU, SIG_DFL);
		}
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		if (rd_do(s, n->rd, 0) != HIBR_OK)
			_exit(HIBR_FAIL);
		execve(path, av, env);
		lg(HIBR_LERR, "%s: %s", path, strerror(errno));
		_exit(errno == ENOENT ? HIBR_NOCMD : HIBR_NOEXEC);
	}
	free(path);
	envfree(env);
	if (jb) {
		setpgid(pid, pid);
		jc_pid(s, jb, (long)pid);
		st = jc_fg(s, jb);
	} else {
		waitpid(pid, &w2, 0);
		st = wstat(w2);
	}
out:
	if (n->f == 2 && n->s) {
		v_copy(s, n->s, "RET");
		s->bind = 0;
	}
	if (asg != &none) {
		vb_put(s, asg);
		vb_put(s, asgm);
	}
	if (s->psub.n)
		xpsub_done(s);
	ar_rel(s->xa, m);
	s->st = st;
	return st;
}

/* Stop the shell, or the current input unit, when errexit is armed. */
int ex_chk(sh *s, int st, int tested)
{
	if (st && !tested && !s->stop)
		tr_err(s, st);
	if (!st || !s->errx || tested || s->stop || s->intry)
		return st;
	lg(HIBR_LERR, "errexit: command failed with status %d", st);
	s->stop = 1;
	s->st = st;
	if (!s->it)
		s->quit = 1;
	return st;
}

/* Flatten a pipeline into its ordered stages. */
void pflat(node *n, vec *v)
{
	if (n->k == N_PIPE) {
		pflat(n->l, v);
		pflat(n->r, v);
		return;
	}
	v_add(v, n);
}

/* Execute a pipeline, one process per stage. */
int ex_pipe(sh *s, node *n)
{
	vec st = { 0, 0, 0 }, pids = { 0, 0, 0 };
	int in = -1, pf[2], w, rc = 0;
	size_t i;
	pid_t pid;
	job *jb = 0;
	node *q;

	pflat(n, &st);
	if (s->it)
		jb = jc_new(s, n->tx, 0);
	for (i = 0; i < st.n; i++) {
		fflush(0);
		if (i + 1 < st.n && pipe(pf) < 0) {
			lg(HIBR_LERR, "pipe: %s", strerror(errno));
			break;
		}
		pid = fork();
		if (pid < 0) {
			lg(HIBR_LERR, "fork: %s", strerror(errno));
			break;
		}
		if (pid == 0) {
			q = (node *)st.p[i];
			if (jb) {
				setpgid(0, (pid_t)jb->pgid);
				if (!jb->pgid)
					tcsetpgrp(s->tty, getpid());
				signal(SIGTSTP, SIG_DFL);
				signal(SIGTTIN, SIG_DFL);
				signal(SIGTTOU, SIG_DFL);
			}
			if (in >= 0) {
				dup2(in, 0);
				close(in);
			}
			if (i + 1 < st.n) {
				close(pf[0]);
				dup2(pf[1], 1);
				close(pf[1]);
			}
			signal(SIGINT, SIG_DFL);
			s->it = 0;
			s->errx = 0;
			tr_fork(s);
			if (q->k == N_CMD)
				s->nofork = 1;
			rc = ex(s, q);
			tr_exit(s);
			fflush(0);
			_exit(rc);
		}
		if (in >= 0)
			close(in);
		if (i + 1 < st.n) {
			close(pf[1]);
			in = pf[0];
		}
		if (jb) {
			setpgid(pid, (pid_t)(jb->pgid ? jb->pgid : (long)pid));
			jc_pid(s, jb, (long)pid);
		}
		v_add(&pids, (void *)(long)pid);
	}
	if (in >= 0)
		close(in);
	if (jb) {
		v_free(&pids);
		v_free(&st);
		return s->st = jc_fg(s, jb);
	}
	s->pfs = 0;
	for (i = 0; i < pids.n; i++) {
		waitpid((pid_t)(long)pids.p[i], &w, 0);
		rc = wstat(w);
		if (rc && !s->pfs)
			s->pfs = rc;
	}
	v_free(&pids);
	v_free(&st);
	s->st = rc;
	return rc;
}

/* Run a command in the background. */
int ex_bg(sh *s, node *n)
{
	pid_t pid;
	int w;
	job *jb = jc_new(s, n->tx, 1);

	fflush(0);
	pid = fork();

	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		jc_drop(s, jb);
		return s->st = HIBR_FAIL;
	}
	if (pid == 0) {
		setpgid(0, 0);
		signal(SIGINT, SIG_IGN);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		s->it = 0;
		tr_fork(s);
		if (n->l && n->l->k == N_CMD)
			s->nofork = 1;
		w = ex(s, n->l);
		tr_exit(s);
		fflush(0);
		_exit(w);
	}
	setpgid(pid, pid);
	jc_pid(s, jb, (long)pid);
	hibr_set(s, "!", xnum(s, (long)pid), 0);
	if (s->it)
		jc_bgnote(s, jb);
	lg(HIBR_LINF, "background pid %ld", (long)pid);
	return s->st = 0;
}

/* Run a command list inside a subshell process. */
int ex_sub(sh *s, node *n)
{
	pid_t pid;
	int w;

	fflush(0);
	pid = fork();

	if (pid < 0) {
		lg(HIBR_LERR, "fork: %s", strerror(errno));
		return s->st = HIBR_FAIL;
	}
	if (pid == 0) {
		signal(SIGINT, SIG_DFL);
		s->it = 0;
		tr_fork(s);
		if (rd_do(s, n->rd, 0) != HIBR_OK)
			_exit(HIBR_FAIL);
		w = ex(s, n->l);
		tr_exit(s);
		fflush(0);
		_exit(w);
	}
	waitpid(pid, &w, 0);
	return s->st = wstat(w);
}

/* Execute a while or until loop. */
int ex_loop(sh *s, node *n)
{
	int st = 0, c;

	for (;;) {
		if (s->quit || s->stop || s->ret)
			break;
		s->tst = 1;
		c = ex(s, n->l);
		s->tst = 0;
		if (n->k == N_UNTIL ? c == 0 : c != 0)
			break;
		st = ex(s, n->r);
		if (s->brk) {
			s->brk--;
			break;
		}
		if (s->cont) {
			s->cont--;
			if (s->cont)
				break;
		}
		if (s->quit || s->stop || s->ret)
			break;
	}
	return s->st = st;
}

/* Execute a for loop over a word list or the positional parameters. */
int ex_for(sh *s, node *n)
{
	amark m = ar_mark(s->xa);
	vec o = { 0, 0, 0 };
	word *w;
	size_t i;
	int st = 0;

	if (n->f)
		for (w = n->w; w; w = w->nx)
			xw(s, w, &o, 0);
	else
		for (i = 0; i < (size_t)s->ac; i++)
			v_add(&o, s->av[i]);
	for (i = 0; i < o.n; i++) {
		hibr_set(s, n->s, (char *)o.p[i], 0);
		st = ex(s, n->r);
		if (s->brk) {
			s->brk--;
			break;
		}
		if (s->cont) {
			s->cont--;
			if (s->cont)
				break;
		}
		if (s->quit || s->stop || s->ret)
			break;
	}
	v_free(&o);
	ar_rel(s->xa, m);
	return s->st = st;
}

/* Execute a case statement. */
int ex_case(sh *s, node *n)
{
	amark m = ar_mark(s->xa);
	char *sub = xone(s, n->w);
	node *cl;
	word *w;
	int st = 0, hit = 0;

	for (cl = n->l; cl; cl = cl->x) {
		if (!hit) {
			for (w = cl->w; w; w = w->nx)
				if (gmatch(xpat(s, w), sub)) {
					hit = 1;
					break;
				}
			if (!hit)
				continue;
		}
		lg(HIBR_LTRC, "case: clause matched %s", sub);
		st = ex(s, cl->r);
		if (cl->f == 1)
			continue;
		if (cl->f == 2) {
			hit = 0;
			continue;
		}
		break;
	}
	ar_rel(s->xa, m);
	return s->st = st;
}

/* Execute a C-style for loop. */
int ex_cfor(sh *s, node *n)
{
	word *wi = n->w, *wc = wi ? wi->nx : 0, *wstep = wc ? wc->nx : 0;
	int st = 0;

	if (wi && wi->p->n)
		ax_text(s, wi->p->t);
	for (;;) {
		if (s->quit || s->stop || s->ret)
			break;
		if (wc && wc->p->n && !ax_text(s, wc->p->t))
			break;
		if (s->xerr) {
			s->xerr = 0;
			st = HIBR_FAIL;
			break;
		}
		st = ex(s, n->r);
		if (s->brk) {
			s->brk--;
			break;
		}
		if (s->cont) {
			s->cont--;
			if (s->cont)
				break;
		}
		if (s->quit || s->stop || s->ret)
			break;
		if (wstep && wstep->p->n)
			ax_text(s, wstep->p->t);
	}
	return s->st = st;
}

struct cx { sh *s; word **it; size_t n, i; int bad, skip, depth; };

int cx_or(struct cx *c);

/* The operator text of a [[ ]] item, or NULL for an operand. */
const char *cx_op(struct cx *c, size_t i)
{
	if (i >= c->n)
		return 0;
	return c->it[i]->p && c->it[i]->p->op == -1 ? c->it[i]->p->t : 0;
}

/* True if the item at i is the literal word given. */
int cx_is(struct cx *c, size_t i, const char *t)
{
	const char *l;

	if (i >= c->n)
		return 0;
	if (cx_op(c, i))
		return !strcmp(cx_op(c, i), t);
	l = w_lit(c->it[i]);
	return l && !strcmp(l, t);
}

/* Expand an operand, unless this branch is being skipped. */
char *cx_val(struct cx *c, size_t i)
{
	word *w = c->it[i];

	if (c->skip)
		return (char *)"";
	if (w->p && w->p->op == -2)
		return w->p->t;
	return xone(c->s, w);
}

/* Compare modification times or identity of two paths. */
int cx_files(const char *a, const char *op, const char *b)
{
	struct stat x, y;
	int hx = stat(a, &x) == 0, hy = stat(b, &y) == 0;

	if (!strcmp(op, "-nt"))
		return hx && (!hy || x.st_mtime > y.st_mtime);
	if (!strcmp(op, "-ot"))
		return hy && (!hx || x.st_mtime < y.st_mtime);
	return hx && hy && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}

/* Evaluate a primary: group, unary test, binary test or bare string. */
int cx_prim(struct cx *c)
{
	const char *bin[] = { "==", "=", "!=", "=~", "<", ">", "-eq", "-ne",
			      "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", 0 };
	char *a, *b, *op;
	int k, r;

	if (c->i >= c->n) {
		c->bad = 1;
		return 0;
	}
	if (cx_is(c, c->i, "(") && cx_op(c, c->i)) {
		if (++c->depth > HIBR_DEPTH) {
			if (!c->bad)
				lg(HIBR_LERR, "[[: nested more than %d deep", HIBR_DEPTH);
			c->bad = 1;
			c->i = c->n;
			return 0;
		}
		c->i++;
		r = cx_or(c);
		c->depth--;
		if (!cx_is(c, c->i, ")")) {
			if (!c->bad)
				lg(HIBR_LERR, "[[: expected )");
			c->bad = 1;
			return 0;
		}
		c->i++;
		return r;
	}
	for (k = 0; bin[k]; k++)
		if (c->i + 2 < c->n && cx_is(c, c->i + 1, bin[k]))
			break;
	if (bin[k]) {
		a = cx_val(c, c->i);
		op = (char *)bin[k];
		c->i += 2;
		if (!strcmp(op, "==") || !strcmp(op, "=") || !strcmp(op, "!=")) {
			b = c->skip ? (char *)"" : xpat(c->s, c->it[c->i]);
			c->i++;
			r = gmatch(b, a);
			return op[0] == '!' ? !r : r;
		}
		b = cx_val(c, c->i);
		c->i++;
		if (c->skip)
			return 0;
		if (!strcmp(op, "=~")) {
			char *mv[4];
			mv[0] = "match";
			mv[1] = a;
			mv[2] = b;
			mv[3] = "M";
			r = b_match(c->s, 4, mv);
			if (r == 2)
				c->bad = 1;
			return r == HIBR_OK;
		}
		if (!strcmp(op, "<"))
			return strcmp(a, b) < 0;
		if (!strcmp(op, ">"))
			return strcmp(a, b) > 0;
		if (op[0] == '-' && (op[1] == 'n' || op[1] == 'o' || op[1] == 'e') &&
		    (op[2] == 't' || op[2] == 'f'))
			return cx_files(a, op, b);
		r = t_two(a, op, b);
		return r > 0;
	}
	if (c->i + 1 < c->n && !cx_op(c, c->i) && w_lit(c->it[c->i]) &&
	    w_lit(c->it[c->i])[0] == '-' && w_lit(c->it[c->i])[1] &&
	    !w_lit(c->it[c->i])[2] && !cx_op(c, c->i + 1)) {
		op = w_lit(c->it[c->i]);
		c->i++;
		a = cx_val(c, c->i);
		c->i++;
		if (c->skip)
			return 0;
		if (op[1] == 'v')
			return v_find(c->s, a) != 0;
		r = t_one(op, a);
		if (r < 0) {
			lg(HIBR_LERR, "[[: %s: unknown test", op);
			c->bad = 1;
			return 0;
		}
		return r;
	}
	a = cx_val(c, c->i);
	c->i++;
	return *a != 0;
}

/* Evaluate negation. */
int cx_not(struct cx *c)
{
	if (cx_is(c, c->i, "!") && !cx_op(c, c->i)) {
		int r;
		if (++c->depth > HIBR_DEPTH) {
			c->bad = 1;
			c->i = c->n;
			return 0;
		}
		c->i++;
		r = !cx_not(c);
		c->depth--;
		return r;
	}
	return cx_prim(c);
}

/* Evaluate a conjunction, skipping the right side once the answer is known. */
int cx_and(struct cx *c)
{
	int v = cx_not(c), r;

	while (cx_op(c, c->i) && !strcmp(cx_op(c, c->i), "&&")) {
		c->i++;
		if (!v)
			c->skip++;
		r = cx_not(c);
		if (!v)
			c->skip--;
		else
			v = r;
	}
	return v;
}

/* Evaluate a disjunction, skipping the right side once the answer is known. */
int cx_or(struct cx *c)
{
	int v = cx_and(c), r;

	while (cx_op(c, c->i) && !strcmp(cx_op(c, c->i), "||")) {
		c->i++;
		if (v)
			c->skip++;
		r = cx_and(c);
		if (v)
			c->skip--;
		else
			v = r;
	}
	return v;
}

/* Execute a [[ ]] conditional. */
int ex_cond(sh *s, node *n)
{
	amark m = ar_mark(s->xa);
	vec *items = vb_get(s);
	struct cx c;
	word *w;
	int v;

	for (w = n->w; w; w = w->nx)
		v_add(items, w);
	c.s = s;
	c.it = (word **)items->p;
	c.n = items->n;
	c.i = 0;
	c.bad = 0;
	c.skip = 0;
	c.depth = 0;
	v = c.n ? cx_or(&c) : 0;
	if (c.i < c.n && !c.bad) {
		lg(HIBR_LERR, "[[: unexpected '%s'", cx_op(&c, c.i) ? cx_op(&c, c.i) :
				(w_lit(c.it[c.i]) ? w_lit(c.it[c.i]) : "operand"));
		c.bad = 1;
	}
	vb_put(s, items);
	ar_rel(s->xa, m);
	return s->st = c.bad ? 2 : (v ? 0 : 1);
}

/* Offer a numbered menu and run the body for each choice. */
int ex_select(sh *s, node *n)
{
	amark m = ar_mark(s->xa);
	vec o = { 0, 0, 0 };
	word *w;
	size_t i;
	int st = 0, show = 1;
	str line;
	char c;
	ssize_t got;
	const char *ps3;
	long k;

	if (n->f)
		for (w = n->w; w; w = w->nx)
			xw(s, w, &o, 0);
	else
		for (i = 0; i < (size_t)s->ac; i++)
			v_add(&o, s->av[i]);
	for (;;) {
		if (show)
			for (i = 0; i < o.n; i++)
				fprintf(stderr, "%lu) %s\n", (unsigned long)i + 1,
					(char *)o.p[i]);
		show = 0;
		ps3 = hibr_get(s, "PS3");
		fputs(ps3 ? ps3 : "#? ", stderr);
		fflush(stderr);
		s_init(&line);
		while ((got = read(0, &c, 1)) == 1 && c != '\n')
			s_ch(&line, c);
		if (got != 1 && !line.n) {
			s_free(&line);
			fputc('\n', stderr);
			st = HIBR_FAIL;
			break;
		}
		hibr_set(s, "REPLY", line.p ? line.p : "", 0);
		if (!line.n) {
			show = 1;
			s_free(&line);
			continue;
		}
		k = atol(line.p);
		hibr_set(s, n->s, k >= 1 && (size_t)k <= o.n ? (char *)o.p[k - 1] : "", 0);
		s_free(&line);
		st = ex(s, n->r);
		if (s->brk) {
			s->brk--;
			break;
		}
		if (s->cont) {
			s->cont--;
			if (s->cont)
				break;
		}
		if (s->quit || s->stop || s->ret)
			break;
	}
	v_free(&o);
	ar_rel(s->xa, m);
	return s->st = st;
}

/* Execute any AST node. */
int ex(sh *s, node *n)
{
	vec sv = { 0, 0, 0 };
	int st, t;

	if (!n)
		return s->st;
	if (s->quit || s->stop || s->ret || s->brk || s->cont)
		return s->st;
	if (tr_pending())
		tr_run(s);
	t = s->tst;
	s->tst = 0;
	switch (n->k) {
	case N_CMD:
		return ex_chk(s, ex_cmd(s, n), t);
	case N_SEQ:
		ex(s, n->l);
		s->tst = t;
		return ex(s, n->r);
	case N_AND:
		s->tst = 1;
		st = ex(s, n->l);
		if (st)
			return st;
		s->tst = t;
		return ex(s, n->r);
	case N_OR:
		s->tst = 1;
		st = ex(s, n->l);
		if (!st)
			return st;
		s->tst = t;
		return ex(s, n->r);
	case N_NOT:
		s->tst = 1;
		st = ex(s, n->l);
		return ex_chk(s, s->st = st ? 0 : 1, t);
	case N_PIPE:
		st = ex_pipe(s, n);
		ex_chk(s, s->pfs, t);
		return st;
	case N_BG:
		return ex_bg(s, n);
	case N_SUB:
		return ex_chk(s, ex_sub(s, n), t);
	case N_GRP:
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		st = ex(s, n->l);
		rd_undo(&sv);
		return st;
	case N_IF:
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		s->tst = 1;
		st = ex(s, n->l);
		s->tst = t;
		st = st == 0 ? ex(s, n->r) : (n->x ? ex(s, n->x) : 0);
		rd_undo(&sv);
		return s->st = st;
	case N_WHILE:
	case N_UNTIL:
	case N_FOR:
	case N_CFOR:
	case N_SELECT:
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		st = n->k == N_FOR ? ex_for(s, n) :
		     n->k == N_CFOR ? ex_cfor(s, n) :
		     n->k == N_SELECT ? ex_select(s, n) : ex_loop(s, n);
		rd_undo(&sv);
		return st;
	case N_ARITH: {
		long v;
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		v = ax_text(s, n->w->p->t);
		rd_undo(&sv);
		if (s->xerr) {
			s->xerr = 0;
			return s->st = HIBR_FAIL;
		}
		return s->st = v ? 0 : 1;
	}
	case N_COND:
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		st = ex_cond(s, n);
		rd_undo(&sv);
		return ex_chk(s, st, t);
	case N_CASE:
		if (rd_do(s, n->rd, &sv) != HIBR_OK) {
			rd_undo(&sv);
			return s->st = HIBR_FAIL;
		}
		st = ex_case(s, n);
		rd_undo(&sv);
		return st;
	case N_FUNC:
		fn_add(s, n);
		return s->st = 0;
	}
	lg(HIBR_LERR, "internal: unknown node kind %d", n->k);
	return s->st = HIBR_FAIL;
}
