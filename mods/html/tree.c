#define _GNU_SOURCE

#include "tr.h"
#include <stdlib.h>
#include <string.h>

hl_n tr_marker;

/* Whether a word is in a space-separated list. */
int tr_one(const char *w, const char *list)
{
	size_t n = strlen(w);
	const char *s = list, *e;

	while (*s) {
		e = strchr(s, ' ');
		if (!e)
			e = s + strlen(s);
		if ((size_t)(e - s) == n && !strncmp(s, w, n))
			return 1;
		s = *e ? e + 1 : e;
	}
	return 0;
}

/* Whether a token is a start or end tag with a name in a list. */
int tr_tag(hl_tok *t, int kind, const char *list)
{
	return t->t == kind && tr_one(t->nm.p ? t->nm.p : "", list);
}

/* The current node. */
hl_n *tr_cur(hl_p *p)
{
	return p->open.n ? p->open.p[p->open.n - 1] : 0;
}

/* The adjusted current node: the context element for a fragment whose
   stack holds only its root. */
hl_n *tr_acur(hl_p *p)
{
	if (p->frag && p->ctx && p->open.n == 1)
		return p->ctx;
	return tr_cur(p);
}

/* Whether a node is an HTML element with a name in a list. */
int tr_isin(hl_n *n, const char *list)
{
	return n && n->t == HL_ELEM && n->ns == HL_NSHTML && tr_one(n->tag, list);
}

/* Whether the parser is in foreign content where CDATA sections apply. */
int hl_foreign(hl_p *p)
{
	hl_n *a = tr_acur(p);

	return a && a->ns != HL_NSHTML;
}

/* The elements the spec calls special, by the flag set when made. */
int tr_special(hl_n *n)
{
	return (n->fl & HL_FSPECIAL) != 0;
}

/* Whether an element is special, worked out from its name once. */
int tr_isspecial(hl_n *n)
{
	if (n->ns == HL_NSHTML)
		return tr_one(n->tag, "address applet area article aside base basefont bgsound "
			      "blockquote body br button caption center col colgroup dd details dir "
			      "div dl dt embed fieldset figcaption figure footer form frame frameset "
			      "h1 h2 h3 h4 h5 h6 head header hgroup hr html iframe img input keygen li "
			      "link listing main marquee menu meta nav noembed noframes noscript object "
			      "ol p param plaintext pre script search section select source style summary "
			      "table tbody td template textarea tfoot th thead title tr track ul wbr xmp");
	if (n->ns == HL_NSMATH)
		return tr_one(n->tag, "mi mo mn ms mtext annotation-xml");
	return tr_one(n->tag, "foreignObject desc title");
}

/* Whether an element with a name in a list is in one of the spec's scopes:
   the default ('d'), list item ('l'), button ('b') or table ('t'). */
int tr_scope(hl_p *p, const char *tag, int kind)
{
	size_t i;
	hl_n *n;
	int one = !strchr(tag, ' ');

	for (i = p->open.n; i-- > 0;) {
		n = p->open.p[i];
		if (n->ns == HL_NSHTML && (one ? !strcmp(n->tag, tag) : tr_one(n->tag, tag)))
			return 1;
		if (n->fl & (kind == 't' ? HL_FTSCOPE : HL_FSCOPE))
			return 0;
		if (kind == 'l' && (n->fl & HL_FLIST))
			return 0;
		if (kind == 'b' && (n->fl & HL_FBUTTON))
			return 0;
	}
	return 0;
}

/* Whether a template element is on the stack of open elements. */
int tr_hastmpl(hl_p *p)
{
	size_t i;

	for (i = 0; i < p->open.n; i++)
		if (hl_isel(p->open.p[i], "template"))
			return 1;
	return 0;
}

/* Pop the current node. */
void tr_pop(hl_p *p)
{
	if (p->open.n)
		p->open.n--;
}

/* Pop until an HTML element of a name has been popped. */
void tr_popto(hl_p *p, const char *tag)
{
	hl_n *n;

	while (p->open.n) {
		n = tr_cur(p);
		tr_pop(p);
		if (n->ns == HL_NSHTML && !strcmp(n->tag, tag))
			return;
	}
}

/* Pop until an HTML element with a name in a list has been popped. */
void tr_poptoany(hl_p *p, const char *list)
{
	hl_n *n;

	while (p->open.n) {
		n = tr_cur(p);
		tr_pop(p);
		if (tr_isin(n, list))
			return;
	}
}

/* Whether a node is on the stack of open elements; where, or -1. */
long tr_onstack(hl_p *p, hl_n *n)
{
	size_t i;

	for (i = p->open.n; i-- > 0;)
		if (p->open.p[i] == n)
			return (long)i;
	return -1;
}

/* Take a node off the stack wherever it is. */
void tr_rmstack(hl_p *p, hl_n *n)
{
	long i = tr_onstack(p, n);

	if (i < 0)
		return;
	memmove(p->open.p + i, p->open.p + i + 1, (p->open.n - (size_t)i - 1) * sizeof(void *));
	p->open.n--;
}

/* Generate implied end tags, except for one name; thoroughly when asked. */
void tr_implied(hl_p *p, const char *except, int thorough)
{
	hl_n *n;
	const char *l = thorough ? "caption colgroup dd dt li optgroup option p rb rp rt rtc "
				   "tbody td tfoot th thead tr" :
				   "dd dt li optgroup option p rb rp rt rtc";

	while ((n = tr_cur(p)) && tr_isin(n, l) && !(except && !strcmp(n->tag, except)))
		tr_pop(p);
}

/* The appropriate place to insert a node: the parent, and the child to go
   before -- foster parenting when the target is a table part. */
hl_n *tr_place(hl_p *p, hl_n *target, hl_n **before)
{
	hl_n *t = target ? target : tr_cur(p), *tab = 0, *tmpl = 0;
	long ti = -1, mi = -1, i;

	*before = 0;
	if (p->fosterok && tr_isin(t, "table tbody tfoot thead tr")) {
		for (i = (long)p->open.n - 1; i >= 0; i--) {
			hl_n *n = p->open.p[i];

			if (mi < 0 && hl_isel(n, "template")) {
				mi = i;
				tmpl = n;
			}
			if (ti < 0 && hl_isel(n, "table")) {
				ti = i;
				tab = n;
			}
		}
		if (tmpl && (!tab || mi > ti))
			return tmpl->tmpl;
		if (!tab)
			return p->open.p[0];
		if (tab->up) {
			*before = tab;
			return tab->up;
		}
		return p->open.p[ti - 1];
	}
	if (hl_isel(t, "template"))
		return t->tmpl;
	return t;
}

