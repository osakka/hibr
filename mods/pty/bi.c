#define _GNU_SOURCE

#include "tt.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern vec tt_list;

/* Look a pty up by the id in a word, complaining if it is not one. */
tt_p *tt_arg(const char *sub, const char *t)
{
	tt_p *p = t ? tt_find(atoi(t)) : 0;

	if (!p)
		lg(HIBR_LERR, "pty %s: %s: no such pty", sub, t ? t : "");
	return p;
}

/* Hand a value back through the result slot, printing it only when nobody
   asked for it, the same way console size does. */
void tt_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* Run a program on a pseudo terminal of its own. */
int m_pty(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows = 24, cols = 80, i, r;
	tt_p *p;
	str o;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: pty spawn|read|drain|write|resize|size|"
			      "alive|wait|signal|pid|close|list ...");
		return 2;
	}
	if (!strcmp(sub, "spawn")) {
		i = 2;
		while (i < ac && av[i][0] == '-' && av[i][1]) {
			if (!strcmp(av[i], "-r") && i + 1 < ac)
				rows = atoi(av[++i]);
			else if (!strcmp(av[i], "-c") && i + 1 < ac)
				cols = atoi(av[++i]);
			else if (!strcmp(av[i], "--")) {
				i++;
				break;
			} else {
				lg(HIBR_LERR, "pty spawn: %s: unknown option",
				   av[i]);
				return 2;
			}
			i++;
		}
		if (i >= ac) {
			lg(HIBR_LERR, "usage: pty spawn [-r rows] [-c cols] "
				      "command [args...]");
			return 2;
		}
		if (rows < 1 || cols < 1) {
			lg(HIBR_LERR, "pty spawn: size must be positive");
			return 2;
		}
		p = tt_spawn(s, rows, cols, av + i);
		if (!p)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, (long)p->id);
		tt_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "read")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		s_init(&o);
		r = tt_read(p, ac > 3 ? atoi(av[3]) : 0, &o);
		tt_ret(s, o.p ? o.p : "");
		s_free(&o);
		return r < 0 ? HIBR_FAIL : HIBR_OK;
	}
	if (!strcmp(sub, "drain")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		s_init(&o);
		i = ac > 3 ? atoi(av[3]) : 1000;
		while (tt_read(p, i, &o) > 0)
			;
		tt_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "write")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: pty write id text...");
			return 2;
		}
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		for (i = 3; i < ac; i++) {
			if (i > 3 && tt_write(p, " ", 1) != 1)
				return HIBR_FAIL;
			if (tt_write(p, av[i], strlen(av[i])) !=
			    (long)strlen(av[i]))
				return HIBR_FAIL;
		}
		return HIBR_OK;
	}
	if (!strcmp(sub, "resize")) {
		if (ac < 5) {
			lg(HIBR_LERR, "usage: pty resize id rows cols");
			return 2;
		}
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		return tt_resize(p, atoi(av[3]), atoi(av[4])) ? HIBR_OK
							      : HIBR_FAIL;
	}
	if (!strcmp(sub, "size")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, (long)p->rows);
		s_ch(&o, ' ');
		s_num(&o, (long)p->cols);
		tt_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "alive")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		return tt_alive(p) ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "wait")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		if (!tt_wait(p, ac > 3 ? atoi(av[3]) : -1))
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, (long)p->st);
		tt_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "signal")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: pty signal id number");
			return 2;
		}
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		return kill((pid_t)p->pid, atoi(av[3])) == 0 ? HIBR_OK
							     : HIBR_FAIL;
	}
	if (!strcmp(sub, "pid")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, p->pid);
		tt_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "close")) {
		if (ac < 3)
			return 2;
		p = tt_arg(sub, av[2]);
		if (!p)
			return HIBR_FAIL;
		tt_drop(p);
		return HIBR_OK;
	}
	if (!strcmp(sub, "list")) {
		size_t j;
		s_init(&o);
		for (j = 0; j < tt_list.n; j++) {
			if (j)
				s_ch(&o, ' ');
			s_num(&o, (long)((tt_p *)tt_list.p[j])->id);
		}
		tt_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "pty: %s: unknown subcommand", sub);
	return HIBR_FAIL;
}

/* Kill anything still running and give the descriptors back. */
void tt_fini(sh *s)
{
	(void)s;
	tt_all();
}

const hibr_bi pty_bi[] = {
	{ "pty", m_pty, "run a program on a pseudo terminal of its own" },
	HIBR_BI_END
};

HIBR_MODULE_P("pty", "0.21",
	      "pseudo terminals: spawn a program on one and drive it",
	      pty_bi, 0, tt_fini, "pty");
