/*
 * pdf -- PDF documents through Poppler, loaded at run time (Gitea #348).
 *
 * libpoppler-glib, libcairo, libgobject and libglib are dlopen'd on first
 * use, never linked, so a shell that opens no PDF pays nothing and one built
 * without them still runs. The struct layouts read here -- a rectangle, a
 * link mapping, an action, a destination, a GList, a GError -- are written
 * from Poppler 22.12's and GLib's own headers, and each is one a stable C
 * ABI has kept since Poppler 0.x.
 *
 * A document is drawn as a viewport: `pdf view` lays every page out one
 * under the next, scaled by a zoom (screen pixels to a point), and renders
 * the rectangle that shows, to a PPM the img module then places -- as
 * pixels, half blocks or text characters, whichever the terminal can show.
 * So a page that spans the top of a window and the page after it is one
 * picture, and continuous scrolling is a question of where the viewport
 * starts. Every position given to or answered by a verb is in one of two
 * systems: points on a page with the origin at its top left, or pixels in
 * the laid-out document; `pdf at` and `pdf where` turn one into the other.
 */
#include "hibr.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PD_GAP
#define PD_GAP 12
#endif
#ifndef PD_MAXPX
#define PD_MAXPX 40000000L
#endif

typedef struct pd_rect {
	double x1, y1, x2, y2;
} pd_rect;

typedef struct pd_lmap {
	pd_rect area;
	void *action;
} pd_lmap;

typedef struct pd_act {
	int type;
	char *title;
	void *p;
} pd_act;

typedef struct pd_dest {
	int type;
	int page;
	double left, bottom, right, top, zoom;
	char *named;
} pd_dest;

typedef struct pd_list {
	void *data;
	struct pd_list *next, *prev;
} pd_list;

typedef struct pd_gerr {
	unsigned domain;
	int code;
	char *message;
} pd_gerr;

enum { PD_GOTO = 2, PD_URI = 5, PD_NAMED = 9 };

static struct {
	void *(*open)(const char *, const char *, pd_gerr **);
	int (*npages)(void *);
	void *(*page)(void *, int);
	char *(*title)(void *);
	void *(*finddest)(void *, const char *);
	void (*size)(void *, double *, double *);
	void (*render)(void *, void *);
	char *(*text)(void *);
	pd_list *(*find)(void *, const char *);
	char *(*seltext)(void *, int, pd_rect *);
	pd_list *(*links)(void *);
	void (*freelinks)(pd_list *);
	void (*rectfree)(pd_rect *);
	void (*actfree)(void *);
	void (*destfree)(void *);
	void *(*itnew)(void *);
	void (*itfree)(void *);
	void *(*itchild)(void *);
	int (*itnext)(void *);
	void *(*itact)(void *);
	void *(*surf)(int, int, int);
	void *(*cr)(void *);
	void (*rgb)(void *, double, double, double);
	void (*rgba)(void *, double, double, double, double);
	void (*paint)(void *);
	void (*translate)(void *, double, double);
	void (*scale)(void *, double, double);
	void (*save)(void *);
	void (*restore)(void *);
	void (*rect)(void *, double, double, double, double);
	void (*fill)(void *);
	void (*flush)(void *);
	unsigned char *(*data)(void *);
	int (*stride)(void *);
	void (*crfree)(void *);
	void (*surffree)(void *);
	void (*unref)(void *);
	void (*gfree)(void *);
	void (*listfree)(pd_list *);
	void (*errfree)(pd_gerr *);
	char *(*touri)(const char *, const char *, pd_gerr **);
} pd;

static int pd_loaded, pd_tried;

typedef struct pd_doc {
	int id;
	void *doc;
	int n;
	double *pw, *ph;
	long seq;
	char *last;
} pd_doc;

static vec pd_docs;
static int pd_next = 1;

/* One library, by any of the names it goes by here. */
void *pd_lib(const char *const *names)
{
	void *h;
	int i;

	for (i = 0; names[i]; i++) {
		h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (h)
			return h;
	}
	return 0;
}

