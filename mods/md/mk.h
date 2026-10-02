#ifndef MK_H
#define MK_H

#include "hibr.h"
#include <stddef.h>

enum {
	MK_DOC, MK_QUOTE, MK_LIST, MK_ITEM, MK_PARA, MK_HEAD, MK_HR, MK_CODE,
	MK_HTML, MK_TABLE, MK_TROW, MK_TCELL,
	MK_TEXT, MK_SOFT, MK_HARD, MK_CSPAN, MK_EMPH, MK_STRONG, MK_DEL,
	MK_LINK, MK_IMG, MK_RAW
};

#ifndef MK_DEPTH
#define MK_DEPTH 500
#endif

typedef struct mk_n mk_n;
typedef struct mk_ent mk_ent;
typedef struct mk_ref mk_ref;
typedef struct mk_p mk_p;

struct mk_ent {
	const char *nm, *ch;
};

struct mk_ref {
	char *lab, *url, *title;
};

struct mk_n {
	int t;
	mk_n *up, *kid, *last, *nx, *pv;
	str s;
	long *off;
	size_t offcap;
	long a, b;
	int open, lb, sl, el;
	int lvl, fence, fch, flen, foff;
	str info, url, title;
	int lt, bch, dlm, start, tight, moff, pad;
	int task;
	long taskat;
	int align, head, setext;
	long ca, cb;
	int dep;
};

struct mk_p {
	mk_n *root, *cur;
	const char *src;
	size_t srcn;
	const char *ln;
	size_t lnn;
	long lnoff;
	int lineno;
	size_t off, fns;
	int col, fnscol, indent, blank, ptab;
	vec refs;
	int gfm;
};

extern const mk_ent mk_ents[];
extern const size_t mk_nents;

mk_n *mk_new(int t);
void mk_free(mk_n *n);
void mk_append(mk_n *up, mk_n *n);
void mk_unlink(mk_n *n);
void mk_insafter(mk_n *at, mk_n *n);
void mk_addb(mk_n *n, const char *p, size_t len, long at);
mk_n *mk_parse(const char *src, size_t n, int gfm, vec *refs);
void mk_inlines(mk_n *leaf, vec *refs, int gfm);
void mk_html(mk_n *n, str *o, int gfm);
void mk_styles(mk_n *doc, const char *src, size_t n, char *sty);

int mk_ispunct(unsigned c);
int mk_isspace(unsigned c);
unsigned mk_utf8(const char *p, size_t n, int *len);
unsigned mk_before(const char *s, size_t i);
void mk_putu(str *o, unsigned c);
const char *mk_entity(const char *nm, size_t n);
void mk_fold(str *o, const char *p, size_t n);
void mk_esc(str *o, const char *p, size_t n);
void mk_eschref(str *o, const char *p, size_t n);
void mk_unesc(str *o, const char *p, size_t n);
size_t mk_entref(const char *p, size_t n, str *o);
size_t mk_htmltag(const char *p, size_t n);
size_t mk_refdef(const char *p, size_t n, vec *refs, mk_n *leaf, long base);
mk_ref *mk_findref(vec *refs, const char *lab, size_t n);
size_t mk_linkdest(const char *p, size_t n, str *url);
size_t mk_linktitle(const char *p, size_t n, str *title);
size_t mk_label(const char *p, size_t n);

#endif
