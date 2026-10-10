#include "pri.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __APPLE__
#include <pthread.h>
#endif
#include <unistd.h>

struct ospec {
	vec sw;
	char *nm, *ty, *dflt, *help;
	int rep, req, isbool, seen;
};

char *pt_start;
char *pt_end;
int pt_taken;

/* Remember the argv/environ region and move environ out of it. */
void pt_init(int ac, char **av)
{
	extern char **environ;
	char **e;
	char *last;

	if (!ac || !av[0])
		return;
	pt_start = av[0];
	last = av[ac - 1] + strlen(av[ac - 1]);
	for (e = environ; e && *e; e++)
		if (*e == last + 1 || *e > last)
			last = *e + strlen(*e);
	pt_end = last;
	lg(HIBR_LTRC, "title region is %lu bytes",
	   (unsigned long)(pt_end - pt_start));
}

/* Move each environment string off the argv region a title will overwrite. */
void pt_claim(void)
{
	extern char **environ;
	char **e;

	if (pt_taken)
		return;
	pt_taken = 1;
	for (e = environ; e && *e; e++)
		*e = xs(*e);
	lg(HIBR_LDBG, "environment strings copied off the argv region");
}

/* Rename the running process, in the two places it shows, which take two
   different strings.
 *
 * **The argv region** is what `ps` and `ps -f` read, and it is as long as
 * the command line and environment it overwrites -- so the readable
 * sentence goes there: `desktop [Files 3]`.
 *
 * **The kernel's own name** -- /proc/pid/comm, which is what `top`, htop's
 * default column, `pgrep` and `killall` read -- is `TASK_COMM_LEN`, 16
 * bytes, 15 usable. Passing it the long name is what made every one of
 * these unreadable where a person actually looks (Gitea #168):
 *
 *     hibr [hold: bli      <- hibr [hold: blit-direct-test-2]
 *     desktop [deskto      <- desktop [desktop]
 *     desktop [Termin      <- desktop [Terminal 104]
 *
 * So the caller gives both: a short name it has composed to fit, role
 * first, and the long one. The caller knows its role and which part of the
 * instance identifies it; this knows the byte limit. A short name with
 * **no brackets**, because `ps` wraps a defunct process's own name in
 * brackets and `[hibr [desktop]] <defunct>` is what that produced.
 *
 * short may be null, which takes the long name truncated -- what every
 * caller got before, kept so that `title` with one argument still works.
 */
void pt_rename(const char *shortnm, const char *name)
{
	size_t room, n;
	str c;

	if (!pt_start || pt_end <= pt_start)
		return;
	pt_claim();
	n = strlen(name);
	room = (size_t)(pt_end - pt_start);
	memset(pt_start, 0, room);
	memcpy(pt_start, name, n < room - 1 ? n : room - 1);
	if (!shortnm || !*shortnm)
		shortnm = name;
	n = strlen(shortnm);
	if (n > HIBR_COMM - 1)
		n = HIBR_COMM - 1;
	s_init(&c);
	s_add(&c, shortnm, n);
	s_grow(&c, 1);
	c.p[c.n] = 0;
#ifdef __linux__
	prctl(PR_SET_NAME, (unsigned long)c.p, 0, 0, 0);
#elif defined(__APPLE__)
	pthread_setname_np(c.p);
#endif
	lg(HIBR_LDBG, "process title now %s, short name %s", name, c.p);
	s_free(&c);
}

/* Rename the running process as seen by ps and top.
 *
 *     title [-s SHORT] NAME...
 *
 * NAME... is the sentence `ps` shows. -s is the name the *kernel* keeps,
 * which is 15 usable bytes and what `top` and `pgrep` read -- role first,
 * no brackets, composed by the caller to fit. Without -s the long name is
 * truncated to fit there, which is what every caller got before 0.99.131
 * and is why a desktop's parts were unreadable in top (Gitea #168). */
int b_title(sh *s, int ac, char **av)
{
	str t;
	size_t i;
	const char *shortnm = 0;
	int k = 1;

	(void)s;
	if (ac > 2 && !strcmp(av[1], "-s")) {
		shortnm = av[2];
		k = 3;
	}
	if (ac <= k) {
		lg(HIBR_LERR, "usage: title [-s short] name...");
		return 2;
	}
	if (!pt_start || pt_end <= pt_start) {
		lg(HIBR_LERR, "title: argv region unavailable");
		return HIBR_FAIL;
	}
	s_init(&t);
	for (i = (size_t)k; i < (size_t)ac; i++) {
		if (t.n)
			s_ch(&t, ' ');
		s_cat(&t, av[i]);
	}
	pt_rename(shortnm, t.p);
	s_free(&t);
	return HIBR_OK;
}

