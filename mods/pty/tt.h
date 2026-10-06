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
};

tt_p *tt_find(int id);
tt_p *tt_spawn(sh *s, int rows, int cols, char **av);
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

#endif
