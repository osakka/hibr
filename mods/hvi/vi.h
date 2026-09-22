#ifndef VI_H
#define VI_H

#include "hibr.h"

/* A gap buffer: the text is contiguous except for a hole at the cursor, so
   typing costs nothing to move and the whole file is one allocation. Every
   position taken or returned here is a logical one, with the gap not counted. */
typedef struct vi_buf vi_buf;
struct vi_buf {
	char *d;
	size_t cap;
	size_t gp;
	size_t gn;
	vec ls;
	size_t dirty;
	int mod;
};

#ifndef VI_GAP
#define VI_GAP 4096
#endif

#ifndef VI_MODES
#define VI_NORMAL 0
#define VI_INSERT 1
#define VI_VISUAL 2
#define VI_VLINE 3
#define VI_EX 4
#define VI_EINS 0
#define VI_EDEL 1
#define VI_MODES 1
#endif

typedef struct vi_edit vi_edit;
struct vi_edit {
	int kind;
	size_t pos;
	char *t;
	size_t n;
	size_t cur;
	int group;
};

typedef struct vi_ed vi_ed;
struct vi_ed {
	vi_buf b;
	char *path;
	size_t cur, top, vstart;
	int wantcol, mode, group, quit, nored, yline, hoff, hlstate;
	vec undo, redo;
	str find, cmd, msg, yank, pend;
	long mtim, mtin;
	size_t fsize;
	int ondisk;
};

void vi_urec(vi_ed *e, int kind, size_t pos, const char *t, size_t n);
void vi_eins(vi_ed *e, size_t pos, const char *t, size_t n);
void vi_edel(vi_ed *e, size_t pos, size_t n);
int vi_undo1(vi_ed *e);
int vi_redo1(vi_ed *e);
void vi_ugroup(vi_ed *e);
void vi_ufree(vi_ed *e);
int vi_stamp(vi_ed *e);
int vi_changed(vi_ed *e);

void vi_binit(vi_buf *b);
void vi_bfree(vi_buf *b);
size_t vi_len(const vi_buf *b);
int vi_at(const vi_buf *b, size_t pos);
void vi_get(const vi_buf *b, size_t pos, size_t n, str *out);
void vi_ins(vi_buf *b, size_t pos, const char *t, size_t n);
void vi_del(vi_buf *b, size_t pos, size_t n);
int vi_load(vi_buf *b, const char *path);
int vi_save(const vi_buf *b, const char *path);

size_t vi_nlines(vi_buf *b);
size_t vi_lstart(vi_buf *b, size_t i);
size_t vi_lend(vi_buf *b, size_t i);
size_t vi_lineof(vi_buf *b, size_t pos);

size_t vi_next(const vi_buf *b, size_t pos);
size_t vi_prev(const vi_buf *b, size_t pos);

#endif