/* One symbol, counting a miss. */
void *pd_sym(void *h, const char *nm, int *bad)
{
	void *p = h ? dlsym(h, nm) : 0;

	if (!p) {
		lg(HIBR_LDBG, "pdf: no %s", nm);
		*bad = 1;
	}
	return p;
}

/* Load the libraries once, the first time a document is opened; a missing
   one says what to install, once. */
int pd_load(void)
{
#ifdef __APPLE__
	const char *pn[] = { "/opt/homebrew/opt/poppler/lib/libpoppler-glib.dylib",
			     "/usr/local/opt/poppler/lib/libpoppler-glib.dylib",
			     "libpoppler-glib.8.dylib", "libpoppler-glib.dylib", 0 };
	const char *cn[] = { "/opt/homebrew/opt/cairo/lib/libcairo.2.dylib",
			     "/usr/local/opt/cairo/lib/libcairo.2.dylib",
			     "libcairo.2.dylib", 0 };
	const char *on[] = { "/opt/homebrew/opt/glib/lib/libgobject-2.0.0.dylib",
			     "/usr/local/opt/glib/lib/libgobject-2.0.0.dylib",
			     "libgobject-2.0.0.dylib", 0 };
	const char *gn[] = { "/opt/homebrew/opt/glib/lib/libglib-2.0.0.dylib",
			     "/usr/local/opt/glib/lib/libglib-2.0.0.dylib",
			     "libglib-2.0.0.dylib", 0 };
#else
	const char *pn[] = { "libpoppler-glib.so.8", "libpoppler-glib.so", 0 };
	const char *cn[] = { "libcairo.so.2", "libcairo.so", 0 };
	const char *on[] = { "libgobject-2.0.so.0", "libgobject-2.0.so", 0 };
	const char *gn[] = { "libglib-2.0.so.0", "libglib-2.0.so", 0 };
#endif
	void *p, *c, *o, *g;
	int bad = 0;

	if (pd_loaded)
		return HIBR_OK;
	if (pd_tried)
		return HIBR_FAIL;
	pd_tried = 1;
	p = pd_lib(pn);
	c = pd_lib(cn);
	o = pd_lib(on);
	g = pd_lib(gn);
	if (!p || !c || !o || !g) {
		lg(HIBR_LERR, "pdf: Poppler is not installed -- on Debian "
			      "`apt install libpoppler-glib8`, on a Mac `brew "
			      "install poppler`");
		return HIBR_FAIL;
	}
	pd.open = pd_sym(p, "poppler_document_new_from_file", &bad);
	pd.npages = pd_sym(p, "poppler_document_get_n_pages", &bad);
	pd.page = pd_sym(p, "poppler_document_get_page", &bad);
	pd.title = pd_sym(p, "poppler_document_get_title", &bad);
	pd.finddest = pd_sym(p, "poppler_document_find_dest", &bad);
	pd.size = pd_sym(p, "poppler_page_get_size", &bad);
	pd.render = pd_sym(p, "poppler_page_render", &bad);
	pd.text = pd_sym(p, "poppler_page_get_text", &bad);
	pd.find = pd_sym(p, "poppler_page_find_text", &bad);
	pd.seltext = pd_sym(p, "poppler_page_get_selected_text", &bad);
	pd.links = pd_sym(p, "poppler_page_get_link_mapping", &bad);
	pd.freelinks = pd_sym(p, "poppler_page_free_link_mapping", &bad);
	pd.rectfree = pd_sym(p, "poppler_rectangle_free", &bad);
	pd.actfree = pd_sym(p, "poppler_action_free", &bad);
	pd.destfree = pd_sym(p, "poppler_dest_free", &bad);
	pd.itnew = pd_sym(p, "poppler_index_iter_new", &bad);
	pd.itfree = pd_sym(p, "poppler_index_iter_free", &bad);
	pd.itchild = pd_sym(p, "poppler_index_iter_get_child", &bad);
	pd.itnext = pd_sym(p, "poppler_index_iter_next", &bad);
	pd.itact = pd_sym(p, "poppler_index_iter_get_action", &bad);
	pd.surf = pd_sym(c, "cairo_image_surface_create", &bad);
	pd.cr = pd_sym(c, "cairo_create", &bad);
	pd.rgb = pd_sym(c, "cairo_set_source_rgb", &bad);
	pd.rgba = pd_sym(c, "cairo_set_source_rgba", &bad);
	pd.paint = pd_sym(c, "cairo_paint", &bad);
	pd.translate = pd_sym(c, "cairo_translate", &bad);
	pd.scale = pd_sym(c, "cairo_scale", &bad);
	pd.save = pd_sym(c, "cairo_save", &bad);
	pd.restore = pd_sym(c, "cairo_restore", &bad);
	pd.rect = pd_sym(c, "cairo_rectangle", &bad);
	pd.fill = pd_sym(c, "cairo_fill", &bad);
	pd.flush = pd_sym(c, "cairo_surface_flush", &bad);
	pd.data = pd_sym(c, "cairo_image_surface_get_data", &bad);
	pd.stride = pd_sym(c, "cairo_image_surface_get_stride", &bad);
	pd.crfree = pd_sym(c, "cairo_destroy", &bad);
	pd.surffree = pd_sym(c, "cairo_surface_destroy", &bad);
	pd.unref = pd_sym(o, "g_object_unref", &bad);
	pd.gfree = pd_sym(g, "g_free", &bad);
	pd.listfree = pd_sym(g, "g_list_free", &bad);
	pd.errfree = pd_sym(g, "g_error_free", &bad);
	pd.touri = pd_sym(g, "g_filename_to_uri", &bad);
	if (bad) {
		lg(HIBR_LERR, "pdf: this Poppler lacks a function hibr needs");
		return HIBR_FAIL;
	}
	pd_loaded = 1;
	lg(HIBR_LDBG, "pdf: Poppler loaded on demand");
	return HIBR_OK;
}