/* Insert characters where they go, joined to a text node just before. */
void tr_text(hl_p *p, const char *s, size_t n)
{
	hl_n *bf, *up = tr_place(p, 0, &bf), *prev, *x;

	if (!n)
		return;
	if (up->t == HL_DOC)
		return;
	prev = bf ? bf->pv : up->last;
	if (prev && prev->t == HL_TEXT) {
		s_add(&prev->s, s, n);
		return;
	}
	x = hl_new(p->doc, HL_TEXT);
	s_add(&x->s, s, n);
	hl_insbefore(up, x, bf);
}

/* Insert a comment, where it goes or in a given parent. */
void tr_comment(hl_p *p, hl_tok *t, hl_n *in)
{
	hl_n *bf = 0, *up = in ? in : tr_place(p, 0, &bf), *x = hl_new(p->doc, HL_COMMENT);

	s_cat(&x->s, t->data.p ? t->data.p : "");
	hl_insbefore(up, x, bf);
}

/* SVG's camel-cased element names, from the lowered ones the tokenizer
   makes. */
const char *tr_svgtag(const char *t)
{
	static const char *m[] = {
		"altglyph", "altGlyph", "altglyphdef", "altGlyphDef", "altglyphitem", "altGlyphItem",
		"animatecolor", "animateColor", "animatemotion", "animateMotion",
		"animatetransform", "animateTransform", "clippath", "clipPath", "feblend", "feBlend",
		"fecolormatrix", "feColorMatrix", "fecomponenttransfer", "feComponentTransfer",
		"fecomposite", "feComposite", "feconvolvematrix", "feConvolveMatrix",
		"fediffuselighting", "feDiffuseLighting", "fedisplacementmap", "feDisplacementMap",
		"fedistantlight", "feDistantLight", "fedropshadow", "feDropShadow", "feflood", "feFlood",
		"fefunca", "feFuncA", "fefuncb", "feFuncB", "fefuncg", "feFuncG", "fefuncr", "feFuncR",
		"fegaussianblur", "feGaussianBlur", "feimage", "feImage", "femerge", "feMerge",
		"femergenode", "feMergeNode", "femorphology", "feMorphology", "feoffset", "feOffset",
		"fepointlight", "fePointLight", "fespecularlighting", "feSpecularLighting",
		"fespotlight", "feSpotLight", "fetile", "feTile", "feturbulence", "feTurbulence",
		"foreignobject", "foreignObject", "glyphref", "glyphRef", "lineargradient",
		"linearGradient", "radialgradient", "radialGradient", "textpath", "textPath", 0
	};
	int i;

	for (i = 0; m[i]; i += 2)
		if (!strcmp(m[i], t))
			return m[i + 1];
	return t;
}

/* SVG's and MathML's camel-cased attribute names, and the foreign
   attributes that carry a namespace prefix. */
void tr_adjattrs(hl_n *e)
{
	static const char *svg[] = {
		"attributename", "attributeName", "attributetype", "attributeType",
		"basefrequency", "baseFrequency", "baseprofile", "baseProfile", "calcmode", "calcMode",
		"clippathunits", "clipPathUnits", "diffuseconstant", "diffuseConstant",
		"edgemode", "edgeMode", "filterunits", "filterUnits", "glyphref", "glyphRef",
		"gradienttransform", "gradientTransform", "gradientunits", "gradientUnits",
		"kernelmatrix", "kernelMatrix", "kernelunitlength", "kernelUnitLength",
		"keypoints", "keyPoints", "keysplines", "keySplines", "keytimes", "keyTimes",
		"lengthadjust", "lengthAdjust", "limitingconeangle", "limitingConeAngle",
		"markerheight", "markerHeight", "markerunits", "markerUnits", "markerwidth", "markerWidth",
		"maskcontentunits", "maskContentUnits", "maskunits", "maskUnits",
		"numoctaves", "numOctaves", "pathlength", "pathLength", "patterncontentunits",
		"patternContentUnits", "patterntransform", "patternTransform", "patternunits",
		"patternUnits", "pointsatx", "pointsAtX", "pointsaty", "pointsAtY", "pointsatz",
		"pointsAtZ", "preservealpha", "preserveAlpha", "preserveaspectratio",
		"preserveAspectRatio", "primitiveunits", "primitiveUnits", "refx", "refX", "refy", "refY",
		"repeatcount", "repeatCount", "repeatdur", "repeatDur", "requiredextensions",
		"requiredExtensions", "requiredfeatures", "requiredFeatures", "specularconstant",
		"specularConstant", "specularexponent", "specularExponent", "spreadmethod",
		"spreadMethod", "startoffset", "startOffset", "stddeviation", "stdDeviation",
		"stitchtiles", "stitchTiles", "surfacescale", "surfaceScale", "systemlanguage",
		"systemLanguage", "tablevalues", "tableValues", "targetx", "targetX", "targety", "targetY",
		"textlength", "textLength", "viewbox", "viewBox", "viewtarget", "viewTarget",
		"xchannelselector", "xChannelSelector", "ychannelselector", "yChannelSelector",
		"zoomandpan", "zoomAndPan", 0
	};
	static const char *fx[] = {
		"xlink:actuate", "xlink", "actuate", "xlink:arcrole", "xlink", "arcrole",
		"xlink:href", "xlink", "href", "xlink:role", "xlink", "role", "xlink:show", "xlink", "show",
		"xlink:title", "xlink", "title", "xlink:type", "xlink", "type", "xml:lang", "xml", "lang",
		"xml:space", "xml", "space", "xmlns", "", "xmlns", "xmlns:xlink", "xmlns", "xlink", 0
	};
	size_t i;
	int j;

	for (i = 0; i < e->attrs.n; i++) {
		hl_attr *a = e->attrs.p[i];

		if (e->ns == HL_NSSVG) {
			for (j = 0; svg[j]; j += 2)
				if (!strcmp(a->nm, svg[j])) {
					free(a->nm);
					a->nm = xs(svg[j + 1]);
					break;
				}
		}
		if (e->ns == HL_NSMATH && !strcmp(a->nm, "definitionurl")) {
			free(a->nm);
			a->nm = xs("definitionURL");
		}
		for (j = 0; fx[j]; j += 3)
			if (!strcmp(a->nm, fx[j])) {
				free(a->nm);
				a->nm = xs(fx[j + 2]);
				if (*fx[j + 1])
					a->pfx = xs(fx[j + 1]);
				break;
			}
	}
}

