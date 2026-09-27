#ifndef HD_H
#define HD_H

#include "hibr.h"
#include "../display.h"
#include "../pty.h"
#include "../term.h"

enum {
	HD_ATTACH = 'a',
	HD_DATA = 'd',
	HD_SIZE = 'w',
	HD_DETACH = 'q',
	HD_EXIT = 'x',
	HD_KILL = 'k',
	HD_INFO = 'i',
	/* Join as an additional client rather than taking over: everyone
	   already attached stays attached. */
	HD_MATTACH = 'm',
	/* One line per attached client: name row col rows cols primary. */
	HD_CLIENTS = 'c',
	/* Reposition a named client: name row col. */
	HD_MOVE = 'v',
	/* Detach a named client, server-side, by name rather than by fd. */
	HD_DROP = 'o',
	/* Make a named client the primary; every other client stops being it. */
	HD_PRIMARY = 'p'
};

extern const py_api *hd_pty;
extern const tm_api *hd_tm;

/* One cell as this client was last sent it -- hold's own copy, not term's:
   hold must not include tm.h just to name tm_cell, and the two have no
   reason to stay the same shape. cp is a code point (0 is never a real one
   tm_api hands back, ~0u a cell this client has not been sent yet at all,
   so a fresh or just-resized front is invalid everywhere without a second
   fill pass). */
struct hd_cell { unsigned cp, fg, bg, attr; };

/* One attached terminal: its connection, where it sits in the session's own
   virtual space -- row,col its top-left corner, rows,cols its own size --
   the mode state last sent to it, so a real terminal is only ever told to
   switch buffers, mice or paste mode on a genuine change, not every frame,
   and front: what it was last sent, cell by cell, so a settled frame sends
   only what actually changed. A lone attacher sits at 0,0 and the space is
   exactly its own size, which is today's behaviour exactly; several placed
   side by side is what a multi-monitor arrangement attaches through.
   mbuf/mst are a small state machine of their own: a client's own mouse
   report, in SGR form, has to be rewritten by this client's own offset
   before it reaches the pty, and a report can arrive split across more
   than one read. Shared between srv.c, which owns the list, render.c,
   which owns front and only ever reads one entry at a time, and mouse.c,
   which owns mbuf/mst. */
struct hd_cli {
	int fd, row, col, rows, cols;
	int primed, alt, mmode, msgr, bpaste, cshape;
	struct hd_cell *front;
	str mbuf;
	int mst;
	/* A display's own name, for the control panel: what a client asked to
	   be called at attach, or "client-<fd>" when it asked for nothing.
	   primary marks the one the desktop's own bar and menu anchor to --
	   the first client to attach, until the panel picks another. */
	str name;
	int primary;
};

void hd_cdrop(vec *cls, int fd);
struct hd_cli *hd_cfind(vec *cls, int fd);
struct hd_cli *hd_cfindname(vec *cls, const char *name);
int hd_chas(vec *cls, int fd);
int hd_anyprimary(vec *cls);
void hd_ubox(vec *cls, int *rows, int *cols);
void hd_union(vec *cls, int id, int tid);
void hd_cclear(vec *cls, const char *why, size_t n);
void hd_rensend1(vec *cls, struct hd_cli *cn, int tid);
void hd_rensend(vec *cls, int tid);
void hd_mtrans(struct hd_cli *cn, const char *p, size_t n, str *out);

int hd_dir(str *out);
int hd_path(const char *name, str *out);
int hd_nameok(const char *name);
int hd_wall(int fd, const char *p, size_t n);
int hd_rall(int fd, char *p, size_t n);
int hd_send(int fd, int type, const char *p, size_t n);
int hd_recv(int fd, int *type, str *out);
int hd_dial(const char *path);
int hd_ask(const char *path, int type, str *reply);
int hd_askp(const char *path, int type, const char *p, size_t n, str *reply);
void hd_selftitle(const char *what, const char *path);
void hd_cloexec(int fd);

void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready);
int hd_attach(const char *name, const char *path, int multi, int row,
	      int col, const char *dname);

#endif
