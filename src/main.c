#include "pri.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t g_int;

/* Record an interrupt without disturbing the running command. */
void on_int(int sig)
{
	(void)sig;
	g_int = 1;
}

/* Install interactive signal handling. */
void sig_init(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_int;
	sigaction(SIGINT, &sa, 0);
	signal(SIGQUIT, SIG_IGN);
}

/* Read one line of input, returning heap memory or NULL. */
char *rdline(FILE *f)
{
	str b;
	int c;

	s_init(&b);
	while ((c = fgetc(f)) != EOF) {
		s_ch(&b, c);
		if (c == '\n')
			break;
	}
	if (c == EOF && !b.n) {
		s_free(&b);
		return 0;
	}
	return b.p;
}

/* Hand the current parse arena to long lived storage when needed. */
void recycle(sh *s)
{
	if (s->keep) {
		v_add(&s->held, s->ar);
		s->ar = ar_new(HIBR_ARCH);
		s->keep = 0;
		lg(HIBR_LDBG, "retained parse arena (%lu held)",
		   (unsigned long)s->held.n);
		return;
	}
	ar_reset(s->ar);
	ar_reset(s->xa);
}

/* Parse and execute a source string in the current shell. */
int hibr_run(sh *s, const char *src)
{
	amark m = ar_mark(s->ar);
	amark xm2 = ar_mark(s->xa);
	int okeep = s->keep;
	node *n;
	int more = 0;

	s->keep = 0;
	n = hibr_parse(s, src, &more);
	if (more) {
		lg(HIBR_LERR, "unexpected end of input");
		s->keep = okeep;
		return s->st = HIBR_FAIL;
	}
	if (n && !s->noexec)
		ex(s, n);
	if (!s->keep) {
		ar_rel(s->xa, xm2);
		ar_rel(s->ar, m);
	}
	s->keep = s->keep || okeep;
	return s->st;
}

/* Run the interactive read, parse and execute loop. */
int loop(sh *s)
{
	str acc;
	char *line;
	node *n;
	const char *ps;
	char *pr;
	int more = 0;

	s_init(&acc);
	while (!s->quit) {
		if (!acc.n)
			jc_poll(s, 1);
		pr = acc.n ? 0 : pr_hook(s);
		if (!pr) {
			ps = acc.n ? hibr_get(s, "PS2") : hibr_get(s, "PS1");
			if (!ps)
				ps = acc.n ? "> " : "\\u@\\h \\w\\$ ";
			pr = pr_make(s, ps);
		}
		ps = pr;
		fflush(0);
		line = ed_line(s, ps);
		free(pr);
		if (line && s->hx && strchr(line, '!')) {
			int ch = 0, bad = 0;
			size_t ln = strlen(line);
			char *ex;
			if (ln && line[ln - 1] == '\n')
				line[ln - 1] = 0;
			if (s->hist.n && !strcmp((char *)s->hist.p[s->hist.n - 1], line)) {
				free(s->hist.p[--s->hist.n]);
			}
			ex = hx_expand(s, line, &ch, &bad);
			free(line);
			if (bad) {
				free(ex);
				acc.n = 0;
				continue;
			}
			if (ch)
				fprintf(stderr, "%s\n", ex);
			hs_add(s, ex);
			line = xm(strlen(ex) + 2);
			strcpy(line, ex);
			strcat(line, "\n");
			free(ex);
		}
		if (!line) {
			if (g_int || (ferror(stdin) && errno == EINTR)) {
				g_int = 0;
				clearerr(stdin);
				acc.n = 0;
				fputc('\n', stderr);
				continue;
			}
			fputc('\n', stderr);
			break;
		}
		s_cat(&acc, line);
		free(line);
		n = hibr_parse(s, acc.p, &more);
		if (more)
			continue;
		if (n) {
			struct timespec c0, c1;
			str d;
			clock_gettime(CLOCK_MONOTONIC, &c0);
			ex(s, n);
			clock_gettime(CLOCK_MONOTONIC, &c1);
			s_init(&d);
			s_num(&d, (c1.tv_sec - c0.tv_sec) * 1000 +
					  (c1.tv_nsec - c0.tv_nsec) / 1000000);
			hibr_set(s, "DURATION", d.p, 0);
			s_free(&d);
		}
		s->stop = 0;
		recycle(s);
		acc.n = 0;
		if (acc.p)
			acc.p[0] = 0;
		g_int = 0;
	}
	s_free(&acc);
	return s->st;
}

/* Read an entire stream into a dynamic string. */
char *slurp(FILE *f)
{
	str b;
	char *buf = xm(HIBR_IOCH);
	size_t n;

	s_init(&b);
	while ((n = fread(buf, 1, HIBR_IOCH, f)) > 0)
		s_add(&b, buf, n);
	free(buf);
	if (!b.p)
		s_ch(&b, 0), b.n = 0;
	return b.p;
}

