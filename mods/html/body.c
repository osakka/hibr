#define _GNU_SOURCE

#include "tr.h"
#include <stdlib.h>
#include <string.h>

/* A start tag token made from just a name, for the rules that act as if
   one had been seen. */
void bd_fake(hl_tok *t, int kind, const char *nm)
{
	memset(t, 0, sizeof *t);
	t->t = kind;
	s_init(&t->nm);
	s_cat(&t->nm, nm);
}

/* Any other end tag in body. */
void bd_endother(hl_p *p, hl_tok *t)
{
	long i;
	hl_n *n;
	const char *nm = t->nm.p ? t->nm.p : "";

	for (i = (long)p->open.n - 1; i >= 0; i--) {
		n = p->open.p[i];
		if (hl_isel(n, nm)) {
			tr_implied(p, nm, 0);
			while (p->open.n > (size_t)i)
				tr_pop(p);
			return;
		}
		if (tr_special(n))
			return;
	}
}

/* The list-item rules shared by li, dd and dt start tags. */
void bd_item(hl_p *p, hl_tok *t, const char *same)
{
	long i;
	hl_n *n;

	p->framesetok = 0;
	for (i = (long)p->open.n - 1; i >= 0; i--) {
		n = p->open.p[i];
		if (tr_isin(n, same)) {
			tr_implied(p, n->tag, 0);
			tr_popto(p, n->tag);
			break;
		}
		if (tr_special(n) && !tr_isin(n, "address div p"))
			break;
	}
	if (tr_scope(p, "p", 'b'))
		tr_closep(p);
	tr_insert(p, t, HL_NSHTML);
}

/* Whether this is a fragment parsed in a select: then select and input
   start tags are ignored. */
int bd_selctx(hl_p *p)
{
	return p->frag && p->ctx && hl_isel(p->ctx, "select");
}

/* An element in a foreign namespace, inserted from a start tag. */
void bd_foreignel(hl_p *p, hl_tok *t, int ns)
{
	tr_reconstruct(p);
	tr_insert(p, t, ns);
	if (t->self)
		tr_pop(p);
}