/* Work out an element's scope and special flags from its name, once. */
void tr_flags(hl_n *e)
{
	e->fl = tr_isspecial(e) ? HL_FSPECIAL : 0;
	if (e->ns == HL_NSHTML) {
		if (tr_one(e->tag, "applet caption html table td th marquee object select template"))
			e->fl |= HL_FSCOPE;
		if (tr_one(e->tag, "html table template"))
			e->fl |= HL_FTSCOPE;
		if (tr_one(e->tag, "ol ul"))
			e->fl |= HL_FLIST;
		if (!strcmp(e->tag, "button"))
			e->fl |= HL_FBUTTON;
	} else if ((e->ns == HL_NSMATH && tr_one(e->tag, "mi mo mn ms mtext annotation-xml")) ||
		   (e->ns == HL_NSSVG && tr_one(e->tag, "foreignObject desc title"))) {
		e->fl |= HL_FSCOPE;
	}
}

/* Make an element for a start tag, in a namespace. */
hl_n *tr_elem(hl_p *p, hl_tok *t, int ns)
{
	hl_n *e = hl_new(p->doc, HL_ELEM);
	size_t i;
	const char *nm = t->nm.p ? t->nm.p : "";

	e->ns = ns;
	e->tag = xs(ns == HL_NSSVG ? tr_svgtag(nm) : nm);
	for (i = 0; i < t->attrs.n; i++) {
		hl_attr *a = t->attrs.p[i], *b = xm(sizeof *b);

		memset(b, 0, sizeof *b);
		b->nm = xs(a->nm);
		b->val = xs(a->val ? a->val : "");
		v_add(&e->attrs, b);
	}
	if (ns != HL_NSHTML)
		tr_adjattrs(e);
	if (ns == HL_NSHTML && !strcmp(e->tag, "template"))
		e->tmpl = hl_new(p->doc, HL_FRAG);
	tr_flags(e);
	return e;
}

/* Insert an element for a token where it goes and push it. Past HL_DEPTH
   open elements the deepest is closed first, as browsers cap the depth of
   what they build: every scope check walks the stack, so an unbounded one
   makes deep input quadratic, and every walk of the tree recurses. */
hl_n *tr_insert(hl_p *p, hl_tok *t, int ns)
{
	hl_n *bf, *up, *e;

	if (p->open.n >= HL_DEPTH) {
		lg(HIBR_LDBG, "html: more than %d elements deep; closing the deepest", HL_DEPTH);
		while (p->open.n >= HL_DEPTH)
			tr_pop(p);
	}
	up = tr_place(p, 0, &bf);
	e = tr_elem(p, t, ns);

	hl_insbefore(up, e, bf);
	v_add(&p->open, e);
	return e;
}

/* Insert an HTML element made from just a name. */
hl_n *tr_insname(hl_p *p, const char *nm)
{
	hl_tok t;
	hl_n *e;

	memset(&t, 0, sizeof t);
	t.t = HL_TSTART;
	s_init(&t.nm);
	s_cat(&t.nm, nm);
	e = tr_insert(p, &t, HL_NSHTML);
	s_free(&t.nm);
	return e;
}

/* Copy a token's attributes onto an element that lacks them. */
void tr_merge(hl_n *e, hl_tok *t)
{
	size_t i;

	for (i = 0; i < t->attrs.n; i++) {
		hl_attr *a = t->attrs.p[i];

		if (!hl_attr_get(e, a->nm))
			hl_setattr(e, a->nm, a->val ? a->val : "");
	}
}

/* Push an element onto the list of active formatting elements, keeping at
   most three alike after the last marker (Noah's Ark). */
void tr_pushfmt(hl_p *p, hl_n *e)
{
	size_t i, j, same = 0, first = 0;

	for (i = p->fmt.n; i-- > 0;) {
		hl_n *f = p->fmt.p[i];
		int eq;

		if (f == MARK)
			break;
		eq = f->ns == e->ns && !strcmp(f->tag, e->tag) && f->attrs.n == e->attrs.n;
		for (j = 0; eq && j < f->attrs.n; j++) {
			hl_attr *a = f->attrs.p[j];
			const char *v = hl_attr_get(e, a->nm);

			if (!v || strcmp(v, a->val))
				eq = 0;
		}
		if (eq) {
			same++;
			first = i;
		}
	}
	if (same >= 3) {
		memmove(p->fmt.p + first, p->fmt.p + first + 1, (p->fmt.n - first - 1) * sizeof(void *));
		p->fmt.n--;
	}
	v_add(&p->fmt, e);
}

/* Where an element is in the formatting list, or -1. */
long tr_infmt(hl_p *p, hl_n *e)
{
	size_t i;

	for (i = p->fmt.n; i-- > 0;)
		if (p->fmt.p[i] == e)
			return (long)i;
	return -1;
}

/* Take an element out of the formatting list. */
void tr_rmfmt(hl_p *p, hl_n *e)
{
	long i = tr_infmt(p, e);

	if (i < 0)
		return;
	memmove(p->fmt.p + i, p->fmt.p + i + 1, (p->fmt.n - (size_t)i - 1) * sizeof(void *));
	p->fmt.n--;
}

/* Clear the formatting list back to the last marker. */
void tr_clearfmt(hl_p *p)
{
	while (p->fmt.n) {
		hl_n *f = p->fmt.p[--p->fmt.n];

		if (f == MARK)
			break;
	}
}

