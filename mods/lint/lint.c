/* lint -- name the mistakes a script makes, without running it. */

#include "hibr.h"
#include "../lint.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern unsigned lg_ln;
const hibr_bi *bi_find(const char *nm);

typedef struct li {
	sh *s;
	vec fns, asg, assoc, arr, num, txt, out;
	int seterr, found;
} li;

typedef struct lif {
	unsigned ln;
	size_t seq;
	const char *rule;
	char *m;
} lif;

/* The text of a word that is one literal part, or null. */
const char *li_lit(word *w)
{
	if (!w || !w->p || w->p->nx || w->p->k != P_TXT)
		return 0;
	return w->p->t;
}

/* The name of the command a simple command runs, when it is written plainly. */
const char *li_cmd(node *n)
{
	return n && n->k == N_CMD && n->w ? li_lit(n->w) : 0;
}

/* Whether a vector of names holds one. */
int li_has(vec *v, const char *nm)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		if (!strcmp((char *)v->p[i], nm))
			return 1;
	return 0;
}

/* Whether a name is one of a null-ended list. */
int li_in(const char *nm, const char **l)
{
	for (; *l; l++)
		if (!strcmp(nm, *l))
			return 1;
	return 0;
}

/* The name an assignment word (name=, name[...]=, name+=) assigns, fresh, or null. */
char *li_asgname(word *w)
{
	const char *t;
	size_t n = 0;
	char *r;

	if (!w || !w->p || w->p->k != P_TXT || w->p->q)
		return 0;
	t = w->p->t;
	while ((t[n] >= 'a' && t[n] <= 'z') || (t[n] >= 'A' && t[n] <= 'Z') ||
	       t[n] == '_' || (n && t[n] >= '0' && t[n] <= '9'))
		n++;
	if (!n || (t[n] != '=' && t[n] != '[' &&
		   !(t[n] == '+' && t[n + 1] == '=')))
		return 0;
	r = xm(n + 1);
	memcpy(r, t, n);
	r[n] = 0;
	return r;
}

/* Keep one finding at a node's line, to be reported in line order. */
void li_say(li *c, node *n, const char *rule, const char *f, ...)
{
	va_list ap, ap2;
	char *m;
	int k;
	lif *r;

	va_start(ap, f);
	va_copy(ap2, ap);
	k = vsnprintf(0, 0, f, ap2);
	va_end(ap2);
	m = xm((size_t)(k < 0 ? 0 : k) + 1);
	vsnprintf(m, (size_t)(k < 0 ? 0 : k) + 1, f, ap);
	va_end(ap);
	r = xm(sizeof *r);
	memset(r, 0, sizeof *r);
	r->ln = n ? n->ln : 0;
	r->seq = c->out.n;
	r->rule = rule;
	r->m = m;
	v_add(&c->out, r);
	c->found++;
}