/* A value back through the result slot, printed when nobody bound it. */
void pd_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* A number into a string, as a decimal with one place. */
void pd_num1(str *o, double v)
{
	long t = (long)(v * 10 + (v < 0 ? -0.5 : 0.5));

	if (t < 0) {
		s_ch(o, '-');
		t = -t;
	}
	s_num(o, t / 10);
	s_ch(o, '.');
	s_num(o, t % 10);
}

/* The document a word names, complaining when it names none. */
pd_doc *pd_arg(const char *sub, const char *t)
{
	size_t i;
	int id = t ? atoi(t) : 0;

	for (i = 0; i < pd_docs.n; i++)
		if (((pd_doc *)pd_docs.p[i])->id == id)
			return pd_docs.p[i];
	lg(HIBR_LERR, "pdf %s: %s: no such document", sub, t ? t : "");
	return 0;
}

/* Close a document, and remove the last view it wrote. */
void pd_drop(pd_doc *d)
{
	size_t i;

	if (d->last) {
		unlink(d->last);
		free(d->last);
	}
	if (d->doc)
		pd.unref(d->doc);
	free(d->pw);
	free(d->ph);
	for (i = 0; i < pd_docs.n; i++)
		if (pd_docs.p[i] == (void *)d) {
			pd_docs.p[i] = pd_docs.p[pd_docs.n - 1];
			pd_docs.n--;
			break;
		}
	free(d);
}