/* Reconstruct the active formatting elements. */
void tr_reconstruct(hl_p *p)
{
	long i;
	hl_n *f, *e;
	hl_tok t;
	size_t j;

	if (!p->fmt.n)
		return;
	f = p->fmt.p[p->fmt.n - 1];
	if (f == MARK || tr_onstack(p, f) >= 0)
		return;
	i = (long)p->fmt.n - 1;
	while (i > 0) {
		f = p->fmt.p[i - 1];
		if (f == MARK || tr_onstack(p, f) >= 0)
			break;
		i--;
	}
	for (; i < (long)p->fmt.n; i++) {
		f = p->fmt.p[i];
		memset(&t, 0, sizeof t);
		t.t = HL_TSTART;
		s_init(&t.nm);
		s_cat(&t.nm, f->tag);
		for (j = 0; j < f->attrs.n; j++)
			v_add(&t.attrs, f->attrs.p[j]);
		e = tr_insert(p, &t, HL_NSHTML);
		v_free(&t.attrs);
		s_free(&t.nm);
		p->fmt.p[i] = e;
	}
}

/* The adoption agency algorithm, for an end tag of a formatting element;
   whether it handled the tag (when not, the tag is an "any other end
   tag"). */
int tr_adopt(hl_p *p, const char *tag)
{
	hl_n *cur = tr_cur(p), *fe, *fb, *ca, *node, *last, *nn, *x, *bf;
	long fi, si, i, ni, bm;
	int outer, inner;

	if (hl_isel(cur, tag) && tr_infmt(p, cur) < 0) {
		tr_pop(p);
		return 1;
	}
	for (outer = 0; outer < 8; outer++) {
		fe = 0;
		for (i = (long)p->fmt.n - 1; i >= 0; i--) {
			hl_n *f = p->fmt.p[i];

			if (f == MARK)
				break;
			if (hl_isel(f, tag)) {
				fe = f;
				break;
			}
		}
		if (!fe)
			return 0;
		fi = tr_onstack(p, fe);
		if (fi < 0) {
			tr_rmfmt(p, fe);
			return 1;
		}
		if (!tr_scope(p, tag, 'd'))
			return 1;
		fb = 0;
		for (si = fi + 1; si < (long)p->open.n; si++)
			if (tr_special(p->open.p[si])) {
				fb = p->open.p[si];
				break;
			}
		if (!fb) {
			while (tr_cur(p) != fe)
				tr_pop(p);
			tr_pop(p);
			tr_rmfmt(p, fe);
			return 1;
		}
		ca = p->open.p[fi - 1];
		bm = tr_infmt(p, fe);
		node = last = fb;
		ni = si;
		for (inner = 1;; inner++) {
			ni--;
			node = p->open.p[ni];
			if (node == fe)
				break;
			if (inner > 3 && tr_infmt(p, node) >= 0) {
				if (tr_infmt(p, node) < bm)
					bm--;
				tr_rmfmt(p, node);
			}
			if (tr_infmt(p, node) < 0) {
				tr_rmstack(p, node);
				continue;
			}
			{
				hl_tok t;
				size_t j;
				long fx = tr_infmt(p, node);

				memset(&t, 0, sizeof t);
				t.t = HL_TSTART;
				s_init(&t.nm);
				s_cat(&t.nm, node->tag);
				for (j = 0; j < node->attrs.n; j++)
					v_add(&t.attrs, node->attrs.p[j]);
				nn = tr_elem(p, &t, HL_NSHTML);
				v_free(&t.attrs);
				s_free(&t.nm);
				p->fmt.p[fx] = nn;
				p->open.p[tr_onstack(p, node)] = nn;
				node = nn;
			}
			if (last == fb)
				bm = tr_infmt(p, node) + 1;
			hl_append(node, last);
			last = node;
		}
		x = tr_place(p, ca, &bf);
		hl_insbefore(x, last, bf);
		{
			hl_tok t;
			size_t j;

			memset(&t, 0, sizeof t);
			t.t = HL_TSTART;
			s_init(&t.nm);
			s_cat(&t.nm, fe->tag);
			for (j = 0; j < fe->attrs.n; j++)
				v_add(&t.attrs, fe->attrs.p[j]);
			nn = tr_elem(p, &t, HL_NSHTML);
			v_free(&t.attrs);
			s_free(&t.nm);
		}
		while (fb->kid)
			hl_append(nn, fb->kid);
		hl_append(fb, nn);
		{
			long at = tr_infmt(p, fe);

			if (at < bm)
				bm--;
			tr_rmfmt(p, fe);
			if (bm > (long)p->fmt.n)
				bm = (long)p->fmt.n;
			v_add(&p->fmt, 0);
			memmove(p->fmt.p + bm + 1, p->fmt.p + bm, (p->fmt.n - (size_t)bm - 1) * sizeof(void *));
			p->fmt.p[bm] = nn;
		}
		tr_rmstack(p, fe);
		si = tr_onstack(p, fb);
		v_add(&p->open, 0);
		memmove(p->open.p + si + 2, p->open.p + si + 1, (p->open.n - (size_t)si - 2) * sizeof(void *));
		p->open.p[si + 1] = nn;
	}
	return 1;
}

/* Reset the insertion mode from the stack of open elements. */
void tr_reset(hl_p *p)
{
	long i;
	hl_n *n;
	int last;

	for (i = (long)p->open.n - 1; i >= 0; i--) {
		n = p->open.p[i];
		last = i == 0;
		if (last && p->frag && p->ctx)
			n = p->ctx;
		if (n->ns != HL_NSHTML) {
			if (last) {
				p->mode = M_INBODY;
				return;
			}
			continue;
		}
		if (tr_one(n->tag, "td th") && !last) {
			p->mode = M_INCELL;
			return;
		}
		if (!strcmp(n->tag, "tr")) {
			p->mode = M_INROW;
			return;
		}
		if (tr_one(n->tag, "tbody thead tfoot")) {
			p->mode = M_INTBODY;
			return;
		}
		if (!strcmp(n->tag, "caption")) {
			p->mode = M_INCAPTION;
			return;
		}
		if (!strcmp(n->tag, "colgroup")) {
			p->mode = M_INCOLGROUP;
			return;
		}
		if (!strcmp(n->tag, "table")) {
			p->mode = M_INTABLE;
			return;
		}
		if (!strcmp(n->tag, "template")) {
			p->mode = (int)(long)p->tmodes.p[p->tmodes.n - 1];
			return;
		}
		if (!strcmp(n->tag, "head") && !last) {
			p->mode = M_INHEAD;
			return;
		}
		if (!strcmp(n->tag, "body")) {
			p->mode = M_INBODY;
			return;
		}
		if (!strcmp(n->tag, "frameset")) {
			p->mode = M_INFRAMESET;
			return;
		}
		if (!strcmp(n->tag, "html")) {
			p->mode = p->head ? M_AHEAD : M_BHEAD;
			return;
		}
		if (last) {
			p->mode = M_INBODY;
			return;
		}
	}
	p->mode = M_INBODY;
}