/* The in body insertion mode. */
void tr_body(hl_p *p, hl_tok *t)
{
	const char *nm = t->nm.p ? t->nm.p : "";
	hl_n *n;
	long i;

	switch (t->t) {
	case HL_TCHAR:
		if (tr_nul(t))
			return;
		tr_reconstruct(p);
		tr_text(p, t->data.p, t->data.n);
		if (!tr_ws(t))
			p->framesetok = 0;
		return;
	case HL_TCOMMENT:
		tr_comment(p, t, 0);
		return;
	case HL_TDOCTYPE:
		return;
	case HL_TEOF:
		if (p->tmodes.n) {
			tr_intemplate(p, t);
			return;
		}
		return;
	}
	if (t->t == HL_TSTART) {
		if (!strcmp(nm, "html")) {
			if (!tr_hastmpl(p) && p->open.n)
				tr_merge(p->open.p[0], t);
			return;
		}
		if (tr_one(nm, "base basefont bgsound link meta noframes script style template title")) {
			tr_inhead(p, t);
			return;
		}
		if (!strcmp(nm, "body")) {
			if (p->open.n < 2 || !hl_isel(p->open.p[1], "body") || tr_hastmpl(p))
				return;
			p->framesetok = 0;
			tr_merge(p->open.p[1], t);
			return;
		}
		if (!strcmp(nm, "frameset")) {
			if (p->open.n < 2 || !hl_isel(p->open.p[1], "body") || !p->framesetok)
				return;
			hl_unlink(p->open.p[1]);
			p->open.n = 1;
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INFRAMESET;
			return;
		}
		if (tr_one(nm, "address article aside blockquote center details dialog dir div dl "
			   "fieldset figcaption figure footer header hgroup main menu nav ol p search "
			   "section summary ul")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			tr_insert(p, t, HL_NSHTML);
			return;
		}
		if (tr_one(nm, "h1 h2 h3 h4 h5 h6")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			if (tr_isin(tr_cur(p), "h1 h2 h3 h4 h5 h6"))
				tr_pop(p);
			tr_insert(p, t, HL_NSHTML);
			return;
		}
		if (tr_one(nm, "pre listing")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			tr_insert(p, t, HL_NSHTML);
			p->ignlf = 1;
			p->framesetok = 0;
			return;
		}
		if (!strcmp(nm, "form")) {
			if (p->form && !tr_hastmpl(p))
				return;
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			n = tr_insert(p, t, HL_NSHTML);
			if (!tr_hastmpl(p))
				p->form = n;
			return;
		}
		if (!strcmp(nm, "li")) {
			bd_item(p, t, "li");
			return;
		}
		if (tr_one(nm, "dd dt")) {
			bd_item(p, t, "dd dt");
			return;
		}
		if (!strcmp(nm, "plaintext")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			tr_insert(p, t, HL_NSHTML);
			p->state = HL_SPLAIN;
			return;
		}
		if (!strcmp(nm, "button")) {
			if (tr_scope(p, "button", 'd')) {
				tr_implied(p, 0, 0);
				tr_popto(p, "button");
			}
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			p->framesetok = 0;
			return;
		}
		if (!strcmp(nm, "a")) {
			for (i = (long)p->fmt.n - 1; i >= 0; i--) {
				n = p->fmt.p[i];
				if (n == MARK)
					break;
				if (hl_isel(n, "a")) {
					if (!tr_adopt(p, "a"))
						bd_endother(p, t);
					tr_rmfmt(p, n);
					tr_rmstack(p, n);
					break;
				}
			}
			tr_reconstruct(p);
			tr_pushfmt(p, tr_insert(p, t, HL_NSHTML));
			return;
		}
		if (tr_one(nm, "b big code em font i s small strike strong tt u")) {
			tr_reconstruct(p);
			tr_pushfmt(p, tr_insert(p, t, HL_NSHTML));
			return;
		}
		if (!strcmp(nm, "nobr")) {
			tr_reconstruct(p);
			if (tr_scope(p, "nobr", 'd')) {
				if (!tr_adopt(p, "nobr"))
					bd_endother(p, t);
				tr_reconstruct(p);
			}
			tr_pushfmt(p, tr_insert(p, t, HL_NSHTML));
			return;
		}
		if (tr_one(nm, "applet marquee object")) {
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			v_add(&p->fmt, MARK);
			p->framesetok = 0;
			return;
		}
		if (!strcmp(nm, "table")) {
			if (p->doc->quirks != HL_QYES && tr_scope(p, "p", 'b'))
				tr_closep(p);
			tr_insert(p, t, HL_NSHTML);
			p->framesetok = 0;
			p->mode = M_INTABLE;
			return;
		}
		if (tr_one(nm, "area br embed img keygen wbr")) {
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			p->framesetok = 0;
			return;
		}
		if (!strcmp(nm, "input")) {
			const char *ty;
			size_t k;

			if (bd_selctx(p))
				return;
			if (tr_scope(p, "select", 'd'))
				tr_popto(p, "select");
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			ty = 0;
			for (k = 0; k < t->attrs.n; k++) {
				hl_attr *a = t->attrs.p[k];

				if (!strcmp(a->nm, "type"))
					ty = a->val;
			}
			if (!ty || strcasecmp(ty, "hidden"))
				p->framesetok = 0;
			return;
		}
		if (tr_one(nm, "param source track")) {
			tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			return;
		}
		if (!strcmp(nm, "hr")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			if (tr_scope(p, "select", 'd'))
				tr_implied(p, 0, 0);
			tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			p->framesetok = 0;
			return;
		}
		if (!strcmp(nm, "image")) {
			t->nm.n = 0;
			s_cat(&t->nm, "img");
			tr_proc(p, t);
			return;
		}
		if (!strcmp(nm, "textarea")) {
			tr_insert(p, t, HL_NSHTML);
			p->ignlf = 1;
			p->state = HL_SRCDATA;
			p->orig = p->mode;
			p->framesetok = 0;
			p->mode = M_TEXT;
			return;
		}
		if (!strcmp(nm, "xmp")) {
			if (tr_scope(p, "p", 'b'))
				tr_closep(p);
			tr_reconstruct(p);
			p->framesetok = 0;
			tr_rawtext(p, t, HL_SRAWTEXT);
			return;
		}
		if (!strcmp(nm, "iframe")) {
			p->framesetok = 0;
			tr_rawtext(p, t, HL_SRAWTEXT);
			return;
		}
		if (!strcmp(nm, "noembed") || (!strcmp(nm, "noscript") && p->scripting)) {
			tr_rawtext(p, t, HL_SRAWTEXT);
			return;
		}
		if (!strcmp(nm, "select")) {
			if (bd_selctx(p))
				return;
			if (tr_scope(p, "select", 'd')) {
				tr_popto(p, "select");
				return;
			}
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			p->framesetok = 0;
			return;
		}
		if (tr_one(nm, "optgroup option")) {
			if (tr_scope(p, "select", 'd'))
				tr_implied(p, !strcmp(nm, "option") ? "optgroup" : 0, 0);
			else if (hl_isel(tr_cur(p), "option"))
				tr_pop(p);
			tr_reconstruct(p);
			tr_insert(p, t, HL_NSHTML);
			return;
		}
		if (tr_one(nm, "rb rtc")) {
			if (tr_scope(p, "ruby", 'd'))
				tr_implied(p, 0, 0);
			tr_insert(p, t, HL_NSHTML);
			return;
		}
		if (tr_one(nm, "rp rt")) {
			if (tr_scope(p, "ruby", 'd'))
				tr_implied(p, "rtc", 0);
			tr_insert(p, t, HL_NSHTML);
			return;
		}
		if (!strcmp(nm, "math")) {
			bd_foreignel(p, t, HL_NSMATH);
			return;
		}
		if (!strcmp(nm, "svg")) {
			bd_foreignel(p, t, HL_NSSVG);
			return;
		}
		if (tr_one(nm, "caption col colgroup frame head tbody td tfoot th thead tr"))
			return;
		tr_reconstruct(p);
		tr_insert(p, t, HL_NSHTML);
		return;
	}
	if (!strcmp(nm, "template")) {
		tr_inhead(p, t);
		return;
	}
	if (!strcmp(nm, "body") || !strcmp(nm, "html")) {
		if (!tr_scope(p, "body", 'd'))
			return;
		p->mode = M_ABODY;
		if (!strcmp(nm, "html"))
			tr_proc(p, t);
		return;
	}
	if (tr_one(nm, "address article aside blockquote button center details dialog dir div dl "
		   "fieldset figcaption figure footer header hgroup listing main menu nav ol pre "
		   "search section select summary ul")) {
		if (!tr_scope(p, nm, 'd'))
			return;
		tr_implied(p, 0, 0);
		tr_popto(p, nm);
		return;
	}
	if (!strcmp(nm, "form")) {
		if (!tr_hastmpl(p)) {
			n = p->form;
			p->form = 0;
			if (!n || tr_onstack(p, n) < 0 || !tr_scope(p, "form", 'd'))
				return;
			tr_implied(p, 0, 0);
			tr_rmstack(p, n);
			return;
		}
		if (!tr_scope(p, "form", 'd'))
			return;
		tr_implied(p, 0, 0);
		tr_popto(p, "form");
		return;
	}
	if (!strcmp(nm, "p")) {
		if (!tr_scope(p, "p", 'b'))
			tr_insname(p, "p");
		tr_closep(p);
		return;
	}
	if (!strcmp(nm, "li")) {
		if (!tr_scope(p, "li", 'l'))
			return;
		tr_implied(p, "li", 0);
		tr_popto(p, "li");
		return;
	}
	if (tr_one(nm, "dd dt")) {
		if (!tr_scope(p, nm, 'd'))
			return;
		tr_implied(p, nm, 0);
		tr_popto(p, nm);
		return;
	}
	if (tr_one(nm, "h1 h2 h3 h4 h5 h6")) {
		if (!tr_scope(p, "h1 h2 h3 h4 h5 h6", 'd'))
			return;
		tr_implied(p, 0, 0);
		tr_poptoany(p, "h1 h2 h3 h4 h5 h6");
		return;
	}
	if (tr_one(nm, "a b big code em font i nobr s small strike strong tt u")) {
		if (!tr_adopt(p, nm))
			bd_endother(p, t);
		return;
	}
	if (tr_one(nm, "applet marquee object")) {
		if (!tr_scope(p, nm, 'd'))
			return;
		tr_implied(p, 0, 0);
		tr_popto(p, nm);
		tr_clearfmt(p);
		return;
	}
	if (!strcmp(nm, "br")) {
		hl_tok u;

		bd_fake(&u, HL_TSTART, "br");
		tr_reconstruct(p);
		tr_insert(p, &u, HL_NSHTML);
		tr_pop(p);
		p->framesetok = 0;
		s_free(&u.nm);
		return;
	}
	bd_endother(p, t);
}