/* The same rename, for a module rather than a script. hibr_title keeps its
   one-argument shape so nothing already built against it has to change;
   hibr_title2 is the one that says both names. */
void hibr_title(const char *name)
{
	pt_rename(0, name);
}

void hibr_title2(const char *shortnm, const char *name)
{
	pt_rename(shortnm, name);
}

/* Wear the title a parent asked for, if it asked. The desktop gives each
   terminal window's shell one, so four shells in four windows can be told
   apart in ps rather than all reading `hibr` (Gitea #111).

   Two things it must do and one it must not. The value is copied out first,
   because a rename overwrites the whole argv and environment region the
   string itself lives in; and the variable is removed as it is used, or
   every program that shell goes on to start would inherit the same name.
   What it must not do is run before the script name and the positional
   parameters have been copied out of that same region, which is why its
   callers are where they are and not beside v_env. */
void sh_proctitle(sh *s)
{
	const char *v = hibr_get(s, "HIBR_PROCTITLE");
	const char *n = hibr_get(s, "HIBR_PROCNAME");
	char *t, *sn = 0;

	if (!v || !*v)
		return;
	/* Both copied out before either is used, for the reason above: the
	   rename overwrites the region these strings live in. */
	t = xs(v);
	if (n && *n)
		sn = xs(n);
	v_del(s, "HIBR_PROCTITLE");
	v_del(s, "HIBR_PROCNAME");
	pt_rename(sn, t);
	free(t);
	free(sn);
}

/* Release one option specification. */
void op_free(struct ospec *o)
{
	size_t i;

	for (i = 0; i < o->sw.n; i++)
		free(o->sw.p[i]);
	v_free(&o->sw);
	free(o->nm);
	free(o->ty);
	free(o->dflt);
	free(o->help);
	free(o);
}

/* Release the option specifications. */
void op_clear(sh *s)
{
	while (s->opts.n)
		op_free((struct ospec *)s->opts.p[--s->opts.n]);
	free(s->odesc);
	s->odesc = 0;
}

/* Declare one option: switches, a name, an optional type, and help text. */
int b_opt(sh *s, int ac, char **av)
{
	struct ospec *o;
	int i = 1;
	str h;
	char *eq;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: opt -s --long name [type[!+][=default]] [help...]");
		return 2;
	}
	if (!strcmp(av[1], ".")) {
		free(s->odesc);
		s->odesc = ac > 2 ? xs(av[2]) : 0;
		return HIBR_OK;
	}
	if (!strcmp(av[1], "-clear")) {
		op_clear(s);
		return HIBR_OK;
	}
	o = xm(sizeof *o);
	memset(o, 0, sizeof *o);
	for (; i < ac && av[i][0] == '-' && av[i][1]; i++)
		v_add(&o->sw, xs(av[i]));
	if (!o->sw.n || i >= ac || !isname(av[i])) {
		lg(HIBR_LERR, "opt: expected switches then a variable name");
		v_free(&o->sw);
		free(o);
		return 2;
	}
	o->nm = xs(av[i++]);
	o->isbool = 1;
	if (i < ac && (!strncmp(av[i], "int", 3) || !strncmp(av[i], "str", 3) ||
		       !strncmp(av[i], "num", 3) || !strncmp(av[i], "path", 4) ||
		       !strncmp(av[i], "bool", 4))) {
		o->ty = xs(av[i++]);
		eq = strchr(o->ty, '=');
		if (eq) {
			*eq = 0;
			o->dflt = xs(eq + 1);
		}
		while (o->ty[0]) {
			size_t n = strlen(o->ty);
			if (o->ty[n - 1] == '!') {
				o->req = 1;
				o->ty[n - 1] = 0;
			} else if (o->ty[n - 1] == '+') {
				o->rep = 1;
				o->ty[n - 1] = 0;
			} else {
				break;
			}
		}
		o->isbool = !strcmp(o->ty, "bool");
	}
	s_init(&h);
	for (; i < ac; i++) {
		if (h.n)
			s_ch(&h, ' ');
		s_cat(&h, av[i]);
	}
	o->help = h.p ? h.p : xs("");
	for (i = 0; i < (int)s->opts.n; i++)
		if (!strcmp(((struct ospec *)s->opts.p[i])->nm, o->nm)) {
			lg(HIBR_LTRC, "opt %s declared again, replacing it", o->nm);
			op_free((struct ospec *)s->opts.p[i]);
			s->opts.p[i] = o;
			return HIBR_OK;
		}
	v_add(&s->opts, o);
	lg(HIBR_LTRC, "opt %s declared", o->nm);
	return HIBR_OK;
}

