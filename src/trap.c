#include "pri.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

volatile sig_atomic_t *tr_pend;
volatile sig_atomic_t tr_any;
int tr_n;

/* Record that a trapped signal arrived. */
void on_trap(int sig)
{
	if (tr_pend && sig > 0 && sig < tr_n)
		tr_pend[sig] = 1;
	tr_any = 1;
}

/* Allocate the trap and pending tables. */
void tr_init(sh *s)
{
	int i;

	tr_n = NSIG;
	tr_pend = xm((size_t)tr_n * sizeof *tr_pend);
	s->trap = xm((size_t)(tr_n + 1) * sizeof *s->trap);
	for (i = 0; i < tr_n; i++) {
		tr_pend[i] = 0;
		s->trap[i] = 0;
	}
	s->trap[tr_n] = 0;
}

/* True when a trapped signal is waiting to be handled. */
int tr_pending(void)
{
	return tr_any != 0;
}

/* Run the handlers for every signal that has arrived. */
void tr_run(sh *s)
{
	int i, ost = s->st;
	char *cmd;

	tr_any = 0;
	if (!s->trap || !tr_pend)
		return;
	for (i = 1; i < tr_n; i++) {
		if (!tr_pend[i])
			continue;
		tr_pend[i] = 0;
		cmd = s->trap[i];
		if (!cmd || !*cmd)
			continue;
		lg(HIBR_LDBG, "running trap for signal %d", i);
		hibr_run(s, cmd);
		s->stop = 0;
	}
	s->st = ost;
}

/* Forget an inherited exit trap in a freshly forked child. */
void tr_fork(sh *s)
{
	if (!s->trap || !s->trap[0])
		return;
	lg(HIBR_LDBG, "subshell forgets the inherited exit trap");
	free(s->trap[0]);
	s->trap[0] = 0;
}

/* Run the exit trap, if one is set. */
void tr_exit(sh *s)
{
	char *cmd;

	if (!s->trap || !s->trap[0])
		return;
	cmd = s->trap[0];
	s->trap[0] = 0;
	s->quit = 0;
	s->stop = 0;
	lg(HIBR_LDBG, "running exit trap");
	hibr_run(s, cmd);
	free(cmd);
	s->quit = 1;
}

/* Run the error trap for a failed command. */
void tr_err(sh *s, int st)
{
	char *cmd = s->etrap;
	int ost = s->st;

	if (!cmd || s->intry)
		return;
	s->etrap = 0;
	s->st = st;
	lg(HIBR_LDBG, "running error trap for status %d", st);
	hibr_run(s, cmd);
	s->stop = 0;
	s->st = ost;
	s->etrap = cmd;
}

/* Report a failure with a message other code can read. */
int b_fail(sh *s, int ac, char **av)
{
	int i = 1, st = 1;
	str b;

	if (ac > 2 && !strcmp(av[1], "-s")) {
		st = atoi(av[2]);
		i = 3;
	}
	s_init(&b);
	for (; i < ac; i++) {
		if (b.n)
			s_ch(&b, ' ');
		s_cat(&b, av[i]);
	}
	hibr_set(s, "ERRMSG", b.p ? b.p : "", 0);
	lg(HIBR_LDBG, "fail: %s", b.p ? b.p : "");
	s_free(&b);
	if (s->dep)
		s->ret = 1;
	return s->st = st;
}