/* Order findings by line, and by when they were found within one. */
int li_cmp(const void *a, const void *b)
{
	const lif *x = *(lif *const *)a, *y = *(lif *const *)b;

	if (x->ln != y->ln)
		return x->ln < y->ln ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* Report the findings the way the shell reports anything: a line of JSON in agent mode, file:line: rule: message otherwise. */
void li_emit(li *c)
{
	lif *r;
	size_t i;

	if (c->out.n)
		qsort(c->out.p, c->out.n, sizeof *c->out.p, li_cmp);
	for (i = 0; i < c->out.n; i++) {
		r = c->out.p[i];
		lg_ln = r->ln;
		if (c->s->sopt & O_AGENT)
			lg(HIBR_LWRN, "%s: %s", r->rule, r->m);
		else
			lg(HIBR_LWRN, "%s:%u: %s: %s", c->s->src ? c->s->src : "command line",
			   r->ln, r->rule, r->m);
		free(r->m);
		free(r);
	}
	lg_ln = 0;
	v_free(&c->out);
}

/* Whether a program of this name is on the PATH. */
int li_onpath(const char *nm)
{
	const char *p = getenv("PATH"), *e;
	str f;
	int ok = 0;

	if (!p || !*nm || strchr(nm, '/'))
		return 0;
	s_init(&f);
	while (*p && !ok) {
		e = strchr(p, ':');
		f.n = 0;
		s_add(&f, p, e ? (size_t)(e - p) : strlen(p));
		s_ch(&f, '/');
		s_cat(&f, nm);
		ok = access(f.p, X_OK) == 0;
		p = e ? e + 1 : p + strlen(p);
	}
	s_free(&f);
	return ok;
}

/* Whether a text is a non-empty run of digits. */
int li_digits(const char *t)
{
	if (!*t)
		return 0;
	while (*t >= '0' && *t <= '9')
		t++;
	return !*t;
}

/* Whether an assignment word gives a number: name=12 or name=$((...)). */
int li_numval(word *w, size_t k)
{
	part *p = w->p;
	const char *v = p->t + k;

	if (*v == '+')
		v++;
	if (*v++ != '=')
		return 0;
	if (!p->nx)
		return li_digits(v);
	return !*v && p->nx->k == P_ARI && !p->nx->nx;
}

/* First pass: functions defined, names assigned and how, arrays made with and without -A, and set -e. */
void li_scan(li *c, node *n)
{
	static const char *decl[] = { "local", "declare", "typeset", "read",
				      "export", "readonly", 0 };
	word *w;
	const char *cmd, *a;
	char *nm;
	node *pm;
	int assoc;

	if (!n)
		return;
	if (n->k == N_FUNC && n->s) {
		v_add(&c->fns, n->s);
		for (pm = n->x; pm; pm = pm->x)
			if (pm->s) {
				v_add(&c->asg, xs(pm->s));
				v_add(&c->txt, xs(pm->s));
			}
	}
	if ((n->k == N_FOR || n->k == N_SELECT) && n->s) {
		v_add(&c->asg, xs(n->s));
		for (w = n->w, assoc = !!w; w; w = w->nx)
			if (!li_lit(w) || !li_digits(li_lit(w)))
				assoc = 0;
		v_add(assoc ? &c->num : &c->txt, xs(n->s));
	}
	if (n->k == N_CMD) {
		for (w = n->aw; w; w = w->nx)
			if ((nm = li_asgname(w))) {
				if (w->p->t[strlen(nm)] == '[')
					v_add(&c->arr, xs(nm));
				else
					v_add(li_numval(w, strlen(nm)) ? &c->num :
					      &c->txt, xs(nm));
				v_add(&c->asg, nm);
			}
		for (pm = n->x; pm; pm = pm->x)
			if (pm->k == N_CLAUSE && pm->s)
				v_add(&c->arr, xs(pm->s));
		if (n->s)
			v_add(&c->asg, xs(n->s));
		cmd = li_cmd(n);
		assoc = 0;
		if (cmd && !strcmp(cmd, "set"))
			for (w = n->w->nx; w; w = w->nx)
				if ((a = li_lit(w)) && ((a[0] == '-' &&
					strchr(a, 'e')) || !strcmp(a, "errexit")))
					c->seterr = 1;
		if (cmd && li_in(cmd, decl))
			for (w = n->w->nx; w; w = w->nx) {
				a = li_lit(w);
				if (a && a[0] == '-') {
					if (strchr(a, 'A'))
						assoc = 1;
					continue;
				}
				nm = li_asgname(w);
				if (!nm && a)
					nm = xs(a);
				if (!nm)
					continue;
				if (assoc)
					v_add(&c->assoc, xs(nm));
				else
					v_add(&c->arr, xs(nm));
				v_add(&c->txt, xs(nm));
				v_add(&c->asg, nm);
			}
	}
	li_scan(c, n->l);
	li_scan(c, n->r);
	if (n->k != N_FUNC)
		li_scan(c, n->x);
}

/* Whether a word holds an unquoted expansion, naming it in *nm. */
int li_unq(word *w, const char **nm)
{
	part *p;

	for (p = w ? w->p : 0; p; p = p->nx)
		if ((p->k == P_VAR || p->k == P_CMD) && !p->q) {
			*nm = p->k == P_VAR ? p->t : "(...)";
			return 1;
		}
	return 0;
}

/* unquoted-path: an unquoted expansion handed to rm, mv and the like, where a blank or a * makes more paths. */
void li_path(li *c, node *n, const char *cmd)
{
	static const char *tools[] = { "rm", "mv", "cp", "rmdir", "ln", "chmod",
				       "chown", "chgrp", "touch", "mkdir", 0 };
	word *w;
	const char *nm;

	if (!li_in(cmd, tools))
		return;
	for (w = n->w->nx; w; w = w->nx)
		if (li_unq(w, &nm)) {
			li_say(c, n, "unquoted-path", "%s is handed $%s unquoted: "
			       "a blank or a * in its value makes more paths than "
			       "meant; write \"$%s\"", cmd, nm, nm);
			return;
		}
}

/* test-unquoted: [ or test given a bare $x that can be empty or blank, leaving the test a word short. */
void li_test(li *c, node *n, const char *cmd)
{
	word *w;

	if (strcmp(cmd, "[") && strcmp(cmd, "test"))
		return;
	for (w = n->w->nx; w; w = w->nx)
		if (w->p && !w->p->nx && w->p->k == P_VAR && !w->p->q &&
		    !w->p->arr && w->p->op != V_LEN &&
		    !strchr("#?$!-", w->p->t[0]) &&
		    !(li_has(&c->num, w->p->t) && !li_has(&c->txt, w->p->t))) {
			li_say(c, n, "test-unquoted", "$%s is unquoted in %s: if it "
			       "is empty the test loses a word and errors or is "
			       "always true; write \"$%s\"", w->p->t, cmd,
			       w->p->t);
			return;
		}
}

/* Whether a module of this name exists, and so may make it a builtin: ls is a program and the ls module's builtin. */
int li_mod(li *c, const char *nm)
{
	const char *p = getenv("HIBR_MODPATH"), *e;
	str f;
	int ok = 0;

	(void)c;
	s_init(&f);
	while (p && *p && !ok) {
		e = strchr(p, ':');
		f.n = 0;
		s_add(&f, p, e ? (size_t)(e - p) : strlen(p));
		s_ch(&f, '/');
		s_cat(&f, nm);
		s_cat(&f, ".so");
		ok = access(f.p, R_OK) == 0;
		p = e ? e + 1 : 0;
	}
#ifdef HIBR_MODDIR
	if (!ok) {
		f.n = 0;
		s_cat(&f, HIBR_MODDIR);
		s_ch(&f, '/');
		s_cat(&f, nm);
		s_cat(&f, ".so");
		ok = access(f.p, R_OK) == 0;
	}
#endif
	s_free(&f);
	return ok;
}

/* bind-program: x := program, where a program prints instead and x stays empty. */
void li_bind(li *c, node *n, const char *cmd)
{
	if (n->f != 2 || bi_find(cmd) || li_has(&c->fns, cmd) ||
	    li_mod(c, cmd) || !li_onpath(cmd))
		return;
	li_say(c, n, "bind-program", ":= binds what a builtin or a function "
	       "returns, and %s is a program: it prints and the variable stays "
	       "empty; write x=$(%s ...)", cmd, cmd);
}

/* unset-quoted-key: unset 'm[$k]', bash's idiom, removes a key literally named $k here. */
void li_unset(li *c, node *n, const char *cmd)
{
	word *w;
	const char *a;

	if (strcmp(cmd, "unset"))
		return;
	for (w = n->w->nx; w; w = w->nx)
		if (w->p && !w->p->nx && w->p->k == P_TXT && w->p->q &&
		    (a = strchr(w->p->t, '[')) && strchr(a, '$')) {
			li_say(c, n, "unset-quoted-key", "unset '%s' removes the key "
			       "written there, $ and all, since a quoted subscript "
			       "is a literal key; write it unquoted with the "
			       "variable quoted inside, m[\"$k\"]", w->p->t);
			return;
		}
}

/* Whether a part list reads the variable nm, in its value or a subscript. */
int li_reads(part *p, const char *nm)
{
	word *w;

	for (; p; p = p->nx) {
		if (p->k == P_VAR && p->t && !strcmp(p->t, nm))
			return 1;
		for (w = p->idx; w; w = w->nx)
			if (li_reads(w->p, nm))
				return 1;
		for (w = p->arg; w; w = w->nx)
			if (li_reads(w->p, nm))
				return 1;
	}
	return 0;
}

/* local-self-ref: local a=$1 b=${m[$a]}, where b reads a before local has assigned it. */
void li_self(li *c, node *n, const char *cmd)
{
	static const char *decl[] = { "local", "declare", "typeset", "export",
				      "readonly", 0 };
	word *w, *v;
	char *nm;

	if (!li_in(cmd, decl))
		return;
	for (w = n->w->nx; w; w = w->nx) {
		if (!(nm = li_asgname(w)))
			continue;
		for (v = w->nx; v; v = v->nx)
			if (li_reads(v->p, nm)) {
				li_say(c, n, "local-self-ref", "$%s is read in the same "
				       "%s that assigns it, before it is assigned: "
				       "put %s=... on its own line first", nm, cmd,
				       nm);
				free(nm);
				return;
			}
		free(nm);
	}
}

/* bare-key: ${m[row]}, row never assigned, on an array this file makes without -A; one made elsewhere may be -A. */
void li_barekey(li *c, node *n, part *p)
{
	word *w;
	const char *k;

	for (; p; p = p->nx) {
		if (p->k == P_VAR && p->idx && !li_has(&c->assoc, p->t) &&
		    li_has(&c->arr, p->t))
			for (w = p->idx; w; w = w->nx) {
				k = li_lit(w);
				if (!k || w->p->q || !((*k >= 'a' && *k <= 'z') ||
				    (*k >= 'A' && *k <= 'Z') || *k == '_') ||
				    strpbrk(k, "+-*/%()<>=!&|^ ") ||
				    li_has(&c->asg, k) || !strcmp(k, "@") ||
				    !strcmp(k, "*"))
					continue;
				li_say(c, n, "bare-key", "${%s[%s]}: an unquoted key "
				       "is looked up as a variable first; if %s is "
				       "the key itself, write [\"%s\"], or declare "
				       "%s -A", p->t, k, k, k, p->t);
				return;
			}
		for (w = p->idx; w; w = w->nx)
			li_barekey(c, n, w->p);
		for (w = p->arg; w; w = w->nx)
			li_barekey(c, n, w->p);
	}
}

/* The first simple command a node runs. */
node *li_first(node *n)
{
	while (n && n->k != N_CMD)
		n = n->l;
	return n;
}

/* The last simple command a sequence runs. */
node *li_last(node *n)
{
	while (n && n->k == N_SEQ)
		n = n->r;
	return n && n->k == N_CMD ? n : 0;
}

/* Whether a node's first command reads $?. */
int li_asks(node *n)
{
	word *w;

	n = li_first(n);
	for (w = n ? n->w : 0; w; w = w->nx)
		if (li_reads(w->p, "?"))
			return 1;
	return 0;
}

/* Whether a declaration assigns a command substitution. */
int li_declsub(node *n)
{
	static const char *decl[] = { "local", "declare", "typeset", "export",
				      "readonly", 0 };
	const char *cmd = li_cmd(n);
	word *w;
	part *p;

	if (!cmd || !li_in(cmd, decl))
		return 0;
	for (w = n->w->nx; w; w = w->nx)
		for (p = w->p; p; p = p->nx)
			if (p->k == P_CMD)
				return 1;
	return 0;
}

/* Walk the tree running every rule; cond is set inside a condition, where a failing cd is already looked at. */
void li_walk(li *c, node *n, int cond)
{
	const char *cmd;
	word *w;

	if (!n)
		return;
	switch (n->k) {
	case N_CMD:
		cmd = li_cmd(n);
		if (cmd) {
			li_path(c, n, cmd);
			li_test(c, n, cmd);
			li_bind(c, n, cmd);
			li_unset(c, n, cmd);
			li_self(c, n, cmd);
			if (!strcmp(cmd, "cd") && !cond && !c->seterr &&
			    !(n->w->nx && li_lit(n->w->nx) &&
			      !strcmp(li_lit(n->w->nx), "/")))
				li_say(c, n, "cd-unchecked", "cd can fail, and then "
				       "everything after it runs in the wrong "
				       "directory; write cd ... || exit");
		}
		for (w = n->w; w; w = w->nx)
			li_barekey(c, n, w->p);
		for (w = n->aw; w; w = w->nx)
			li_barekey(c, n, w->p);
		li_walk(c, n->x, cond);
		return;
	case N_SEQ: {
		node *a = li_last(n->l), *b = li_first(n->r);
		if (a && li_declsub(a) && li_asks(n->r))
			li_say(c, a, "local-masks-status", "$? after %s x=$(...) "
			       "is %s's own status, always 0, not the command's; "
			       "declare it first, then assign it on a line of its "
			       "own", li_cmd(a), li_cmd(a));
		if (a && li_cmd(a) && !strcmp(li_cmd(a), "ret") && b &&
		    li_cmd(b) && !strcmp(li_cmd(b), "return") && b->w->nx &&
		    !(li_lit(b->w->nx) && !strcmp(li_lit(b->w->nx), "0")))
			li_say(c, b, "dead-return", "ret already ends the "
			       "function with status 0, so this failing return "
			       "never runs; for a failure, return without ret");
		li_walk(c, n->l, cond);
		li_walk(c, n->r, cond);
		return;
	}
	case N_AND:
	case N_OR:
		li_walk(c, n->l, 1);
		li_walk(c, n->r, cond);
		return;
	case N_IF:
	case N_WHILE:
	case N_UNTIL:
		li_walk(c, n->l, 1);
		li_walk(c, n->r, cond);
		li_walk(c, n->x, cond);
		return;
	case N_FOR:
		for (w = n->w; w; w = w->nx) {
			part *p;
			const char *t;
			for (p = w->p; p; p = p->nx) {
				if (p->k != P_CMD || p->q || !p->t)
					continue;
				t = p->t;
				while (*t == '(' || *t == ' ' || *t == '\t')
					t++;
				if (t[0] == 'l' && t[1] == 's' &&
				    (!t[2] || t[2] == ' ' || t[2] == ')'))
					li_say(c, n, "for-ls", "for over $(ls) splits "
					       "file names at blanks and globs them; "
					       "loop over a glob: for f in *");
			}
		}
		for (w = n->w; w; w = w->nx)
			li_barekey(c, n, w->p);
		li_walk(c, n->r, cond);
		return;
	case N_CASE:
		for (w = n->w; w; w = w->nx)
			li_barekey(c, n, w->p);
		li_walk(c, n->l, cond);
		li_walk(c, n->r, cond);
		li_walk(c, n->x, cond);
		return;
	case N_FUNC:
		li_walk(c, n->r, 0);
		return;
	default:
		li_walk(c, n->l, cond);
		li_walk(c, n->r, cond);
		li_walk(c, n->x, cond);
		return;
	}
}

/* Explain a parsed script: every finding reported, and how many there were. */
int li_explain(sh *s, node *n)
{
	li c;
	size_t i;

	memset(&c, 0, sizeof c);
	c.s = s;
	li_scan(&c, n);
	li_walk(&c, n, 0);
	li_emit(&c);
	for (i = 0; i < c.asg.n; i++)
		free(c.asg.p[i]);
	for (i = 0; i < c.num.n; i++)
		free(c.num.p[i]);
	for (i = 0; i < c.txt.n; i++)
		free(c.txt.p[i]);
	v_free(&c.num);
	v_free(&c.txt);
	for (i = 0; i < c.assoc.n; i++)
		free(c.assoc.p[i]);
	for (i = 0; i < c.arr.n; i++)
		free(c.arr.p[i]);
	v_free(&c.arr);
	v_free(&c.asg);
	v_free(&c.assoc);
	v_free(&c.fns);
	lg(HIBR_LDBG, "lint: %d finding%s", c.found, c.found == 1 ? "" : "s");
	return c.found;
}

static const li_api li_tab = { li_explain };

const hibr_bi li_bi[] = { HIBR_BI_END };

/* Offer the linter as "lint", so --explain can find it unloaded. */
int li_init(sh *s)
{
	return hibr_provide(s, "lint", LI_API_VER, (void *)&li_tab);
}

/* Withdraw the linter before the module is unloaded. */
void li_fini(sh *s)
{
	hibr_unprovide(s, "lint");
}

HIBR_MODULE_P("lint", "1.0", "name the mistakes a script makes, without running it",
	      li_bi, li_init, li_fini, "lint");