/* Whether a whitespace-only character token. */
int tr_ws(hl_tok *t)
{
	size_t i;

	if (t->t != HL_TCHAR || !t->data.n)
		return 0;
	for (i = 0; i < t->data.n; i++)
		if (!strchr(" \t\n\f\r", t->data.p[i]))
			return 0;
	return 1;
}

/* Whether a NUL character token. */
int tr_nul(hl_tok *t)
{
	return t->t == HL_TCHAR && t->data.n == 1 && t->data.p[0] == 0;
}

/* The public identifiers a quirks-mode doctype starts with. */
int tr_quirkpub(const char *pub)
{
	static const char *q[] = {
		"+//silmaril//dtd html pro v0r11 19970101//", "-//as//dtd html 3.0 aswedit + extensions//",
		"-//advasoft ltd//dtd html 3.0 aswedit + extensions//", "-//ietf//dtd html 2.0 level 1//",
		"-//ietf//dtd html 2.0 level 2//", "-//ietf//dtd html 2.0 strict level 1//",
		"-//ietf//dtd html 2.0 strict level 2//", "-//ietf//dtd html 2.0 strict//",
		"-//ietf//dtd html 2.0//", "-//ietf//dtd html 2.1e//", "-//ietf//dtd html 3.0//",
		"-//ietf//dtd html 3.2 final//", "-//ietf//dtd html 3.2//", "-//ietf//dtd html 3//",
		"-//ietf//dtd html level 0//", "-//ietf//dtd html level 1//", "-//ietf//dtd html level 2//",
		"-//ietf//dtd html level 3//", "-//ietf//dtd html strict level 0//",
		"-//ietf//dtd html strict level 1//", "-//ietf//dtd html strict level 2//",
		"-//ietf//dtd html strict level 3//", "-//ietf//dtd html strict//", "-//ietf//dtd html//",
		"-//metrius//dtd metrius presentational//",
		"-//microsoft//dtd internet explorer 2.0 html strict//",
		"-//microsoft//dtd internet explorer 2.0 html//",
		"-//microsoft//dtd internet explorer 2.0 tables//",
		"-//microsoft//dtd internet explorer 3.0 html strict//",
		"-//microsoft//dtd internet explorer 3.0 html//",
		"-//microsoft//dtd internet explorer 3.0 tables//",
		"-//netscape comm. corp.//dtd html//", "-//netscape comm. corp.//dtd strict html//",
		"-//o'reilly and associates//dtd html 2.0//",
		"-//o'reilly and associates//dtd html extended 1.0//",
		"-//o'reilly and associates//dtd html extended relaxed 1.0//",
		"-//sq//dtd html 2.0 hotmetal + extensions//",
		"-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
		"-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
		"-//spyglass//dtd html 2.0 extended//", "-//sun microsystems corp.//dtd hotjava html//",
		"-//sun microsystems corp.//dtd hotjava strict html//", "-//w3c//dtd html 3 1995-03-24//",
		"-//w3c//dtd html 3.2 draft//", "-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//",
		"-//w3c//dtd html 3.2s draft//", "-//w3c//dtd html 4.0 frameset//",
		"-//w3c//dtd html 4.0 transitional//", "-//w3c//dtd html experimental 19960712//",
		"-//w3c//dtd html experimental 970421//", "-//w3c//dtd w3 html//", "-//w3o//dtd w3 html 3.0//",
		"-//webtechs//dtd mozilla html 2.0//", "-//webtechs//dtd mozilla html//", 0
	};
	int i;

	for (i = 0; q[i]; i++)
		if (!strncasecmp(pub, q[i], strlen(q[i])))
			return 1;
	return 0;
}

/* The quirks mode a doctype puts the document in. */
int tr_quirks(hl_tok *t)
{
	const char *pub = t->pub.p ? t->pub.p : "", *sys = t->sys.p ? t->sys.p : "";

	if (t->quirks || !t->nm.p || strcmp(t->nm.p, "html"))
		return HL_QYES;
	if (t->haspub && (!strcasecmp(pub, "-//w3o//dtd w3 html strict 3.0//en//") ||
			  !strcasecmp(pub, "-/w3c/dtd html 4.0 transitional/en") ||
			  !strcasecmp(pub, "html")))
		return HL_QYES;
	if (t->hassys && !strcasecmp(sys, "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"))
		return HL_QYES;
	if (t->haspub && tr_quirkpub(pub))
		return HL_QYES;
	if (!t->hassys && t->haspub &&
	    (!strncasecmp(pub, "-//w3c//dtd html 4.01 frameset//", 32) ||
	     !strncasecmp(pub, "-//w3c//dtd html 4.01 transitional//", 36)))
		return HL_QYES;
	if (t->haspub && (!strncasecmp(pub, "-//w3c//dtd xhtml 1.0 frameset//", 32) ||
			  !strncasecmp(pub, "-//w3c//dtd xhtml 1.0 transitional//", 36)))
		return HL_QLIMITED;
	if (t->hassys && t->haspub &&
	    (!strncasecmp(pub, "-//w3c//dtd html 4.01 frameset//", 32) ||
	     !strncasecmp(pub, "-//w3c//dtd html 4.01 transitional//", 36)))
		return HL_QLIMITED;
	return HL_QNO;
}

/* Close a p element. */
void tr_closep(hl_p *p)
{
	tr_implied(p, "p", 0);
	tr_popto(p, "p");
}