/* Pop back to a table context: table, template or html. */
void bd_clearto(hl_p *p, const char *list)
{
	while (p->open.n && !tr_isin(tr_cur(p), list))
		tr_pop(p);
}

/* Close a table cell. */
void bd_closecell(hl_p *p)
{
	tr_implied(p, 0, 0);
	tr_poptoany(p, "td th");
	tr_clearfmt(p);
	p->mode = M_INROW;
}

/* Anything else in table: in body, with foster parenting on. */
void bd_tableelse(hl_p *p, hl_tok *t)
{
	p->fosterok = 1;
	tr_body(p, t);
	p->fosterok = 0;
}

/* Insert a name's element then reprocess the token in a new mode. */
void bd_insre(hl_p *p, hl_tok *t, const char *nm, int mode)
{
	tr_insname(p, nm);
	p->mode = mode;
	tr_proc(p, t);
}

/* The pending table character tokens, flushed at the end of table text. */
void bd_flushtext(hl_p *p)
{
	hl_tok u;

	if (!p->pendtab_s.n)
		return;
	if (p->pendnonws) {
		memset(&u, 0, sizeof u);
		u.t = HL_TCHAR;
		u.data = p->pendtab_s;
		p->fosterok = 1;
		tr_reconstruct(p);
		tr_text(p, u.data.p, u.data.n);
		p->framesetok = 0;
		p->fosterok = 0;
	} else {
		tr_text(p, p->pendtab_s.p, p->pendtab_s.n);
	}
	p->pendtab_s.n = 0;
	if (p->pendtab_s.p)
		p->pendtab_s.p[0] = 0;
	p->pendnonws = 0;
}

