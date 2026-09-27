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
	HD_MATTACH = 'm'
};

extern const py_api *hd_pty;
extern const tm_api *hd_tm;

/* One attached terminal: its connection, where it sits in the session's own
   virtual space -- row,col its top-left corner, rows,cols its own size --
   and the mode state last sent to it, so a real terminal is only ever told
   to switch buffers, mice or paste mode on a genuine change, not every
   frame. A lone attacher sits at 0,0 and the space is exactly its own size,
   which is today's behaviour exactly; several placed side by side is what a
   multi-monitor arrangement attaches through. Shared between srv.c, which
   owns the list, and render.c, which only ever reads one entry at a time. */
struct hd_cli {
	int fd, row, col, rows, cols;
	int primed, alt, mmode, msgr, bpaste, cshape;
};

void hd_cdrop(vec *cls, int fd);
struct hd_cli *hd_cfind(vec *cls, int fd);
int hd_chas(vec *cls, int fd);
void hd_union(vec *cls, int id, int tid);
void hd_cclear(vec *cls, const char *why, size_t n);
void hd_rensend(vec *cls, int tid);

int hd_dir(str *out);
int hd_path(const char *name, str *out);
int hd_nameok(const char *name);
int hd_wall(int fd, const char *p, size_t n);
int hd_rall(int fd, char *p, size_t n);
int hd_send(int fd, int type, const char *p, size_t n);
int hd_recv(int fd, int *type, str *out);
int hd_dial(const char *path);
int hd_ask(const char *path, int type, str *reply);
void hd_selftitle(const char *what, const char *path);
void hd_cloexec(int fd);

void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready);
int hd_attach(const char *name, const char *path, int multi, int row,
	      int col);

#endif