/* The generic RCDATA and raw text element parsing algorithms. */
void tr_rawtext(hl_p *p, hl_tok *t, int state)
{
	tr_insert(p, t, HL_NSHTML);
	p->state = state;
	p->orig = p->mode;
	p->mode = M_TEXT;
}

/* A character token split at its first non-whitespace: the whitespace
   handled by one rule and the rest by another means handling the first
   part here and the rest again; whether there was a rest to reprocess. */
int tr_splitws(hl_tok *t, str *rest)
{
	size_t i;

	if (t->t != HL_TCHAR)
		return 0;
	for (i = 0; i < t->data.n && strchr(" \t\n\f\r", t->data.p[i]); i++)
		;
	if (i == 0 || i == t->data.n)
		return 0;
	s_add(rest, t->data.p + i, t->data.n - i);
	t->data.n = i;
	t->data.p[i] = 0;
	return 1;
}

/* Process a token in a mode, splitting a character run whose leading
   whitespace and remainder are handled differently. */
void tr_procws(hl_p *p, hl_tok *t)
{
	str rest;
	hl_tok u;

	s_init(&rest);
	if (tr_splitws(t, &rest)) {
		tr_proc(p, t);
		memset(&u, 0, sizeof u);
		u.t = HL_TCHAR;
		u.data = rest;
		tr_proc(p, &u);
	} else {
		tr_proc(p, t);
	}
	s_free(&rest);
}

/* The initial insertion mode. */
void tr_initial(hl_p *p, hl_tok *t)
{
	hl_n *d;

	if (tr_ws(t))
		return;
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, p->doc->root);
		return;
	}
	if (t->t == HL_TDOCTYPE) {
		d = hl_new(p->doc, HL_DOCTYPE);
		s_cat(&d->s, t->nm.p ? t->nm.p : "");
		d->haspub = t->haspub;
		d->hassys = t->hassys;
		d->pub = xs(t->pub.p ? t->pub.p : "");
		d->sys = xs(t->sys.p ? t->sys.p : "");
		hl_append(p->doc->root, d);
		p->doc->quirks = tr_quirks(t);
		p->mode = M_BHTML;
		return;
	}
	p->doc->quirks = HL_QYES;
	p->mode = M_BHTML;
	tr_proc(p, t);
}

/* The before html insertion mode. */
void tr_bhtml(hl_p *p, hl_tok *t)
{
	hl_n *e;

	if (t->t == HL_TDOCTYPE || tr_ws(t))
		return;
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, p->doc->root);
		return;
	}
	if (tr_tag(t, HL_TSTART, "html")) {
		e = tr_elem(p, t, HL_NSHTML);
		hl_append(p->doc->root, e);
		v_add(&p->open, e);
		p->mode = M_BHEAD;
		return;
	}
	if (t->t == HL_TEND && !tr_tag(t, HL_TEND, "head body html br"))
		return;
	{
		hl_tok h;

		memset(&h, 0, sizeof h);
		h.t = HL_TSTART;
		s_init(&h.nm);
		s_cat(&h.nm, "html");
		e = tr_elem(p, &h, HL_NSHTML);
		s_free(&h.nm);
	}
	hl_append(p->doc->root, e);
	v_add(&p->open, e);
	p->mode = M_BHEAD;
	tr_proc(p, t);
}

/* The before head insertion mode. */
void tr_bhead(hl_p *p, hl_tok *t)
{
	if (tr_ws(t))
		return;
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
	if (tr_tag(t, HL_TSTART, "head")) {
		p->head = tr_insert(p, t, HL_NSHTML);
		p->mode = M_INHEAD;
		return;
	}
	if (t->t == HL_TEND && !tr_tag(t, HL_TEND, "head body html br"))
		return;
	p->head = tr_insname(p, "head");
	p->mode = M_INHEAD;
	tr_proc(p, t);
}

/* The in head insertion mode. */
void tr_inhead(hl_p *p, hl_tok *t)
{
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
	if (tr_tag(t, HL_TSTART, "base basefont bgsound link meta")) {
		tr_insert(p, t, HL_NSHTML);
		tr_pop(p);
		return;
	}
	if (tr_tag(t, HL_TSTART, "title")) {
		tr_rawtext(p, t, HL_SRCDATA);
		return;
	}
	if ((tr_tag(t, HL_TSTART, "noscript") && p->scripting) ||
	    tr_tag(t, HL_TSTART, "noframes style")) {
		tr_rawtext(p, t, HL_SRAWTEXT);
		return;
	}
	if (tr_tag(t, HL_TSTART, "noscript")) {
		tr_insert(p, t, HL_NSHTML);
		p->mode = M_INHEADNS;
		return;
	}
	if (tr_tag(t, HL_TSTART, "script")) {
		tr_rawtext(p, t, HL_SSCRIPT);
		return;
	}
	if (tr_tag(t, HL_TEND, "head")) {
		tr_pop(p);
		p->mode = M_AHEAD;
		return;
	}
	if (tr_tag(t, HL_TSTART, "template")) {
		tr_insert(p, t, HL_NSHTML);
		v_add(&p->fmt, MARK);
		p->framesetok = 0;
		p->mode = M_INTEMPLATE;
		v_add(&p->tmodes, (void *)(long)M_INTEMPLATE);
		return;
	}
	if (tr_tag(t, HL_TEND, "template")) {
		if (!tr_hastmpl(p))
			return;
		tr_implied(p, 0, 1);
		tr_popto(p, "template");
		tr_clearfmt(p);
		if (p->tmodes.n)
			p->tmodes.n--;
		tr_reset(p);
		return;
	}
	if (tr_tag(t, HL_TSTART, "head") || (t->t == HL_TEND && !tr_tag(t, HL_TEND, "body html br")))
		return;
	tr_pop(p);
	p->mode = M_AHEAD;
	tr_proc(p, t);
}

