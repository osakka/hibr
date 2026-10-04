#ifndef HL_H
#define HL_H

#include "hibr.h"
#include <stddef.h>

enum { HL_DOC, HL_ELEM, HL_TEXT, HL_COMMENT, HL_DOCTYPE, HL_FRAG };
enum { HL_NSHTML, HL_NSSVG, HL_NSMATH };
enum { HL_TDOCTYPE, HL_TSTART, HL_TEND, HL_TCOMMENT, HL_TCHAR, HL_TEOF };
enum { HL_QNO, HL_QLIMITED, HL_QYES };
enum { HL_SDATA, HL_SRCDATA, HL_SRAWTEXT, HL_SSCRIPT, HL_SPLAIN };
enum { HL_FSPECIAL = 1, HL_FSCOPE = 2, HL_FTSCOPE = 4, HL_FLIST = 8, HL_FBUTTON = 16 };

#ifndef HL_DEPTH
#define HL_DEPTH 512
#endif

typedef struct hl_ent hl_ent;
typedef struct hl_attr hl_attr;
typedef struct hl_n hl_n;
typedef struct hl_tok hl_tok;
typedef struct hl_doc hl_doc;
typedef struct hl_p hl_p;

struct hl_ent {
	const char *nm, *ch;
	int legacy;
};

struct hl_attr {
	char *nm, *val, *pfx;
	int ns;
};

struct hl_n {
	int t, ns, id, fl;
	char *tag;
	str s;
	vec attrs;
	hl_n *up, *kid, *last, *nx, *pv;
	hl_n *tmpl;
	char *pub, *sys;
	int haspub, hassys;
};

struct hl_tok {
	int t, self, quirks, ackself;
	str nm, data, pub, sys;
	int haspub, hassys, hasnm;
	vec attrs;
};

struct hl_doc {
	hl_n *root;
	vec nodes;
	int quirks;
};

struct hl_p {
	const char *src;
	size_t n, i;
	int state, rstate;
	str buf, tmp, lastsg;
	hl_tok tok;
	str chars;
	int charws;
	hl_doc *doc;
	int mode, orig;
	vec open, fmt, tmodes;
	str pendtab_s;
	int pendnonws;
	hl_n *head, *form, *ctx;
	int framesetok, fosterok, scripting, frag, quirks;
	int pendws;
	int depth;
	int ignlf;
};

extern const hl_ent hl_ents[];
extern const size_t hl_nents;

hl_n *hl_new(hl_doc *d, int t);
void hl_append(hl_n *up, hl_n *n);
void hl_insbefore(hl_n *up, hl_n *n, hl_n *ref);
void hl_unlink(hl_n *n);
void hl_setattr(hl_n *e, const char *nm, const char *val);
const char *hl_attr_get(hl_n *e, const char *nm);
void hl_docfree(hl_doc *d);
void hl_dump(hl_n *n, int dep, str *o);
int hl_isel(hl_n *n, const char *tag);
int hl_isnsel(hl_n *n, int ns, const char *tag);
hl_n *hl_clone(hl_doc *d, hl_n *n);
hl_n *hl_findel(hl_n *n, const char *tag);
void hl_selected(hl_doc *d, hl_n *n);
void hl_selopt(hl_n *n, hl_n **first, hl_n **sel);
int hl_query(hl_n *root, const char *sel, vec *out);
int hl_lines(sh *s, hl_n *root, int width, int flags);

hl_doc *hl_parse(const char *src, size_t n, int scripting);
hl_doc *hl_parsefrag(const char *src, size_t n, const char *ctx, int ctxns,
		     int scripting);

void hl_toknext(hl_p *p);
void hl_tokinit(hl_p *p, const char *src, size_t n);
void hl_tokfree(hl_p *p);
void hl_emit(hl_p *p, hl_tok *t);
int hl_foreign(hl_p *p);

int hl_utf8(const char *s, size_t n, unsigned *cp);
void hl_putcp(str *o, unsigned cp);
const hl_ent *hl_entfind(const char *nm, size_t len);

#endif