/* The in table insertion mode. */
void bd_table(hl_p *p, hl_tok *t)
{
	const char *nm = t->nm.p ? t->nm.p : "";

	if (t->t == HL_TCHAR && tr_isin(tr_cur(p), "table tbody template tfoot thead tr")) {
		p->pendtab_s.n = 0;
		p->pendnonws = 0;
		p->orig = p->mode;
		p->mode = M_INTABLETEXT;
		tr_proc(p, t);
		return;
	}
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, 0);
		return;
	}
	if (t->t == HL_TDOCTYPE)
		return;
	if (t->t == HL_TSTART) {
		if (!strcmp(nm, "caption")) {
			bd_clearto(p, "table template html");
			v_add(&p->fmt, MARK);
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INCAPTION;
			return;
		}
		if (!strcmp(nm, "colgroup")) {
			bd_clearto(p, "table template html");
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INCOLGROUP;
			return;
		}
		if (!strcmp(nm, "col")) {
			bd_clearto(p, "table template html");
			bd_insre(p, t, "colgroup", M_INCOLGROUP);
			return;
		}
		if (tr_one(nm, "tbody tfoot thead")) {
			bd_clearto(p, "table template html");
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INTBODY;
			return;
		}
		if (tr_one(nm, "td th tr")) {
			bd_clearto(p, "table template html");
			bd_insre(p, t, "tbody", M_INTBODY);
			return;
		}
		if (!strcmp(nm, "table")) {
			if (!tr_scope(p, "table", 't'))
				return;
			tr_popto(p, "table");
			tr_reset(p);
			tr_proc(p, t);
			return;
		}
		if (tr_one(nm, "style script template")) {
			tr_inhead(p, t);
			return;
		}
		if (!strcmp(nm, "input")) {
			const char *ty = 0;
			size_t k;

			for (k = 0; k < t->attrs.n; k++) {
				hl_attr *a = t->attrs.p[k];

				if (!strcmp(a->nm, "type"))
					ty = a->val;
			}
			if (ty && !strcasecmp(ty, "hidden")) {
				tr_insert(p, t, HL_NSHTML);
				tr_pop(p);
				return;
			}
		}
		if (!strcmp(nm, "form")) {
			if (tr_hastmpl(p) || p->form)
				return;
			p->form = tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			return;
		}
	}
	if (t->t == HL_TEND) {
		if (!strcmp(nm, "table")) {
			if (!tr_scope(p, "table", 't'))
				return;
			tr_popto(p, "table");
			tr_reset(p);
			return;
		}
		if (tr_one(nm, "body caption col colgroup html tbody td tfoot th thead tr"))
			return;
		if (!strcmp(nm, "template")) {
			tr_inhead(p, t);
			return;
		}
	}
	if (t->t == HL_TEOF) {
		tr_body(p, t);
		return;
	}
	bd_tableelse(p, t);
}