/* Open a file, given as a path; its pages' sizes are read once. */
int pd_open(sh *s, const char *path, const char *pw)
{
	pd_gerr *e = 0;
	char *uri, *abs;
	pd_doc *d;
	str o;
	int i;
	void *pg;

	if (pd_load() != HIBR_OK)
		return HIBR_FAIL;
	abs = realpath(path, 0);
	if (!abs) {
		lg(HIBR_LERR, "pdf open: %s: no such file", path);
		return HIBR_FAIL;
	}
	uri = pd.touri(abs, 0, &e);
	free(abs);
	if (!uri) {
		lg(HIBR_LERR, "pdf open: %s: %s", path,
		   e && e->message ? e->message : "not a path");
		if (e)
			pd.errfree(e);
		return HIBR_FAIL;
	}
	d = xm(sizeof *d);
	memset(d, 0, sizeof *d);
	d->doc = pd.open(uri, pw, &e);
	pd.gfree(uri);
	if (!d->doc) {
		lg(HIBR_LERR, "pdf open: %s: %s", path,
		   e && e->message ? e->message : "cannot be read");
		if (e)
			pd.errfree(e);
		free(d);
		return HIBR_FAIL;
	}
	d->n = pd.npages(d->doc);
	d->pw = xm(sizeof *d->pw * (size_t)(d->n > 0 ? d->n : 1));
	d->ph = xm(sizeof *d->ph * (size_t)(d->n > 0 ? d->n : 1));
	for (i = 0; i < d->n; i++) {
		d->pw[i] = 612;
		d->ph[i] = 792;
		pg = pd.page(d->doc, i);
		if (pg) {
			pd.size(pg, &d->pw[i], &d->ph[i]);
			pd.unref(pg);
		}
	}
	d->id = pd_next++;
	v_add(&pd_docs, d);
	lg(HIBR_LDBG, "pdf: %s open as %d, %d pages", path, d->id, d->n);
	s_init(&o);
	s_num(&o, d->id);
	pd_ret(s, o.p);
	s_free(&o);
	return HIBR_OK;
}

/* Where page p begins in the laid-out document, in pixels at zoom z, and
   how tall the whole document is. */
double pd_top(const pd_doc *d, int p, double z)
{
	double y = 0;
	int i;

	for (i = 0; i < p && i < d->n; i++)
		y += d->ph[i] * z + PD_GAP;
	return y;
}

/* The widest page, in points, which is what a document is centred by. */
double pd_widest(const pd_doc *d)
{
	double w = 0;
	int i;

	for (i = 0; i < d->n; i++)
		if (d->pw[i] > w)
			w = d->pw[i];
	return w;
}

/* The page under a document pixel, and the point on it -- top-left origin --
   or -1 for the gap or the margin. */
int pd_at(const pd_doc *d, double z, double vw, double x, double y,
	  double *px, double *py)
{
	double t = 0, h, left;
	int i;

	for (i = 0; i < d->n; i++) {
		h = d->ph[i] * z;
		if (y >= t && y < t + h) {
			left = (vw - d->pw[i] * z) / 2;
			if (left < 0)
				left = 0;
			*px = (x - left) / z;
			*py = (y - t) / z;
			if (*px < 0 || *px > d->pw[i])
				return -1;
			return i;
		}
		t += h + PD_GAP;
	}
	return -1;
}

/* A rectangle on a page, in top-left points, filled on the view. */
void pd_mark(void *cr, const pd_doc *d, double z, double vw, double top,
	     double left, int p, double x1, double y1, double x2, double y2,
	     double r, double g, double b)
{
	double pl;

	if (p < 0 || p >= d->n)
		return;
	pl = (vw - d->pw[p] * z) / 2;
	if (pl < 0)
		pl = 0;
	pd.rgba(cr, r, g, b, 0.35);
	pd.rect(cr, pl - left + x1 * z, pd_top(d, p, z) - top + y1 * z,
		(x2 - x1) * z, (y2 - y1) * z);
	pd.fill(cr);
}

/* The viewport: w by h pixels of the document at zoom z, from top and left,
   with marks painted in, written as a PPM of its own name in dir (the img
   module's caches go by a file's whole-second mtime, so a view is never
   written over another of the same name). Answers the file, the page being
   read -- the one a quarter of the way down the view -- and the document's
   height and width in pixels. */