/* The in head noscript insertion mode. */
void tr_inheadns(hl_p *p, hl_tok *t)
{
	if (t->t == HL_TDOCTYPE)
		return;
	if (tr_tag(t, HL_TSTART, "html")) {
		tr_body(p, t);
		return;
	}
	if (tr_tag(t, HL_TEND, "noscript")) {
		tr_pop(p);
		p->mode = M_INHEAD;
		return;
	}
	if (tr_ws(t) || t->t == HL_TCOMMENT ||
	    tr_tag(t, HL_TSTART, "basefont bgsound link meta noframes style")) {
		tr_inhead(p, t);
		return;
	}
	if (tr_tag(t, HL_TSTART, "head noscript") || (t->t == HL_TEND && !tr_tag(t, HL_TEND, "br")))
		return;
	tr_pop(p);
	p->mode = M_INHEAD;
	tr_proc(p, t);
}

/* The after head insertion mode. */
void tr_ahead(hl_p *p, hl_tok *t)
{
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
	if (tr_tag(t, HL_TSTART, "body")) {
		tr_insert(p, t, HL_NSHTML);
		p->framesetok = 0;
		p->mode = M_INBODY;
		return;
	}
	if (tr_tag(t, HL_TSTART, "frameset")) {
		tr_insert(p, t, HL_NSHTML);
		p->mode = M_INFRAMESET;
		return;
	}
	if (tr_tag(t, HL_TSTART, "base basefont bgsound link meta noframes script style template title")) {
		v_add(&p->open, p->head);
		tr_inhead(p, t);
		tr_rmstack(p, p->head);
		return;
	}
	if (tr_tag(t, HL_TEND, "template")) {
		tr_inhead(p, t);
		return;
	}
	if (tr_tag(t, HL_TSTART, "head") || (t->t == HL_TEND && !tr_tag(t, HL_TEND, "body html br")))
		return;
	tr_insname(p, "body");
	p->mode = M_INBODY;
	tr_proc(p, t);
}

/* The text insertion mode. */
void tr_textmode(hl_p *p, hl_tok *t)
{
	if (t->t == HL_TCHAR) {
		tr_text(p, t->data.p, t->data.n);
		return;
	}
	if (t->t == HL_TEOF) {
		tr_pop(p);
		p->mode = p->orig;
		tr_proc(p, t);
		return;
	}
	if (t->t == HL_TEND) {
		tr_pop(p);
		p->mode = p->orig;
	}
}

/* The in frameset insertion mode. */
void tr_inframeset(hl_p *p, hl_tok *t)
{
	if (t->t == HL_TCHAR) {
		size_t i;
		str w;

		s_init(&w);
		for (i = 0; i < t->data.n; i++)
			if (strchr(" \t\n\f\r", t->data.p[i]))
				s_ch(&w, t->data.p[i]);
		tr_text(p, w.p ? w.p : "", w.n);
		s_free(&w);
		return;
	}
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, 0);
		return;
	}
	if (tr_tag(t, HL_TSTART, "html")) {
		tr_body(p, t);
		return;
	}
	if (tr_tag(t, HL_TSTART, "frameset")) {
		tr_insert(p, t, HL_NSHTML);
		return;
	}
	if (tr_tag(t, HL_TEND, "frameset")) {
		if (p->open.n == 1)
			return;
		tr_pop(p);
		if (!p->frag && !hl_isel(tr_cur(p), "frameset"))
			p->mode = M_AFRAMESET;
		return;
	}
	if (tr_tag(t, HL_TSTART, "frame")) {
		tr_insert(p, t, HL_NSHTML);
		tr_pop(p);
		return;
	}
	if (tr_tag(t, HL_TSTART, "noframes")) {
		tr_inhead(p, t);
		return;
	}
}

/* The after frameset insertion mode. */
void tr_aframeset(hl_p *p, hl_tok *t)
{
	if (t->t == HL_TCHAR) {
		size_t i;
		str w;

		s_init(&w);
		for (i = 0; i < t->data.n; i++)
			if (strchr(" \t\n\f\r", t->data.p[i]))
				s_ch(&w, t->data.p[i]);
		tr_text(p, w.p ? w.p : "", w.n);
		s_free(&w);
		return;
	}
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, 0);
		return;
	}
	if (tr_tag(t, HL_TSTART, "html")) {
		tr_body(p, t);
		return;
	}
	if (tr_tag(t, HL_TEND, "html")) {
		p->mode = M_AAFRAMESET;
		return;
	}
	if (tr_tag(t, HL_TSTART, "noframes"))
		tr_inhead(p, t);
}

/* The after body insertion mode. */
void tr_abody(hl_p *p, hl_tok *t)
{
	if (tr_ws(t) || tr_tag(t, HL_TSTART, "html")) {
		tr_body(p, t);
		return;
	}
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, p->open.p[0]);
		return;
	}
	if (t->t == HL_TDOCTYPE)
		return;
	if (tr_tag(t, HL_TEND, "html")) {
		if (!p->frag)
			p->mode = M_AABODY;
		return;
	}
	if (t->t == HL_TEOF)
		return;
	p->mode = M_INBODY;
	tr_proc(p, t);
}

/* The after after body and after after frameset insertion modes. */
void tr_aabody(hl_p *p, hl_tok *t, int frameset)
{
	if (t->t == HL_TCOMMENT) {
		tr_comment(p, t, p->doc->root);
		return;
	}
	if (t->t == HL_TDOCTYPE || tr_ws(t) || tr_tag(t, HL_TSTART, "html")) {
		tr_body(p, t);
		return;
	}
	if (t->t == HL_TEOF)
		return;
	if (frameset) {
		if (tr_tag(t, HL_TSTART, "noframes"))
			tr_inhead(p, t);
		return;
	}
	p->mode = M_INBODY;
	tr_proc(p, t);
}

/* Whether a token should be handled by the rules for foreign content. */
int tr_useforeign(hl_p *p, hl_tok *t)
{
	hl_n *a = tr_acur(p);
	const char *enc;

	if (!a || a->ns == HL_NSHTML || t->t == HL_TEOF)
		return 0;
	if (a->ns == HL_NSMATH && tr_one(a->tag, "mi mo mn ms mtext")) {
		if (t->t == HL_TSTART && !tr_tag(t, HL_TSTART, "mglyph malignmark"))
			return 0;
		if (t->t == HL_TCHAR)
			return 0;
	}
	if (a->ns == HL_NSMATH && !strcmp(a->tag, "annotation-xml") &&
	    tr_tag(t, HL_TSTART, "svg"))
		return 0;
	if (t->t == HL_TSTART || t->t == HL_TCHAR) {
		if (a->ns == HL_NSSVG && tr_one(a->tag, "foreignObject desc title"))
			return 0;
		if (a->ns == HL_NSMATH && !strcmp(a->tag, "annotation-xml") &&
		    (enc = hl_attr_get(a, "encoding")) &&
		    (!strcasecmp(enc, "text/html") || !strcasecmp(enc, "application/xhtml+xml")))
			return 0;
	}
	return 1;
}

