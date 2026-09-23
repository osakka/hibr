#ifndef HD_H
#define HD_H

#include "hibr.h"
#include "../pty.h"

enum {
	HD_ATTACH = 'a',
	HD_DATA = 'd',
	HD_SIZE = 'w',
	HD_DETACH = 'q',
	HD_EXIT = 'x',
	HD_KILL = 'k',
	HD_INFO = 'i'
};

extern const py_api *hd_pty;

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

void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready);
int hd_attach(const char *name, const char *path);

#endif