int pd_view(sh *s, int ac, char **av)
{
	pd_doc *d;
	int w, h, i, p, x, y, st, nm = 0;
	double z, top, left, vw, t, ph;
	void *surf, *cr, *pg;
	unsigned char *px, *row;
	str o, out;
	FILE *f;
	unsigned bg = 0x505050;

	if (ac < 9) {
		lg(HIBR_LERR, "usage: pdf view ID DIR W H ZOOM TOP LEFT "
			      "[-b rrggbb] [-k|-s|-c page x1 y1 x2 y2]...");
		return 2;
	}
	d = pd_arg("view", av[2]);
	if (!d)
		return HIBR_FAIL;
	w = atoi(av[4]);
	h = atoi(av[5]);
	z = atof(av[6]);
	top = atof(av[7]);
	left = atof(av[8]);
	if (w < 1 || h < 1 || z <= 0 || (long)w * h > PD_MAXPX) {
		lg(HIBR_LERR, "pdf view: %dx%d at %s is not a view", w, h, av[6]);
		return 2;
	}
	for (i = 9; i + 1 < ac; i++)
		if (!strcmp(av[i], "-b"))
			bg = (unsigned)strtoul(av[++i], 0, 16);
	vw = pd_widest(d) * z;
	if (vw < w)
		vw = w;
	surf = pd.surf(1, w, h);
	cr = pd.cr(surf);
	pd.rgb(cr, ((bg >> 16) & 255) / 255.0, ((bg >> 8) & 255) / 255.0,
	       (bg & 255) / 255.0);
	pd.paint(cr);
	for (p = 0; p < d->n; p++) {
		t = pd_top(d, p, z) - top;
		ph = d->ph[p] * z;
		if (t >= h)
			break;
		if (t + ph <= 0)
			continue;
		pd.save(cr);
		x = (int)((vw - d->pw[p] * z) / 2);
		if (x < 0)
			x = 0;
		pd.translate(cr, x - left, t);
		pd.scale(cr, z, z);
		pd.rgb(cr, 1, 1, 1);
		pd.rect(cr, 0, 0, d->pw[p], d->ph[p]);
		pd.fill(cr);
		pg = pd.page(d->doc, p);
		if (pg) {
			pd.render(pg, cr);
			pd.unref(pg);
		}
		pd.restore(cr);
	}
	for (i = 9; i + 5 < ac; i++) {
		double r = 1, g = 0.85, b = 0;

		if (!strcmp(av[i], "-s")) {
			r = 0.2;
			g = 0.45;
			b = 1;
		} else if (!strcmp(av[i], "-c")) {
			r = 1;
			g = 0.45;
			b = 0;
		} else if (strcmp(av[i], "-k")) {
			continue;
		}
		pd_mark(cr, d, z, vw, top, left, atoi(av[i + 1]) - 1,
			atof(av[i + 2]), atof(av[i + 3]), atof(av[i + 4]),
			atof(av[i + 5]), r, g, b);
		i += 5;
	}
	pd.flush(surf);
	px = pd.data(surf);
	st = pd.stride(surf);
	s_init(&out);
	s_cat(&out, av[3]);
	s_cat(&out, "/pdf-");
	s_num(&out, d->id);
	s_ch(&out, '-');
	s_num(&out, ++d->seq);
	s_cat(&out, ".ppm");
	f = fopen(out.p, "wb");
	if (!f) {
		lg(HIBR_LERR, "pdf view: %s: cannot write", out.p);
		pd.crfree(cr);
		pd.surffree(surf);
		s_free(&out);
		return HIBR_FAIL;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	row = xm((size_t)w * 3);
	for (y = 0; y < h; y++) {
		unsigned char *q = px + (size_t)y * st;

		for (x = 0; x < w; x++) {
			unsigned v = (unsigned)q[x * 4] | ((unsigned)q[x * 4 + 1] << 8) |
				     ((unsigned)q[x * 4 + 2] << 16);

			row[x * 3] = (unsigned char)(v >> 16);
			row[x * 3 + 1] = (unsigned char)(v >> 8);
			row[x * 3 + 2] = (unsigned char)v;
		}
		if (fwrite(row, 1, (size_t)w * 3, f) != (size_t)w * 3)
			nm = 1;
	}
	free(row);
	if (fclose(f) != 0)
		nm = 1;
	pd.crfree(cr);
	pd.surffree(surf);
	if (nm) {
		unlink(out.p);
		lg(HIBR_LERR, "pdf view: %s: short write", out.p);
		s_free(&out);
		return HIBR_FAIL;
	}
	if (d->last) {
		unlink(d->last);
		free(d->last);
	}
	d->last = xs(out.p);
	p = 0;
	while (p + 1 < d->n && pd_top(d, p + 1, z) <= top + h / 4.0)
		p++;
	s_init(&o);
	s_cat(&o, out.p);
	s_ch(&o, ' ');
	s_num(&o, p + 1);
	s_ch(&o, ' ');
	s_num(&o, (long)(pd_top(d, d->n, z) - (d->n ? PD_GAP : 0)));
	s_ch(&o, ' ');
	s_num(&o, (long)vw);
	pd_ret(s, o.p);
	s_free(&o);
	s_free(&out);
	return HIBR_OK;
}

/* A link's target as words: `page N Y` (1-based, Y in top-left points) or
   `uri U`; empty when it is neither. */
void pd_target(pd_doc *d, pd_act *a, str *o)
{
	pd_dest *de, *found = 0;

	if (!a)
		return;
	if (a->type == PD_URI && a->p) {
		s_cat(o, "uri ");
		s_cat(o, (const char *)a->p);
		return;
	}
	if (a->type != PD_GOTO || !a->p)
		return;
	de = a->p;
	if (de->type == PD_NAMED && de->named) {
		found = pd.finddest(d->doc, de->named);
		if (!found)
			return;
		de = found;
	}
	if (de->page >= 1 && de->page <= d->n) {
		s_cat(o, "page ");
		s_num(o, de->page);
		s_ch(o, ' ');
		pd_num1(o, de->top > 0 ? d->ph[de->page - 1] - de->top : 0);
	}
	if (found)
		pd.destfree(found);
}

/* The verbs that read a page: text, find, select, link. */
int pd_page(sh *s, const char *sub, int ac, char **av)
{
	pd_doc *d;
	void *pg;
	int p;
	str o;
	char *t;
	pd_list *l, *k;
	pd_rect r;

	d = pd_arg(sub, ac > 2 ? av[2] : 0);
	if (!d)
		return HIBR_FAIL;
	s_init(&o);
	if (!strcmp(sub, "find")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: pdf find ID TEXT");
			s_free(&o);
			return 2;
		}
		for (p = 0; p < d->n; p++) {
			pg = pd.page(d->doc, p);
			if (!pg)
				continue;
			l = pd.find(pg, av[3]);
			for (k = l; k; k = k->next) {
				pd_rect *q = k->data;

				if (o.n)
					s_ch(&o, '\n');
				s_num(&o, p + 1);
				s_ch(&o, ' ');
				pd_num1(&o, q->x1);
				s_ch(&o, ' ');
				pd_num1(&o, d->ph[p] - q->y2);
				s_ch(&o, ' ');
				pd_num1(&o, q->x2);
				s_ch(&o, ' ');
				pd_num1(&o, d->ph[p] - q->y1);
				pd.rectfree(q);
			}
			pd.listfree(l);
			pd.unref(pg);
		}
		pd_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	p = ac > 3 ? atoi(av[3]) : 0;
	if (p < 1 || p > d->n) {
		lg(HIBR_LERR, "pdf %s: %s: no such page", sub, ac > 3 ? av[3] : "");
		s_free(&o);
		return HIBR_FAIL;
	}
	pg = pd.page(d->doc, p - 1);
	if (!pg) {
		s_free(&o);
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "text")) {
		t = pd.text(pg);
		pd_ret(s, t ? t : "");
		pd.gfree(t);
	} else if (!strcmp(sub, "select") && ac > 7) {
		r.x1 = atof(av[4]);
		r.y1 = atof(av[5]);
		r.x2 = atof(av[6]);
		r.y2 = atof(av[7]);
		t = pd.seltext(pg, 0, &r);
		pd_ret(s, t ? t : "");
		pd.gfree(t);
	} else if (!strcmp(sub, "link") && ac > 5) {
		double x = atof(av[4]), y = d->ph[p - 1] - atof(av[5]);

		l = pd.links(pg);
		for (k = l; k; k = k->next) {
			pd_lmap *m = k->data;

			if (x >= m->area.x1 && x <= m->area.x2 &&
			    y >= m->area.y1 && y <= m->area.y2) {
				pd_target(d, m->action, &o);
				break;
			}
		}
		pd.freelinks(l);
		pd.unref(pg);
		if (!o.n) {
			s_free(&o);
			return HIBR_FAIL;
		}
		pd_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	} else {
		lg(HIBR_LERR, "usage: pdf text ID PAGE | select ID PAGE x1 y1 "
			      "x2 y2 | link ID PAGE x y");
		pd.unref(pg);
		s_free(&o);
		return 2;
	}
	pd.unref(pg);
	s_free(&o);
	return HIBR_OK;
}