/* Print the generated usage text. */
void op_usage(sh *s, FILE *f)
{
	struct ospec *o;
	size_t i, j, w = 0;
	str col;

	{
		const char *nm = s->arg0 ? s->arg0 : "hibr";
		const char *sl = strrchr(nm, '/');
		fprintf(f, "usage: %s [options] [args...]\n", sl ? sl + 1 : nm);
	}
	if (s->odesc)
		fprintf(f, "  %s\n", s->odesc);
	fprintf(f, "\n");
	for (i = 0; i < s->opts.n; i++) {
		o = (struct ospec *)s->opts.p[i];
		s_init(&col);
		for (j = 0; j < o->sw.n; j++) {
			if (j)
				s_cat(&col, ", ");
			s_cat(&col, (char *)o->sw.p[j]);
		}
		if (!o->isbool) {
			s_ch(&col, ' ');
			for (j = 0; o->nm[j]; j++)
				s_ch(&col, toupper((unsigned char)o->nm[j]));
		}
		if (col.n > w)
			w = col.n;
		s_free(&col);
	}
	for (i = 0; i < s->opts.n; i++) {
		o = (struct ospec *)s->opts.p[i];
		s_init(&col);
		for (j = 0; j < o->sw.n; j++) {
			if (j)
				s_cat(&col, ", ");
			s_cat(&col, (char *)o->sw.p[j]);
		}
		if (!o->isbool) {
			s_ch(&col, ' ');
			for (j = 0; o->nm[j]; j++)
				s_ch(&col, toupper((unsigned char)o->nm[j]));
		}
		fprintf(f, "  %-*s  %s", (int)w, col.p, o->help);
		if (!o->isbool) {
			fprintf(f, " (%s", o->ty);
			if (o->dflt)
				fprintf(f, ", default %s", o->dflt);
			if (o->req)
				fprintf(f, ", required");
			if (o->rep)
				fprintf(f, ", repeatable");
			fprintf(f, ")");
		}
		fprintf(f, "\n");
		s_free(&col);
	}
	fprintf(f, "  %-*s  %s\n", (int)w, "-h, --help", "show this help");
}

/* Find the option carrying a switch. */
struct ospec *op_find(sh *s, const char *sw)
{
	struct ospec *o;
	size_t i, j;

	for (i = 0; i < s->opts.n; i++) {
		o = (struct ospec *)s->opts.p[i];
		for (j = 0; j < o->sw.n; j++)
			if (!strcmp((char *)o->sw.p[j], sw))
				return o;
	}
	return 0;
}

/* Store a parsed value, appending when the option repeats. */
int op_put(sh *s, struct ospec *o, const char *v)
{
	vec *cur;
	size_t i;

	if (!o->isbool && !ty_ok(o->ty, v)) {
		lg(HIBR_LERR, "%s: expected %s, got '%s'", o->nm, o->ty, v);
		return HIBR_FAIL;
	}
	if (o->rep) {
		cur = vb_get(s);
		if (o->seen)
			v_list(s, o->nm, 0, 0, cur, 0);
		for (i = 0; i < cur->n; i++)
			cur->p[i] = xs((char *)cur->p[i]);
		v_add(cur, xs(v));
		v_arr(s, o->nm, cur);
		for (i = 0; i < cur->n; i++)
			free(cur->p[i]);
		vb_put(s, cur);
	} else {
		hibr_set(s, o->nm, v, 0);
	}
	o->seen = 1;
	return HIBR_OK;
}

