#ifndef TT_H
#define TT_H

#include "hibr.h"

typedef struct tt_p tt_p;

struct tt_p {
	int id;
	int fd;
	long pid;
	int rows, cols;
	/* The pixel size of the whole terminal, as ws_xpixel/ws_ypixel: 0
	   when nobody has said, which is what a program asking what a cell
	   measures reads as "no pictures here". Kept so an ordinary resize
	   does not throw it away. */
	int xpix, ypix;
	int done;
	int st;
	int eof;
	/* Whose child the program is (ADR 0050, a terminal's program the
	   desktop owns): 0 this process's own; 1 this process's, served for
	   an app's process on channel chan for the app named owner, and lone
	   since that many milliseconds when no channel holds it; 2 another
	   process's -- the desktop's -- reached through this process's own
	   channel, rid being its number there. */
	int far;
	int rid;
	int chan;
	char *owner;
	long lone;
	int eofwait;
};

tt_p *tt_find(int id);
tt_p *tt_spawn(sh *s, int rows, int cols, char **av);
tt_p *tt_spawnx(int rows, int cols, int xpix, int ypix, const char *cwd,
		char **av, char **env);
tt_p *tt_local(sh *s, int rows, int cols, char **av);
int tt_resize(tt_p *p, int rows, int cols);
int tt_resizepx(tt_p *p, int rows, int cols, int xpix, int ypix);
long tt_write(tt_p *p, const char *t, size_t n);
int tt_read(tt_p *p, int ms, str *out);
int tt_alive(tt_p *p);
int tt_wait(tt_p *p, int ms);
void tt_drop(tt_p *p);
tt_p *tt_adopt(int fd, long pid, int rows, int cols);
void tt_release(tt_p *p);
void tt_all(void);
void tt_unlist(tt_p *p);
void tt_free(tt_p *p);

/* far.c: the program's parent elsewhere (ADR 0050). */
int tt_viaset(int fd);
int tt_viafd(void);
int tt_pump(void);
tt_p *tt_farspawn(int rows, int cols, int xpix, int ypix, char **av);
tt_p *tt_faradopt(int want, long pid, int rows, int cols);
int tt_faralive(tt_p *p);
int tt_farsignal(tt_p *p, int sig);
void tt_fardrop(tt_p *p);
int tt_serve(int fd, const char *owner, str *log);
void tt_unserve(int fd);
void tt_unserveall(void);
int tt_reap(str *log);
void tt_orphans(long ms, str *log);
void tt_owned(str *out);
tt_p *tt_adoptown(int fd, long pid, int rows, int cols, const char *owner);
long tt_ms(void);

#endif