/* The outline, one entry a line: depth, page, title, tab-separated. */
void pd_walk(pd_doc *d, void *it, int depth, str *o)
{
	void *c, *a;
	str t;

	do {
		a = pd.itact(it);
		if (a) {
			pd_act *x = a;

			s_init(&t);
			pd_target(d, x, &t);
			if (o->n)
				s_ch(o, '\n');
			s_num(o, depth);
			s_ch(o, '\t');
			if (t.n > 5 && !strncmp(t.p, "page ", 5)) {
				s_add(o, t.p + 5, strcspn(t.p + 5, " "));
			} else {
				s_ch(o, '0');
			}
			s_ch(o, '\t');
			s_cat(o, x->title ? x->title : "");
			s_free(&t);
			pd.actfree(a);
		}
		c = pd.itchild(it);
		if (c) {
			pd_walk(d, c, depth + 1, o);
			pd.itfree(c);
		}
	} while (pd.itnext(it));
}

/* The zoom a mode means for a viewport w by h pixels -- fitw, the widest
   page across it; fitp, the largest page whole in it; or a percentage,
   where 100 is 96 pixels an inch -- as the zoom and its percentage. An
   app's arithmetic is in whole numbers, so this is worked out here. */
void pd_zoom(pd_doc *d, const char *mode, double w, double h, str *o)
{
	double z, mw = pd_widest(d), mh = 0, pct;
	int i;

	for (i = 0; i < d->n; i++)
		if (d->ph[i] > mh)
			mh = d->ph[i];
	if (mw <= 0)
		mw = 612;
	if (mh <= 0)
		mh = 792;
	if (!strcmp(mode, "fitw")) {
		z = w / mw;
	} else if (!strcmp(mode, "fitp")) {
		z = w / mw;
		if (h / mh < z)
			z = h / mh;
	} else {
		pct = atof(mode);
		if (pct < 10)
			pct = 10;
		if (pct > 800)
			pct = 800;
		z = pct / 100 * 96 / 72;
	}
	if (z < 0.05)
		z = 0.05;
	{
		long t = (long)(z * 10000 + 0.5);

		s_num(o, t / 10000);
		s_ch(o, '.');
		s_num(o, t / 1000 % 10);
		s_num(o, t / 100 % 10);
		s_num(o, t / 10 % 10);
		s_num(o, t % 10);
	}
	s_ch(o, ' ');
	s_num(o, (long)(z * 72 / 96 * 100 + 0.5));
}