/* The table modes, each by the spec's own rules. */
void tr_intable(hl_p *p, hl_tok *t)
{
	const char *nm = t->nm.p ? t->nm.p : "";

	switch (p->mode) {
	case M_INTABLE:
		bd_table(p, t);
		return;
	case M_INTABLETEXT:
		if (t->t == HL_TCHAR) {
			if (tr_nul(t))
				return;
			s_add(&p->pendtab_s, t->data.p, t->data.n);
			if (!tr_ws(t))
				p->pendnonws = 1;
			return;
		}
		bd_flushtext(p);
		p->mode = p->orig;
		tr_proc(p, t);
		return;
	case M_INCAPTION:
		if (tr_tag(t, HL_TEND, "caption") ||
		    tr_tag(t, HL_TSTART, "caption col colgroup tbody td tfoot th thead tr") ||
		    tr_tag(t, HL_TEND, "table")) {
			if (!tr_scope(p, "caption", 't'))
				return;
			tr_implied(p, 0, 0);
			tr_popto(p, "caption");
			tr_clearfmt(p);
			p->mode = M_INTABLE;
			if (!tr_tag(t, HL_TEND, "caption"))
				tr_proc(p, t);
			return;
		}
		if (tr_tag(t, HL_TEND, "body col colgroup html tbody td tfoot th thead tr"))
			return;
		tr_body(p, t);
		return;
	case M_INCOLGROUP:
		if (tr_ws(t)) {
			tr_text(p, t->data.p, t->data.n);
			return;
		}
		if (t->t == HL_TCOMMENT) {
			tr_comment(p, t, 0);
			return;
		}
		if (t->t == HL_TDOCTYPE)
			return;
		if (tr_tag(t, HL_TSTART, "html")) {
			tr_body(p, t);
			return;
		}
		if (tr_tag(t, HL_TSTART, "col")) {
			tr_insert(p, t, HL_NSHTML);
			tr_pop(p);
			return;
		}
		if (tr_tag(t, HL_TEND, "colgroup")) {
			if (!hl_isel(tr_cur(p), "colgroup"))
				return;
			tr_pop(p);
			p->mode = M_INTABLE;
			return;
		}
		if (tr_tag(t, HL_TEND, "col"))
			return;
		if (tr_tag(t, HL_TSTART, "template") || tr_tag(t, HL_TEND, "template")) {
			tr_inhead(p, t);
			return;
		}
		if (t->t == HL_TEOF) {
			tr_body(p, t);
			return;
		}
		if (!hl_isel(tr_cur(p), "colgroup"))
			return;
		tr_pop(p);
		p->mode = M_INTABLE;
		tr_proc(p, t);
		return;
	case M_INTBODY:
		if (tr_tag(t, HL_TSTART, "tr")) {
			bd_clearto(p, "tbody tfoot thead template html");
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INROW;
			return;
		}
		if (tr_tag(t, HL_TSTART, "th td")) {
			bd_clearto(p, "tbody tfoot thead template html");
			bd_insre(p, t, "tr", M_INROW);
			return;
		}
		if (tr_tag(t, HL_TEND, "tbody tfoot thead")) {
			if (!tr_scope(p, nm, 't'))
				return;
			bd_clearto(p, "tbody tfoot thead template html");
			tr_pop(p);
			p->mode = M_INTABLE;
			return;
		}
		if (tr_tag(t, HL_TSTART, "caption col colgroup tbody tfoot thead") ||
		    tr_tag(t, HL_TEND, "table")) {
			if (!tr_scope(p, "tbody thead tfoot", 't'))
				return;
			bd_clearto(p, "tbody tfoot thead template html");
			tr_pop(p);
			p->mode = M_INTABLE;
			tr_proc(p, t);
			return;
		}
		if (tr_tag(t, HL_TEND, "body caption col colgroup html td th tr"))
			return;
		bd_table(p, t);
		return;
	case M_INROW:
		if (tr_tag(t, HL_TSTART, "th td")) {
			bd_clearto(p, "tr template html");
			tr_insert(p, t, HL_NSHTML);
			p->mode = M_INCELL;
			v_add(&p->fmt, MARK);
			return;
		}
		if (tr_tag(t, HL_TEND, "tr")) {
			if (!tr_scope(p, "tr", 't'))
				return;
			bd_clearto(p, "tr template html");
			tr_pop(p);
			p->mode = M_INTBODY;
			return;
		}
		if (tr_tag(t, HL_TSTART, "caption col colgroup tbody tfoot thead tr") ||
		    tr_tag(t, HL_TEND, "table")) {
			if (!tr_scope(p, "tr", 't'))
				return;
			bd_clearto(p, "tr template html");
			tr_pop(p);
			p->mode = M_INTBODY;
			tr_proc(p, t);
			return;
		}
		if (tr_tag(t, HL_TEND, "tbody tfoot thead")) {
			if (!tr_scope(p, nm, 't') || !tr_scope(p, "tr", 't'))
				return;
			bd_clearto(p, "tr template html");
			tr_pop(p);
			p->mode = M_INTBODY;
			tr_proc(p, t);
			return;
		}
		if (tr_tag(t, HL_TEND, "body caption col colgroup html td th"))
			return;
		bd_table(p, t);
		return;
	case M_INCELL:
		if (tr_tag(t, HL_TEND, "td th")) {
			if (!tr_scope(p, nm, 't'))
				return;
			tr_implied(p, 0, 0);
			tr_popto(p, nm);
			tr_clearfmt(p);
			p->mode = M_INROW;
			return;
		}
		if (tr_tag(t, HL_TSTART, "caption col colgroup tbody td tfoot th thead tr")) {
			if (!tr_scope(p, "td th", 't'))
				return;
			bd_closecell(p);
			tr_proc(p, t);
			return;
		}
		if (tr_tag(t, HL_TEND, "body caption col colgroup html"))
			return;
		if (tr_tag(t, HL_TEND, "table tbody tfoot thead tr")) {
			if (!tr_scope(p, nm, 't'))
				return;
			bd_closecell(p);
			tr_proc(p, t);
			return;
		}
		tr_body(p, t);
		return;
	}
}

