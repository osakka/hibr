#ifndef HIBR_PTY_H
#define HIBR_PTY_H

#include "hibr.h"

/* The interface the pty module offers through hibr_provide, under the name
   "pty". A terminal emulator needs the bytes a program writes and a way to
   send it keystrokes, and it should not carry its own copy of forkpty to get
   them. Anything added here is a new version; anything reordered or removed
   breaks every module already using it. */

#ifndef PY_API_VER
#define PY_API_VER 4u
#endif

typedef struct py_api py_api;
struct py_api {
	/* Start a program on a terminal of its own; returns an id, or 0. */
	int (*spawn)(sh *s, int rows, int cols, char **av);
	/* Collect what it has written: 1 with something, 0 with nothing yet,
	   -1 at end of file. Waits up to ms for the first byte. */
	int (*read)(int id, int ms, str *out);
	/* Send it bytes, as if typed. Returns how many went. */
	long (*write)(int id, const char *t, size_t n);
	int (*resize)(int id, int rows, int cols);
	int (*alive)(int id);
	int (*status)(int id);
	long (*pid)(int id);
	void (*drop)(int id);
	/* The master's descriptor, for a caller that waits on it alongside
	   others; -1 for no such terminal. Added in version 2. */
	int (*fd)(int id);
	/* Version 3. The size in pixels as well as in cells, which is the
	   only way a program on this terminal can learn what a cell measures
	   -- and so whether it can put pixels on screen at all (ADR 0037).
	   `resize` keeps whatever was last set here. 0 for no such id. */
	int (*resizepx)(int id, int rows, int cols, int xpix, int ypix);
	/* Version 4, for a server that re-execs and takes its own session
	   back (Gitea #198). `adopt` takes over a terminal that is already
	   open -- a master descriptor and the pid of the program on it --
	   and `release` forgets one without ending the program or closing
	   the descriptor, which is what the process about to exec must do
	   so the exec does not take the program with it. Both are what the
	   `pty adopt` and `pty release` builtins already do; this is the
	   same thing for a module that has no shell to ask. */
	int (*adopt)(int fd, long pid, int rows, int cols);
	int (*release)(int id);
};

#endif