/* pdf ready|open|close|pages|size|title|view|zoom|at|where|text|find|select|link|outline */
int m_pdf(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	pd_doc *d;
	str o;
	int p;
	char *t;
	void *it;

	if (!strcmp(sub, "open")) {
		if (ac < 3) {
			lg(HIBR_LERR, "usage: pdf open FILE [password]");
			return 2;
		}
		return pd_open(s, av[2], ac > 3 ? av[3] : 0);
	}
	if (!strcmp(sub, "view"))
		return pd_view(s, ac, av);
	if (!strcmp(sub, "ready"))
		return pd_load();
	if (!strcmp(sub, "text") || !strcmp(sub, "find") ||
	    !strcmp(sub, "select") || !strcmp(sub, "link"))
		return pd_page(s, sub, ac, av);
	if (!*sub || (strcmp(sub, "close") && strcmp(sub, "pages") &&
		      strcmp(sub, "size") && strcmp(sub, "title") &&
		      strcmp(sub, "at") && strcmp(sub, "where") &&
		      strcmp(sub, "zoom") && strcmp(sub, "outline"))) {
		lg(HIBR_LERR, "usage: pdf open|close|pages|size|title|view|zoom|"
			      "at|where|text|find|select|link|outline ...");
		return 2;
	}
	d = pd_arg(sub, ac > 2 ? av[2] : 0);
	if (!d)
		return HIBR_FAIL;
	s_init(&o);
	if (!strcmp(sub, "close")) {
		pd_drop(d);
	} else if (!strcmp(sub, "pages")) {
		s_num(&o, d->n);
		pd_ret(s, o.p);
	} else if (!strcmp(sub, "size")) {
		p = ac > 3 ? atoi(av[3]) : 1;
		if (p < 1 || p > d->n) {
			s_free(&o);
			return HIBR_FAIL;
		}
		pd_num1(&o, d->pw[p - 1]);
		s_ch(&o, ' ');
		pd_num1(&o, d->ph[p - 1]);
		pd_ret(s, o.p);
	} else if (!strcmp(sub, "title")) {
		t = pd.title(d->doc);
		pd_ret(s, t ? t : "");
		pd.gfree(t);
	} else if (!strcmp(sub, "at")) {
		double px, py;

		if (ac < 7) {
			lg(HIBR_LERR, "usage: pdf at ID ZOOM VIEWW X Y");
			s_free(&o);
			return 2;
		}
		{
			double vw = pd_widest(d) * atof(av[3]);

			if (vw < atof(av[4]))
				vw = atof(av[4]);
			p = pd_at(d, atof(av[3]), vw, atof(av[5]), atof(av[6]),
				  &px, &py);
		}
		if (p < 0) {
			s_free(&o);
			return HIBR_FAIL;
		}
		s_num(&o, p + 1);
		s_ch(&o, ' ');
		pd_num1(&o, px);
		s_ch(&o, ' ');
		pd_num1(&o, py);
		pd_ret(s, o.p);
	} else if (!strcmp(sub, "where")) {
		if (ac < 6) {
			lg(HIBR_LERR, "usage: pdf where ID ZOOM PAGE Y");
			s_free(&o);
			return 2;
		}
		p = atoi(av[4]);
		if (p < 1 || p > d->n) {
			s_free(&o);
			return HIBR_FAIL;
		}
		{
			double y = pd_top(d, p - 1, atof(av[3])) +
				   atof(av[5]) * atof(av[3]);
			long t = (long)y;

			if (t < y)
				t++;
			s_num(&o, t);
		}
		pd_ret(s, o.p);
	} else if (!strcmp(sub, "zoom")) {
		if (ac < 6) {
			lg(HIBR_LERR, "usage: pdf zoom ID fitw|fitp|PCT W H");
			s_free(&o);
			return 2;
		}
		pd_zoom(d, av[3], atof(av[4]), atof(av[5]), &o);
		pd_ret(s, o.p);
	} else if (!strcmp(sub, "outline")) {
		it = pd.itnew(d->doc);
		if (it) {
			pd_walk(d, it, 0, &o);
			pd.itfree(it);
		}
		pd_ret(s, o.p ? o.p : "");
	}
	s_free(&o);
	return HIBR_OK;
}

/* Every document closed when the module is dropped. */
void pd_fini(sh *s)
{
	(void)s;
	while (pd_docs.n)
		pd_drop(pd_docs.p[pd_docs.n - 1]);
	v_free(&pd_docs);
}

const hibr_bi pdf_bi[] = {
	{ "pdf", m_pdf, "read a PDF: its pages as a picture, its text, links and outline" },
	HIBR_BI_END
};

HIBR_MODULE("pdf", "0.1", "PDF documents through Poppler, loaded at run time",
	    pdf_bi, 0, pd_fini);