/* Switch the template's insertion mode and reprocess. */
void bd_tmode(hl_p *p, hl_tok *t, int mode)
{
	if (p->tmodes.n)
		p->tmodes.n--;
	v_add(&p->tmodes, (void *)(long)mode);
	p->mode = mode;
	tr_proc(p, t);
}

/* The in template insertion mode. */
void tr_intemplate(hl_p *p, hl_tok *t)
{
	if (t->t == HL_TCHAR || t->t == HL_TCOMMENT || t->t == HL_TDOCTYPE) {
		tr_body(p, t);
		return;
	}
	if (tr_tag(t, HL_TSTART, "base basefont bgsound link meta noframes script style template title") ||
	    tr_tag(t, HL_TEND, "template")) {
		tr_inhead(p, t);
		return;
	}
	if (tr_tag(t, HL_TSTART, "caption colgroup tbody tfoot thead")) {
		bd_tmode(p, t, M_INTABLE);
		return;
	}
	if (tr_tag(t, HL_TSTART, "col")) {
		bd_tmode(p, t, M_INCOLGROUP);
		return;
	}
	if (tr_tag(t, HL_TSTART, "tr")) {
		bd_tmode(p, t, M_INTBODY);
		return;
	}
	if (tr_tag(t, HL_TSTART, "td th")) {
		bd_tmode(p, t, M_INROW);
		return;
	}
	if (t->t == HL_TSTART) {
		bd_tmode(p, t, M_INBODY);
		return;
	}
	if (t->t == HL_TEND)
		return;
	if (!tr_hastmpl(p))
		return;
	tr_popto(p, "template");
	tr_clearfmt(p);
	if (p->tmodes.n)
		p->tmodes.n--;
	tr_reset(p);
	tr_proc(p, t);
}