/* Release every shell resource. */
void sh_fini(sh *s)
{
	size_t i;
	var *v, *nv;

	tr_exit(s);
	tr_fini(s);
	jc_fini(s);
	ed_fini(s);
	al_fini(s);
	sc_fini(s);
	op_clear(s);
	v_free(&s->opts);
	free(s->arg0);
	m_fini(s);
	for (i = 0; i < s->held.n; i++)
		ar_free((arena *)s->held.p[i]);
	v_free(&s->held);
	for (i = 0; i < s->sbf.n; i++) {
		s_free((str *)s->sbf.p[i]);
		free(s->sbf.p[i]);
	}
	v_free(&s->sbf);
	for (i = 0; i < s->vbf.n; i++) {
		v_free((vec *)s->vbf.p[i]);
		free(s->vbf.p[i]);
	}
	v_free(&s->vbf);
	v_free(&s->fns);
	v_free(&s->scope);
	v_free(&s->psub);
	hsh_clear(s, 0);
	v_free(&s->cmds);
	ar_free(s->ar);
	ar_free(s->xa);
	for (i = 0; i < s->tsz; i++)
		for (v = s->tab[i]; v; v = nv) {
			nv = v->nx;
			v_free_el(v);
			free(v->k);
			free(v->v);
			free(v);
		}
	free(s->tab);
	if (s->avo && s->av) {
		int j;
		for (j = 0; j < s->ac; j++)
			free(s->av[j]);
		free(s->av);
	}
}

/* Start the shell. */
int main(int ac, char **av)
{
	sh s;
	char *src = 0, *text;
	FILE *f;
	int i = 1, rc;
	char *p;

	memset(&s, 0, sizeof s);
	s.ar = ar_new(HIBR_ARCH);
	s.xa = ar_new(HIBR_ARCH);
	s.arg0 = xs(av[0]);
	s.pid = (long)getpid();
	pt_init(ac, av);
	v_env(&s);
	tr_init(&s);
	s.t0 = (long)time(0);
	srand((unsigned)(s.t0 ^ (long)getpid()));
	hibr_set(&s, "HIBR_VERSION", HIBR_VER, 0);
	{
		str b;
		char *hn = xm(256);
		s_init(&b);
		s_num(&b, (long)getppid());
		hibr_set(&s, "PPID", b.p, 0);
		b.n = 0;
		s_num(&b, (long)getuid());
		hibr_set(&s, "UID", b.p, 0);
		b.n = 0;
		s_num(&b, (long)geteuid());
		hibr_set(&s, "EUID", b.p, 0);
		b.n = 0;
		s_num(&b, (long)HIBR_ABI);
		hibr_set(&s, "HIBR_ABI", b.p, 0);
		s_free(&b);
		if (!hibr_get(&s, "HOSTNAME") && gethostname(hn, 255) == 0) {
			hn[255] = 0;
			hibr_set(&s, "HOSTNAME", hn, 0);
		}
		free(hn);
	}
	p = getenv("HIBR_DEBUG");
	if (p)
		hibr_lv = atoi(p);
	for (; i < ac; i++) {
		if (!strcmp(av[i], "-c") && i + 1 < ac) {
			src = xs(av[++i]);
			i++;
			break;
		}
		if (!strcmp(av[i], "-d") && i + 1 < ac) {
			hibr_lv = atoi(av[++i]);
			continue;
		}
		if (!strcmp(av[i], "-v") || !strcmp(av[i], "--version")) {
			printf("hibr %s abi %u\n", HIBR_VER, HIBR_ABI);
			sh_fini(&s);
			return 0;
		}
		if (!strcmp(av[i], "-n")) {
			s.noexec = 1;
			continue;
		}
		if (!strcmp(av[i], "-h") || !strcmp(av[i], "--help")) {
			printf("usage: hibr [-d level] [-n] [script [args...]]\n"
			       "       hibr -c 'commands' [args...]\n"
			       "       hibr -v | -h\n\n"
			       "  -c   run the given commands\n"
			       "  -n   parse only, report syntax errors, run nothing\n"
			       "  -d   log level 0-4 (error, warn, info, debug, trace)\n"
			       "  -v   print version and module ABI\n\n"
			       "Interactive when stdin is a terminal: reads ~/.hibrc,\n"
			       "line editing, history, job control, completion.\n"
			       "Type help for the builtin list.\n");
			sh_fini(&s);
			return 0;
		}
		break;
	}
	if (src) {
		if (i < ac) {
			free(s.arg0);
			s.arg0 = xs(av[i]);
			i++;
		}
		if (i < ac)
			v_pos(&s, ac - i, av + i);
		rc = hibr_run(&s, src);
		free(src);
		sh_fini(&s);
		return rc;
	}
	if (i < ac) {
		f = fopen(av[i], "r");
		if (!f) {
			lg(HIBR_LERR, "%s: %s", av[i], strerror(errno));
			sh_fini(&s);
			return HIBR_NOCMD;
		}
		free(s.arg0);
		s.arg0 = xs(av[i]);
		if (i + 1 < ac)
			v_pos(&s, ac - i - 1, av + i + 1);
		text = slurp(f);
		fclose(f);
		rc = hibr_run(&s, text);
		free(text);
		sh_fini(&s);
		return rc;
	}
	if (isatty(0)) {
		s.it = 1;
		s.hx = 1;
		sig_init();
		jc_init(&s);
		ed_init(&s);
		rc_load(&s);
		lg(HIBR_LINF, "interactive shell, pid %ld", (long)getpid());
		rc = loop(&s);
	} else {
		text = slurp(stdin);
		rc = hibr_run(&s, text);
		free(text);
	}
	sh_fini(&s);
	return rc;
}