/* Hand one token to the mode that handles it now, foreign content's own
   rules included. Reprocessing recurses; a bound keeps a pathological
   document from overflowing the stack. */
void tr_proc(hl_p *p, hl_tok *t)
{
	if (++p->depth > 200) {
		p->depth--;
		return;
	}
	if (tr_useforeign(p, t))
		tr_foreign(p, t);
	else
		tr_mode(p, t);
	p->depth--;
}

/* Hand one token to the current insertion mode's rules for HTML content. */
void tr_mode(hl_p *p, hl_tok *t)
{
	switch (p->mode) {
	case M_INITIAL:
		tr_initial(p, t);
		break;
	case M_BHTML:
		tr_bhtml(p, t);
		break;
	case M_BHEAD:
		tr_bhead(p, t);
		break;
	case M_INHEAD:
		tr_inhead(p, t);
		break;
	case M_INHEADNS:
		tr_inheadns(p, t);
		break;
	case M_AHEAD:
		tr_ahead(p, t);
		break;
	case M_INBODY:
		tr_body(p, t);
		break;
	case M_TEXT:
		tr_textmode(p, t);
		break;
	case M_INTABLE:
	case M_INTABLETEXT:
	case M_INCAPTION:
	case M_INCOLGROUP:
	case M_INTBODY:
	case M_INROW:
	case M_INCELL:
	case M_INSELECT:
	case M_INSELECTTABLE:
		tr_intable(p, t);
		break;
	case M_INTEMPLATE:
		tr_intemplate(p, t);
		break;
	case M_ABODY:
		tr_abody(p, t);
		break;
	case M_INFRAMESET:
		tr_inframeset(p, t);
		break;
	case M_AFRAMESET:
		tr_aframeset(p, t);
		break;
	case M_AABODY:
		tr_aabody(p, t, 0);
		break;
	case M_AAFRAMESET:
		tr_aabody(p, t, 1);
		break;
	}
}

/* The tokenizer's hand-off: a line feed right after pre, listing or
   textarea is dropped, and a character run whose leading whitespace is
   handled differently from what follows is split. */
void hl_emit(hl_p *p, hl_tok *t)
{
	if (p->ignlf) {
		p->ignlf = 0;
		if (t->t == HL_TCHAR && t->data.n && t->data.p[0] == '\n') {
			if (t->data.n == 1)
				return;
			memmove(t->data.p, t->data.p + 1, t->data.n);
			t->data.n--;
		}
	}
	if (t->t == HL_TCHAR) {
		hl_tok u = *t;

		tr_procws(p, &u);
		return;
	}
	tr_proc(p, t);
}

/* A new document with its root. */
hl_doc *tr_newdoc(void)
{
	hl_doc *d = xm(sizeof *d);

	memset(d, 0, sizeof *d);
	d->root = hl_new(d, HL_DOC);
	return d;
}

/* Let go of a parser's own state. */
void tr_done(hl_p *p)
{
	hl_tokfree(p);
	v_free(&p->open);
	v_free(&p->fmt);
	v_free(&p->tmodes);
	s_free(&p->pendtab_s);
}

/* Parse a whole document. */
hl_doc *hl_parse(const char *src, size_t n, int scripting)
{
	hl_p p;

	memset(&p, 0, sizeof p);
	p.doc = tr_newdoc();
	p.scripting = scripting;
	p.framesetok = 1;
	s_init(&p.pendtab_s);
	hl_tokinit(&p, src, n);
	p.mode = M_INITIAL;
	hl_toknext(&p);
	tr_done(&p);
	hl_selected(p.doc, p.doc->root);
	return p.doc;
}

/* Parse a fragment as the children of a context element: what
   innerHTML does. The result's root holds an html element whose children
   are the fragment. */
hl_doc *hl_parsefrag(const char *src, size_t n, const char *ctx, int ctxns, int scripting)
{
	hl_p p;
	hl_n *html, *c;
	hl_tok t;

	memset(&p, 0, sizeof p);
	p.doc = tr_newdoc();
	p.scripting = scripting;
	p.framesetok = 1;
	p.frag = 1;
	s_init(&p.pendtab_s);
	hl_tokinit(&p, src, n);
	memset(&t, 0, sizeof t);
	t.t = HL_TSTART;
	s_init(&t.nm);
	s_cat(&t.nm, ctx);
	c = tr_elem(&p, &t, ctxns);
	s_free(&t.nm);
	p.ctx = c;
	if (ctxns == HL_NSHTML) {
		if (tr_one(ctx, "title textarea"))
			p.state = HL_SRCDATA;
		else if (tr_one(ctx, "style xmp iframe noembed noframes") ||
			 (!strcmp(ctx, "noscript") && scripting))
			p.state = HL_SRAWTEXT;
		else if (!strcmp(ctx, "script"))
			p.state = HL_SSCRIPT;
		else if (!strcmp(ctx, "plaintext"))
			p.state = HL_SPLAIN;
	}
	memset(&t, 0, sizeof t);
	t.t = HL_TSTART;
	s_init(&t.nm);
	s_cat(&t.nm, "html");
	html = tr_elem(&p, &t, HL_NSHTML);
	s_free(&t.nm);
	hl_append(p.doc->root, html);
	v_add(&p.open, html);
	if (ctxns == HL_NSHTML && !strcmp(ctx, "template"))
		v_add(&p.tmodes, (void *)(long)M_INTEMPLATE);
	tr_reset(&p);
	if (ctxns == HL_NSHTML && !strcmp(ctx, "form"))
		p.form = c;
	hl_toknext(&p);
	tr_done(&p);
	hl_selected(p.doc, p.doc->root);
	return p.doc;
}
