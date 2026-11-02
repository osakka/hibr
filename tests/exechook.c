/* A module with a child of its own that must not outlive an exec in place,
   standing in for the web module's engine (the hibr-wpe zombies Restart
   Desktop left behind, one per restart). Its init starts the child and
   registers an exec hook that ends it and waits for it; `exechook pid`
   names the child. A fixture of the suite's, not a real module. */
#include "hibr.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t eh_child;

/* End the child and wait for it, as exec is about to replace us. */
void eh_hook(sh *s, void *arg)
{
	int w = 0;

	(void)s;
	(void)arg;
	if (eh_child <= 0)
		return;
	kill(eh_child, SIGTERM);
	if (waitpid(eh_child, &w, 0) == eh_child)
		fprintf(stderr, "exechook: child ended before the exec\n");
	eh_child = 0;
}

/* exechook pid: the child's pid. */
int m_exechook(sh *s, int ac, char **av)
{
	(void)s;
	(void)ac;
	(void)av;
	printf("%ld\n", (long)eh_child);
	return HIBR_OK;
}

const hibr_bi exechook_bi[] = {
	{ "exechook", m_exechook, "the fixture's child" },
	HIBR_BI_END
};

/* Start the child and register the hook. The child ends when the pipe
   from us closes, which an exec does, as the engine ends when its pipe
   does: unwaited, it would then be a zombie of the image that replaced us. */
int eh_ini(sh *s)
{
	int pf[2];
	char c;

	if (pipe(pf) < 0)
		return HIBR_FAIL;
	eh_child = fork();
	if (eh_child == 0) {
		close(pf[1]);
		while (read(pf[0], &c, 1) > 0)
			;
		_exit(0);
	}
	close(pf[0]);
	fcntl(pf[1], F_SETFD, FD_CLOEXEC);
	hibr_onexec(s, eh_hook, 0);
	return HIBR_OK;
}

/* Withdraw the hook; the child goes with the shell. */
void eh_fin(sh *s)
{
	hibr_offexec(s, eh_hook, 0);
	if (eh_child > 0) {
		kill(eh_child, SIGTERM);
		waitpid(eh_child, 0, 0);
	}
}

HIBR_MODULE("exechook", "1.0", "a child an exec in place must end first",
	    exechook_bi, eh_ini, eh_fin);
