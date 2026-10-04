#ifndef HL_TR_H
#define HL_TR_H

#include "hl.h"

enum {
	M_INITIAL, M_BHTML, M_BHEAD, M_INHEAD, M_INHEADNS, M_AHEAD, M_INBODY,
	M_TEXT, M_INTABLE, M_INTABLETEXT, M_INCAPTION, M_INCOLGROUP, M_INTBODY,
	M_INROW, M_INCELL, M_INSELECT, M_INSELECTTABLE, M_INTEMPLATE, M_ABODY,
	M_INFRAMESET, M_AFRAMESET, M_AABODY, M_AAFRAMESET
};

extern hl_n tr_marker;
#ifndef MARK
#define MARK (&tr_marker)
#endif

void tr_proc(hl_p *p, hl_tok *t);
void tr_mode(hl_p *p, hl_tok *t);
void tr_body(hl_p *p, hl_tok *t);
void tr_inhead(hl_p *p, hl_tok *t);
void tr_intable(hl_p *p, hl_tok *t);
void tr_intemplate(hl_p *p, hl_tok *t);
void tr_foreign(hl_p *p, hl_tok *t);
int tr_one(const char *w, const char *list);
int tr_tag(hl_tok *t, int kind, const char *list);
hl_n *tr_cur(hl_p *p);
hl_n *tr_acur(hl_p *p);
int tr_isin(hl_n *n, const char *list);
int tr_special(hl_n *n);
int tr_scope(hl_p *p, const char *tag, int kind);
int tr_hastmpl(hl_p *p);
void tr_pop(hl_p *p);
void tr_popto(hl_p *p, const char *tag);
void tr_poptoany(hl_p *p, const char *list);
long tr_onstack(hl_p *p, hl_n *n);
void tr_rmstack(hl_p *p, hl_n *n);
void tr_implied(hl_p *p, const char *except, int thorough);
void tr_text(hl_p *p, const char *s, size_t n);
void tr_comment(hl_p *p, hl_tok *t, hl_n *in);
void tr_adjattrs(hl_n *e);
hl_n *tr_elem(hl_p *p, hl_tok *t, int ns);
hl_n *tr_insert(hl_p *p, hl_tok *t, int ns);
hl_n *tr_insname(hl_p *p, const char *nm);
void tr_merge(hl_n *e, hl_tok *t);
void tr_pushfmt(hl_p *p, hl_n *e);
long tr_infmt(hl_p *p, hl_n *e);
void tr_rmfmt(hl_p *p, hl_n *e);
void tr_clearfmt(hl_p *p);
void tr_reconstruct(hl_p *p);
int tr_adopt(hl_p *p, const char *tag);
void tr_reset(hl_p *p);
int tr_ws(hl_tok *t);
int tr_nul(hl_tok *t);
void tr_closep(hl_p *p);
void tr_rawtext(hl_p *p, hl_tok *t, int state);

#endif
