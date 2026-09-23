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

/* Move the environment off the argv region, so a title may overwrite it. */
void pt_claim(void)
{
	extern char **environ;
	char **e, **copy;
	size_t n = 0, i;

	if (pt_taken)
		return;
	pt_taken = 1;
	for (e = environ; e && *e; e++)
		n++;
	copy = xm((n + 1) * sizeof *copy);
	for (i = 0; i < n; i++)
		copy[i] = xs(environ[i]);
	copy[n] = 0;
	environ = copy;
	lg(HIBR_LDBG, "environment copied off the argv region");
}

/* Rename the running process everywhere it shows: the argv region ps reads,
   and the kernel's short name /proc/pid/comm and top's default column read.
   Shared by the `title` builtin and by hibr_title, so a module can say who
   it is the same way a script does -- hold uses it for the server it forks
   and the terminal that attaches to one, neither of which is a script. */
void pt_rename(const char *name)
{
	size_t room, n;

	if (!pt_start || pt_end <= pt_start)
		return;
	pt_claim();
	n = strlen(name);
	room = (size_t)(pt_end - pt_start);
	memset(pt_start, 0, room);
	memcpy(pt_start, name, n < room - 1 ? n : room - 1);
#ifdef __linux__
	prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
#elif defined(__APPLE__)
	pthread_setname_np(name);
#endif
	lg(HIBR_LDBG, "process title now %s", name);
}

/* Rename the running process as seen by ps and top. */
int b_title(sh *s, int ac, char **av)
{
	str t;
	size_t i;

	(void)s;
	if (ac < 2) {
		lg(HIBR_LERR, "usage: title name...");
		return 2;
	}
	if (!pt_start || pt_end <= pt_start) {
		lg(HIBR_LERR, "title: argv region unavailable");
		return HIBR_FAIL;
	}
	s_init(&t);
	for (i = 1; i < (size_t)ac; i++) {
		if (t.n)
			s_ch(&t, ' ');
		s_cat(&t, av[i]);
	}
	pt_rename(t.p);
	s_free(&t);
	return HIBR_OK;
}

/* The same rename, for a module rather than a script. */
void hibr_title(const char *name)
{
	pt_rename(name);
}

/* Release the option specifications. */
void op_clear(sh *s)
{
	struct ospec *o;
	size_t i;

	while (s->opts.n) {
		o = (struct ospec *)s->opts.p[--s->opts.n];
		for (i = 0; i < o->sw.n; i++)
			free(o->sw.p[i]);
		v_free(&o->sw);
		free(o->nm);
		free(o->ty);
		free(o->dflt);
		free(o->help);
		free(o);
	}
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

/* Parse arguments against the declared options. */
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
	return rc;
}