/* Whether a node is a MathML text integration point or an HTML
   integration point. */
int bd_integ(hl_n *n)
{
	const char *enc;

	if (n->ns == HL_NSMATH && tr_one(n->tag, "mi mo mn ms mtext"))
		return 1;
	if (n->ns == HL_NSSVG && tr_one(n->tag, "foreignObject desc title"))
		return 1;
	if (n->ns == HL_NSMATH && !strcmp(n->tag, "annotation-xml") &&
	    (enc = hl_attr_get(n, "encoding")) &&
	    (!strcasecmp(enc, "text/html") || !strcasecmp(enc, "application/xhtml+xml")))
		return 1;
	return 0;
}

/* The rules for parsing tokens in foreign content: SVG and MathML. */
void tr_foreign(hl_p *p, hl_tok *t)
{
	const char *nm = t->nm.p ? t->nm.p : "";
	hl_n *a, *n;
	long i;

	if (t->t == HL_TCHAR) {
		if (tr_nul(t)) {
			tr_text(p, "\xef\xbf\xbd", 3);
			return;
		}
		tr_text(p, t->data.p, t->data.n);
		if (!tr_ws(t))
			p->framesetok = 0;
		return;
	}
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, 0);
		return;
	}
	if (t->t == HL_TDOCTYPE)
		return;
	if (t->t == HL_TSTART) {
		int brk = tr_one(nm, "b big blockquote body br center code dd div dl dt em embed h1 h2 "
				 "h3 h4 h5 h6 head hr i img li listing menu meta nobr ol p pre ruby s "
				 "small span strong strike sub sup table tt u ul var");
		size_t k;

		if (!brk && !strcmp(nm, "font"))
			for (k = 0; k < t->attrs.n; k++) {
				hl_attr *x = t->attrs.p[k];

				if (tr_one(x->nm, "color face size"))
					brk = 1;
			}
		if (brk) {
			while (p->open.n > 1) {
				n = tr_cur(p);
				if (n->ns == HL_NSHTML || bd_integ(n))
					break;
				tr_pop(p);
			}
			tr_mode(p, t);
			return;
		}
		a = tr_acur(p);
		tr_insert(p, t, a->ns);
		if (t->self)
			tr_pop(p);
		return;
	}
	if (tr_tag(t, HL_TEND, "br p")) {
		while (p->open.n > 1) {
			n = tr_cur(p);
			if (n->ns == HL_NSHTML || bd_integ(n))
				break;
			tr_pop(p);
		}
		tr_mode(p, t);
		return;
	}
	if (t->t == HL_TEND) {
		for (i = (long)p->open.n - 1; i >= 0; i--) {
			n = p->open.p[i];
			if (i == 0)
				return;
			if (!strcasecmp(n->tag, nm)) {
				while (p->open.n > (size_t)i)
					tr_pop(p);
				return;
			}
			if (i > 0 && ((hl_n *)p->open.p[i - 1])->ns == HL_NSHTML) {
				tr_mode(p, t);
				return;
			}
		}
	}
}
