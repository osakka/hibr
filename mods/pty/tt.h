#ifndef TT_H
#define TT_H

#include "hibr.h"

typedef struct tt_p tt_p;

struct tt_p {
	int id;
	int fd;
	long pid;
	int rows, cols;
	int done;
	int st;
	int eof;
};

tt_p *tt_find(int id);
tt_p *tt_spawn(sh *s, int rows, int cols, char **av);
int tt_resize(tt_p *p, int rows, int cols);
long tt_write(tt_p *p, const char *t, size_t n);
int tt_read(tt_p *p, int ms, str *out);
int tt_alive(tt_p *p);
int tt_wait(tt_p *p, int ms);
void tt_drop(tt_p *p);
void tt_all(void);

#endif