/* Parse arguments against the declared options, then forget them. */
int b_args(sh *s, int ac, char **av)
{
	vec *pos = vb_get(s);
	struct ospec *o;
	size_t i, k;
	int a = 1, rc = HIBR_OK;
	str key;

	for (i = 0; i < s->opts.n; i++)
		((struct ospec *)s->opts.p[i])->seen = 0;
	for (; a < ac; a++) {
		const char *t = av[a];
		if (!strcmp(t, "--")) {
			for (a++; a < ac; a++)
				v_add(pos, av[a]);
			break;
		}
		if (!strcmp(t, "-h") || !strcmp(t, "--help")) {
			if (!op_find(s, t)) {
				op_usage(s, stdout);
				vb_put(s, pos);
				op_clear(s);
				hibr_set(s, "ARGS_HELP", "1", 0);
				if (!s->it && !s->dep && !s->intry)
					s->quit = 1;
				return HIBR_OK;
			}
		}
		if (t[0] == '-' && t[1] == '-' && t[2]) {
			const char *eq = strchr(t, '=');
			const char *val = eq ? eq + 1 : 0;
			s_init(&key);
			s_add(&key, t, eq ? (size_t)(eq - t) : strlen(t));
			o = op_find(s, key.p);
			if (!o && !strncmp(key.p, "--no-", 5)) {
				str pl;
				s_init(&pl);
				s_cat(&pl, "--");
				s_cat(&pl, key.p + 5);
				o = op_find(s, pl.p);
				s_free(&pl);
				if (o && o->isbool) {
					hibr_set(s, o->nm, "0", 0);
					o->seen = 1;
					s_free(&key);
					continue;
				}
				o = 0;
			}
			if (!o) {
				lg(HIBR_LERR, "unknown option %s", key.p);
				s_free(&key);
				rc = 2;
				goto done;
			}
			s_free(&key);
			if (o->isbool) {
				hibr_set(s, o->nm, val && !strcmp(val, "0") ? "0" : "1", 0);
				o->seen = 1;
				continue;
			}
			if (!val) {
				if (a + 1 >= ac) {
					lg(HIBR_LERR, "%s needs a value", t);
					rc = 2;
					goto done;
				}
				val = av[++a];
			}
			if (op_put(s, o, val) != HIBR_OK) {
				rc = 2;
				goto done;
			}
			continue;
		}
		if (t[0] == '-' && t[1] && t[1] != '-') {
			for (k = 1; t[k]; k++) {
				char sw[3];
				sw[0] = '-';
				sw[1] = t[k];
				sw[2] = 0;
				o = op_find(s, sw);
				if (!o) {
					lg(HIBR_LERR, "unknown option -%c", t[k]);
					rc = 2;
					goto done;
				}
				if (o->isbool) {
					hibr_set(s, o->nm, "1", 0);
					o->seen = 1;
					continue;
				}
				if (t[k + 1]) {
					if (op_put(s, o, t + k + 1) != HIBR_OK) {
						rc = 2;
						goto done;
					}
				} else {
					if (a + 1 >= ac) {
						lg(HIBR_LERR, "-%c needs a value", t[k]);
						rc = 2;
						goto done;
					}
					if (op_put(s, o, av[++a]) != HIBR_OK) {
						rc = 2;
						goto done;
					}
				}
				break;
			}
			continue;
		}
		v_add(pos, av[a]);
	}
	for (i = 0; i < s->opts.n; i++) {
		o = (struct ospec *)s->opts.p[i];
		if (o->seen)
			continue;
		if (o->req) {
			lg(HIBR_LERR, "%s is required (%s)", o->nm,
			   (char *)o->sw.p[o->sw.n - 1]);
			rc = 2;
			goto done;
		}
		if (o->dflt)
			hibr_set(s, o->nm, o->dflt, 0);
		else if (o->isbool)
			hibr_set(s, o->nm, "0", 0);
		else if (o->rep) {
			vec empty = { 0, 0, 0 };
			v_arr(s, o->nm, &empty);
		} else
			hibr_set(s, o->nm, "", 0);
	}
	v_arr(s, "ARGS", pos);
	v_pos(s, (int)pos->n, (char **)pos->p);
	lg(HIBR_LDBG, "args: %lu positional", (unsigned long)pos->n);
done:
	if (rc != HIBR_OK) {
		op_usage(s, stderr);
		s->st = rc;
		if (!s->it && !s->dep && !s->intry)
			s->quit = 1;
	}
	vb_put(s, pos);
	lg(HIBR_LDBG, "args: declarations used up");
	op_clear(s);
	return rc;
}