/* Run a command, catching failure instead of propagating it. */
int b_try(sh *s, int ac, char **av)
{
	str cmd;
	int i, oi = s->intry, st;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: try command [args...]");
		return 2;
	}
	hibr_set(s, "ERRMSG", "", 0);
	s_init(&cmd);
	for (i = 1; i < ac; i++) {
		if (cmd.n)
			s_ch(&cmd, ' ');
		s_ch(&cmd, '\'');
		{
			const char *p = av[i];
			for (; *p; p++) {
				if (*p == '\'')
					s_cat(&cmd, "'\\''");
				else
					s_ch(&cmd, *p);
			}
		}
		s_ch(&cmd, '\'');
	}
	s->intry = 1;
	hibr_run(s, cmd.p);
	st = s->st;
	s->intry = oi;
	s->stop = 0;
	s_free(&cmd);
	hibr_set(s, "ERR", st ? "1" : "0", 0);
	{
		str n;
		s_init(&n);
		s_num(&n, (long)st);
		hibr_set(s, "ERRSTATUS", n.p, 0);
		s_free(&n);
	}
	lg(HIBR_LTRC, "try caught status %d", st);
	return s->st = HIBR_OK;
}

/* Run the debug trap before a command, keeping the shell's state intact. */
void tr_debug(sh *s, const char *what)
{
	char *cmd = s->dtrap;
	int ost, ostop, obind, oxerr, obrk, ocont, oret;

	if (!cmd)
		return;
	ost = s->st;
	ostop = s->stop;
	obind = s->bind;
	oxerr = s->xerr;
	obrk = s->brk;
	ocont = s->cont;
	oret = s->ret;
	s->dtrap = 0;
	if (what)
		hibr_set(s, "CMD", what, 0);
	hibr_run(s, cmd);
	s->dtrap = cmd;
	s->st = ost;
	s->stop = ostop;
	s->bind = obind;
	s->xerr = oxerr;
	s->brk = obrk;
	s->cont = ocont;
	s->ret = oret;
}

/* Translate a signal name or number, with EXIT as slot zero. */
int tr_sig(const char *nm)
{
	const struct signm *sm;

	if (isdigit((unsigned char)nm[0]))
		return atoi(nm);
	if (!strcasecmp(nm, "EXIT") || !strcmp(nm, "0"))
		return 0;
	if (!strcasecmp(nm, "ERR"))
		return -2;
	if (!strcasecmp(nm, "DEBUG"))
		return -3;
	if (!strncasecmp(nm, "SIG", 3))
		nm += 3;
	for (sm = jc_sigs; sm->nm; sm++)
		if (!strcasecmp(sm->nm, nm))
			return sm->sig;
	return -1;
}

/* Set, clear or list signal handlers. */
int b_trap(sh *s, int ac, char **av)
{
	int i, sig, reset = 0;
	const char *cmd;
	struct sigaction sa;

	if (!s->trap)
		tr_init(s);
	if (ac == 1) {
		if (s->etrap)
			printf("trap -- '%s' ERR\n", s->etrap);
		if (s->dtrap)
			printf("trap -- '%s' DEBUG\n", s->dtrap);
		for (i = 0; i < tr_n; i++)
			if (s->trap[i]) {
				const struct signm *sm;
				const char *nm = "EXIT";
				for (sm = jc_sigs; i && sm->nm; sm++)
					if (sm->sig == i)
						nm = sm->nm;
				printf("trap -- '%s' %s%s\n", s->trap[i],
				       i ? "SIG" : "", nm);
			}
		return HIBR_OK;
	}
	cmd = av[1];
	i = 2;
	if (!strcmp(cmd, "-")) {
		reset = 1;
	} else if (ac == 2) {
		lg(HIBR_LERR, "trap: a signal name is required");
		return HIBR_FAIL;
	}
	for (; i < ac; i++) {
		sig = tr_sig(av[i]);
		if (sig == -2) {
			free(s->etrap);
			s->etrap = reset || !*cmd ? 0 : xs(cmd);
			continue;
		}
		if (sig == -3) {
			free(s->dtrap);
			s->dtrap = reset || !*cmd ? 0 : xs(cmd);
			continue;
		}
		if (sig < 0 || sig >= tr_n) {
			lg(HIBR_LERR, "trap: %s: unknown signal", av[i]);
			return HIBR_FAIL;
		}
		free(s->trap[sig]);
		s->trap[sig] = reset || !*cmd ? 0 : xs(cmd);
		if (!sig)
			continue;
		memset(&sa, 0, sizeof sa);
		if (reset)
			sa.sa_handler = SIG_DFL;
		else if (!*cmd)
			sa.sa_handler = SIG_IGN;
		else
			sa.sa_handler = on_trap;
		if (sigaction(sig, &sa, 0) < 0) {
			lg(HIBR_LERR, "trap: %s: %s", av[i], strerror(errno));
			return HIBR_FAIL;
		}
		lg(HIBR_LDBG, "trap on signal %d %s", sig,
		   reset ? "cleared" : "installed");
	}
	return HIBR_OK;
}

/* Expand backslash escapes into a buffer. */
void pf_esc(str *o, const char *p, int stop_at_c)
{
	int v, i;

	while (*p) {
		if (*p != '\\') {
			s_ch(o, *p++);
			continue;
		}
		p++;
		switch (*p) {
		case 'n': s_ch(o, '\n'); p++; break;
		case 't': s_ch(o, '\t'); p++; break;
		case 'r': s_ch(o, '\r'); p++; break;
		case 'a': s_ch(o, '\a'); p++; break;
		case 'b': s_ch(o, '\b'); p++; break;
		case 'f': s_ch(o, '\f'); p++; break;
		case 'v': s_ch(o, '\v'); p++; break;
		case 'e': s_ch(o, 27); p++; break;
		case '\\': s_ch(o, '\\'); p++; break;
		case 'c':
			if (stop_at_c)
				return;
			s_ch(o, 'c');
			p++;
			break;
		case '0':
			p++;
			v = 0;
			for (i = 0; i < 3 && *p >= '0' && *p <= '7'; i++)
				v = v * 8 + (*p++ - '0');
			s_ch(o, v);
			break;
		default:
			s_ch(o, '\\');
			if (*p)
				s_ch(o, *p++);
			break;
		}
	}
}

/* Largest width or precision requested by a conversion spec. */
size_t pf_wide(const char *spec)
{
	size_t w = 0, cur = 0;

	for (; *spec; spec++) {
		if (*spec >= '0' && *spec <= '9') {
			cur = cur * 10 + (size_t)(*spec - '0');
			if (cur > w)
				w = cur;
		} else {
			cur = 0;
		}
	}
	return w;
}

/* Emit one conversion with its flags, width and precision. */
void pf_one(str *o, const char *spec, char cv, const char *arg)
{
	str f;
	char *e;
	size_t room = pf_wide(spec) + 64;

	s_init(&f);
	s_cat(&f, spec);
	switch (cv) {
	case 'd':
	case 'i':
		s_cat(&f, "ld");
		s_grow(o, room);
		o->n += (size_t)sprintf(o->p + o->n, f.p,
					arg ? strtol(arg, 0, 0) : 0L);
		break;
	case 'u':
	case 'x':
	case 'X':
	case 'o':
		s_ch(&f, 'l');
		s_ch(&f, cv);
		s_grow(o, room);
		o->n += (size_t)sprintf(o->p + o->n, f.p,
					arg ? strtoul(arg, 0, 0) : 0UL);
		break;
	case 'f':
	case 'e':
	case 'E':
	case 'g':
	case 'G':
		s_ch(&f, cv);
		s_grow(o, room + 320);
		o->n += (size_t)sprintf(o->p + o->n, f.p,
					arg ? strtod(arg, &e) : 0.0);
		break;
	case 'c':
		s_ch(&f, 'c');
		s_grow(o, room);
		o->n += (size_t)sprintf(o->p + o->n, f.p, arg ? arg[0] : 0);
		break;
	default:
		s_ch(&f, 's');
		s_grow(o, strlen(arg ? arg : "") + room);
		o->n += (size_t)sprintf(o->p + o->n, f.p, arg ? arg : "");
		break;
	}
	o->p[o->n] = 0;
	s_free(&f);
}

/* Write formatted output, recycling the format over the arguments. */
int b_printf(sh *s, int ac, char **av)
{
	str o, spec;
	const char *p, *into = 0;
	int i = 2, used;

	if (ac > 2 && !strcmp(av[1], "-v")) {
		into = av[2];
		av += 2;
		ac -= 2;
	}
	if (ac < 2) {
		lg(HIBR_LERR, "printf: a format is required");
		return HIBR_FAIL;
	}
	s_init(&o);
	do {
		used = 0;
		for (p = av[1]; *p;) {
			if (*p != '%') {
				if (*p == '\\') {
					str t;
					const char *q = p;
					s_init(&t);
					while (*q && *q != '%')
						q++;
					{
						char *seg = xm((size_t)(q - p) + 1);
						memcpy(seg, p, (size_t)(q - p));
						seg[q - p] = 0;
						pf_esc(&t, seg, 1);
						free(seg);
					}
					s_add(&o, t.p ? t.p : "", t.n);
					s_free(&t);
					p = q;
					continue;
				}
				s_ch(&o, *p++);
				continue;
			}
			p++;
			if (*p == '%') {
				s_ch(&o, '%');
				p++;
				continue;
			}
			s_init(&spec);
			s_ch(&spec, '%');
			while (*p && strchr("-+ #0", *p))
				s_ch(&spec, *p++);
			while (isdigit((unsigned char)*p))
				s_ch(&spec, *p++);
			if (*p == '.') {
				s_ch(&spec, *p++);
				while (isdigit((unsigned char)*p))
					s_ch(&spec, *p++);
			}
			if (!*p) {
				s_add(&o, spec.p, spec.n);
				s_free(&spec);
				break;
			}
			if (*p == '(') {
				const char *cl = strchr(p, ')');
				str fm;
				time_t tv;
				struct tm *tmv;
				size_t got, cap;
				char *buf;
				if (!cl || cl[1] != 'T') {
					s_ch(&o, *p++);
					s_free(&spec);
					continue;
				}
				s_init(&fm);
				s_add(&fm, p + 1, (size_t)(cl - p - 1));
				tv = i < ac ? (time_t)atol(av[i]) : time(0);
				if (i < ac && !strcmp(av[i], "-1"))
					tv = time(0);
				tmv = localtime(&tv);
				cap = fm.n * 8 + 64;
				buf = xm(cap);
				got = strftime(buf, cap, fm.p ? fm.p : "", tmv);
				s_add(&o, buf, got);
				free(buf);
				s_free(&fm);
				s_free(&spec);
				if (i < ac)
					used = 1;
				i++;
				p = cl + 2;
				continue;
			}
			if (*p == 'q') {
				s_cat(&o, xquote(s, i < ac ? av[i] : "", 1));
				s_free(&spec);
				if (i < ac)
					used = 1;
				i++;
				p++;
				continue;
			}
			if (*p == 'b') {
				str t;
				s_init(&t);
				pf_esc(&t, i < ac ? av[i] : "", 1);
				s_add(&o, t.p ? t.p : "", t.n);
				s_free(&t);
				s_free(&spec);
				if (i < ac)
					used = 1;
				i++;
				p++;
				continue;
			}
			pf_one(&o, spec.p, *p, i < ac ? av[i] : 0);
			s_free(&spec);
			if (i < ac)
				used = 1;
			i++;
			p++;
		}
	} while (i < ac && used);
	hibr_set(s, "RET", o.p ? o.p : "", 0);
	if (into)
		hibr_set(s, into, o.p ? o.p : "", 0);
	else if (!s->bind) {
		fwrite(o.p ? o.p : "", 1, o.n, stdout);
		fflush(stdout);
	}
	s_free(&o);
	return HIBR_OK;
}

/* Release the trap tables. */
void tr_fini(sh *s)
{
	int i;

	for (i = 0; s->trap && i < tr_n; i++)
		free(s->trap[i]);
	free(s->trap);
	s->trap = 0;
	free(s->etrap);
	s->etrap = 0;
	free((void *)tr_pend);
	tr_pend = 0;
}
