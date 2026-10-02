#define _GNU_SOURCE

#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TM_SAVEVER
#define TM_SAVEVER 1
#endif

/* One field of a terminal kept in a save, by name, so a newer loader skips
   what it does not know and an older save leaves a new field at its
   default. */
typedef struct tm_fld {
	const char *nm;
	void *p;
	int u;
} tm_fld;

/* The fields a save keeps: where the cursor is, the pen, the modes a
   program set and expects to find still set. */
int tm_fields(tm_t *t, tm_fld *f)
{
	int n = 0, k;
	static const char *sv[2][9] = {
		{ "s0set", "s0r", "s0c", "s0wrap", "s0om", "s0gl", "s0fg",
		  "s0bg", "s0attr" },
		{ "s1set", "s1r", "s1c", "s1wrap", "s1om", "s1gl", "s1fg",
		  "s1bg", "s1attr" }
	};

	f[n++] = (tm_fld){ "cr", &t->cr, 0 };
	f[n++] = (tm_fld){ "cc", &t->cc, 0 };
	f[n++] = (tm_fld){ "top", &t->top, 0 };
	f[n++] = (tm_fld){ "bot", &t->bot, 0 };
	f[n++] = (tm_fld){ "fg", &t->fg, 1 };
	f[n++] = (tm_fld){ "bg", &t->bg, 1 };
	f[n++] = (tm_fld){ "attr", &t->attr, 1 };
	f[n++] = (tm_fld){ "dfg", &t->dfg, 1 };
	f[n++] = (tm_fld){ "dbg", &t->dbg, 1 };
	f[n++] = (tm_fld){ "dattr", &t->dattr, 1 };
	f[n++] = (tm_fld){ "vis", &t->vis, 0 };
	f[n++] = (tm_fld){ "wrapnext", &t->wrapnext, 0 };
	f[n++] = (tm_fld){ "autowrap", &t->autowrap, 0 };
	f[n++] = (tm_fld){ "gl", &t->gl, 0 };
	f[n++] = (tm_fld){ "ckm", &t->ckm, 0 };
	f[n++] = (tm_fld){ "kpam", &t->kpam, 0 };
	f[n++] = (tm_fld){ "om", &t->om, 0 };
	f[n++] = (tm_fld){ "irm", &t->irm, 0 };
	f[n++] = (tm_fld){ "lnm", &t->lnm, 0 };
	f[n++] = (tm_fld){ "focus", &t->focus, 0 };
	f[n++] = (tm_fld){ "mmode", &t->mmode, 0 };
	f[n++] = (tm_fld){ "msgr", &t->msgr, 0 };
	f[n++] = (tm_fld){ "bpaste", &t->bpaste, 0 };
	f[n++] = (tm_fld){ "cshape", &t->cshape, 0 };
	f[n++] = (tm_fld){ "link", &t->link, 1 };
	f[n++] = (tm_fld){ "hasrgb", &t->hasrgb, 0 };
	f[n++] = (tm_fld){ "rgbfg", &t->rgbfg, 1 };
	f[n++] = (tm_fld){ "rgbbg", &t->rgbbg, 1 };
	f[n++] = (tm_fld){ "sbmax", &t->sbmax, 0 };
	for (k = 0; k < 2; k++) {
		f[n++] = (tm_fld){ sv[k][0], &t->sv[k].set, 0 };
		f[n++] = (tm_fld){ sv[k][1], &t->sv[k].r, 0 };
		f[n++] = (tm_fld){ sv[k][2], &t->sv[k].c, 0 };
		f[n++] = (tm_fld){ sv[k][3], &t->sv[k].wrap, 0 };
		f[n++] = (tm_fld){ sv[k][4], &t->sv[k].om, 0 };
		f[n++] = (tm_fld){ sv[k][5], &t->sv[k].gl, 0 };
		f[n++] = (tm_fld){ sv[k][6], &t->sv[k].fg, 1 };
		f[n++] = (tm_fld){ sv[k][7], &t->sv[k].bg, 1 };
		f[n++] = (tm_fld){ sv[k][8], &t->sv[k].attr, 1 };
	}
	return n;
}

/* Write n cells, each field on its own, so a cell that later grows a field
   does not change what an older save means. */
void tm_wcells(FILE *f, const tm_cell *c, long n)
{
	long i;
	unsigned v[7];

	for (i = 0; i < n; i++) {
		v[0] = c[i].cp;
		v[1] = c[i].fg;
		v[2] = c[i].bg;
		v[3] = c[i].attr;
		v[4] = c[i].x;
		v[5] = c[i].link;
		v[6] = c[i].w;
		fwrite(v, sizeof v, 1, f);
	}
}

/* Read n cells written by tm_wcells; 0 if the file ends first. */
int tm_rcells(FILE *f, tm_cell *c, long n)
{
	long i;
	unsigned v[7];

	for (i = 0; i < n; i++) {
		if (fread(v, sizeof v, 1, f) != 1)
			return 0;
		memset(c + i, 0, sizeof c[i]);
		c[i].cp = v[0];
		c[i].fg = v[1];
		c[i].bg = v[2];
		c[i].attr = v[3];
		c[i].x = v[4];
		c[i].link = v[5];
		c[i].w = (unsigned char)v[6];
	}
	return 1;
}

/* Write a string as its length, a newline, then its bytes. */
void tm_wstr(FILE *f, const char *nm, const char *p, size_t n)
{
	fprintf(f, "%s %lu\n", nm, (unsigned long)n);
	if (n)
		fwrite(p, 1, n, f);
	fputc('\n', f);
}

/* Read the bytes of a string tm_wstr wrote, n of them, and its newline. */
char *tm_rstr(FILE *f, unsigned long n)
{
	char *p = xm(n + 1);

	if (n && fread(p, 1, n, f) != n) {
		free(p);
		return 0;
	}
	p[n] = 0;
	fgetc(f);
	return p;
}

/* Keep everything a terminal shows and every mode its program set, for a
   process that will exec and load it again. The program's own pty is not
   in it: that survives the exec by itself. */
int tm_savefile(tm_t *t, const char *path)
{
	FILE *f = fopen(path, "wb");
	tm_fld fl[64];
	int n, i;

	if (!f)
		return 0;
	fprintf(f, "hibr-term %d\n", TM_SAVEVER);
	fprintf(f, "size %d %d\n", t->rows, t->cols);
	fprintf(f, "inalt %d\n", t->inalt);
	fprintf(f, "gset %d %d\n", t->gset[0], t->gset[1]);
	n = tm_fields(t, fl);
	for (i = 0; i < n; i++)
		fprintf(f, "%s %ld\n", fl[i].nm, fl[i].u ?
			(long)*(unsigned *)fl[i].p : (long)*(int *)fl[i].p);
	fprintf(f, "s0g %d %d\n", t->sv[0].g[0], t->sv[0].g[1]);
	fprintf(f, "s1g %d %d\n", t->sv[1].g[0], t->sv[1].g[1]);
	tm_wstr(f, "title", t->title.p ? t->title.p : "", t->title.n);
	fprintf(f, "xp %d\n", t->xn);
	for (i = 0; i < t->xn; i++)
		tm_wstr(f, "x", t->xp[i], strlen(t->xp[i]));
	fprintf(f, "lk %d\n", t->lkn);
	for (i = 0; i < t->lkn; i++)
		tm_wstr(f, "l", t->lk[i], strlen(t->lk[i]));
	tm_wstr(f, "tabs", (const char *)t->tabs, (size_t)t->cols);
	fprintf(f, "grid %ld\n", (long)t->rows * t->cols);
	tm_wcells(f, t->g, (long)t->rows * t->cols);
	fputc('\n', f);
	if (t->alt) {
		fprintf(f, "alt %ld\n", (long)t->rows * t->cols);
		tm_wcells(f, t->alt, (long)t->rows * t->cols);
		fputc('\n', f);
	}
	fprintf(f, "sb %d\n", t->sbn);
	for (i = 0; i < t->sbn; i++) {
		tm_line *l = tm_sbline(t, i);
		fprintf(f, "%d\n", l->w);
		tm_wcells(f, l->c, l->w);
	}
	fprintf(f, "end\n");
	n = ferror(f);
	if (fclose(f) || n)
		return 0;
	lg(HIBR_LDBG, "terminal %d saved to %s: %d lines of scrollback", t->id,
	   path, t->sbn);
	return 1;
}

/* Clear any cell's reference into a pool it does not have an entry in, so
   a damaged save cannot send tm_ext or a link lookup past the end. */
void tm_sane(tm_t *t, tm_cell *c, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		if (c[i].x > (unsigned)t->xn)
			c[i].x = 0;
		if (c[i].link > (unsigned)t->lkn)
			c[i].link = 0;
		if (c[i].w > 2)
			c[i].w = 1;
	}
}

/* Load what tm_savefile wrote into t, a terminal of the saved size made
   for it. 0 if the file is not one, or ends early; t is then still a
   usable blank terminal. */
int tm_loadfile(tm_t *t, const char *path)
{
	FILE *f = fopen(path, "rb");
	tm_fld fl[64];
	char nm[64];
	long v, cnt, i;
	int n, k, a, b, ok = 0;
	unsigned long len;

	if (!f)
		return 0;
	n = tm_fields(t, fl);
	if (fscanf(f, "hibr-term %d\n", &a) != 1 || a != TM_SAVEVER) {
		lg(HIBR_LERR, "term: %s: not a terminal save this version reads",
		   path);
		fclose(f);
		return 0;
	}
	while (fscanf(f, "%63s", nm) == 1) {
		if (!strcmp(nm, "end")) {
			ok = 1;
			break;
		}
		if (!strcmp(nm, "size")) {
			if (fscanf(f, "%d %d\n", &a, &b) != 2 || a != t->rows ||
			    b != t->cols)
				break;
		} else if (!strcmp(nm, "inalt")) {
			if (fscanf(f, "%d\n", &a) != 1)
				break;
			t->inalt = !!a;
		} else if (!strcmp(nm, "gset") || !strcmp(nm, "s0g") ||
			   !strcmp(nm, "s1g")) {
			unsigned char *g = nm[0] == 'g' ? t->gset :
				nm[1] == '0' ? t->sv[0].g : t->sv[1].g;
			if (fscanf(f, "%d %d\n", &a, &b) != 2)
				break;
			g[0] = (unsigned char)a;
			g[1] = (unsigned char)b;
		} else if (!strcmp(nm, "title")) {
			char *p;
			if (fscanf(f, "%lu", &len) != 1 || fgetc(f) != '\n')
				break;
			p = tm_rstr(f, len);
			if (!p)
				break;
			t->title.n = 0;
			s_add(&t->title, p, len);
			free(p);
		} else if (!strcmp(nm, "xp") || !strcmp(nm, "lk")) {
			int xp = nm[0] == 'x';
			if (fscanf(f, "%ld\n", &cnt) != 1 || cnt < 0)
				break;
			for (i = 0; i < cnt; i++) {
				char *p;
				if (fscanf(f, "%63s %lu", nm, &len) != 2 ||
				    fgetc(f) != '\n')
					break;
				p = tm_rstr(f, len);
				if (!p)
					break;
				if (xp) {
					t->xp = xr(t->xp, (size_t)(t->xn + 1) *
						   sizeof *t->xp);
					t->xp[t->xn++] = p;
					t->xcap = t->xn;
				} else {
					t->lk = xr(t->lk, (size_t)(t->lkn + 1) *
						   sizeof *t->lk);
					t->lk[t->lkn++] = p;
					t->lkcap = t->lkn;
				}
			}
			if (i < cnt)
				break;
		} else if (!strcmp(nm, "tabs")) {
			char *p;
			if (fscanf(f, "%lu", &len) != 1 || fgetc(f) != '\n')
				break;
			p = tm_rstr(f, len);
			if (!p)
				break;
			if (len == (unsigned long)t->cols)
				memcpy(t->tabs, p, len);
			free(p);
		} else if (!strcmp(nm, "grid") || !strcmp(nm, "alt")) {
			tm_cell **g = nm[0] == 'g' ? &t->g : &t->alt;
			if (fscanf(f, "%ld", &cnt) != 1 || fgetc(f) != '\n' ||
			    cnt != (long)t->rows * t->cols)
				break;
			if (!*g)
				*g = xm((size_t)cnt * sizeof **g);
			if (!tm_rcells(f, *g, cnt))
				break;
			fgetc(f);
		} else if (!strcmp(nm, "sb")) {
			if (fscanf(f, "%ld\n", &cnt) != 1 || cnt < 0)
				break;
			for (i = 0; i < cnt; i++) {
				tm_cell *row;
				if (fscanf(f, "%d", &a) != 1 || fgetc(f) != '\n' ||
				    a < 0 || a > 65536)
					break;
				row = xm(((size_t)a + 1) * sizeof *row);
				if (!tm_rcells(f, row, a)) {
					free(row);
					break;
				}
				t->tot--;
				tm_push(t, row, a);
				free(row);
			}
			if (i < cnt)
				break;
		} else {
			if (fscanf(f, "%ld\n", &v) != 1)
				break;
			for (k = 0; k < n; k++)
				if (!strcmp(fl[k].nm, nm)) {
					if (fl[k].u)
						*(unsigned *)fl[k].p = (unsigned)v;
					else
						*(int *)fl[k].p = (int)v;
					break;
				}
			if (k == n)
				lg(HIBR_LDBG, "term: %s: skipped %s, not known here",
				   path, nm);
		}
	}
	fclose(f);
	if (t->cr < 0 || t->cr >= t->rows)
		t->cr = 0;
	if (t->cc < 0 || t->cc >= t->cols)
		t->cc = 0;
	if (t->top < 0 || t->bot >= t->rows || t->top > t->bot) {
		t->top = 0;
		t->bot = t->rows - 1;
	}
	if (t->inalt && !t->alt) {
		t->alt = xm((size_t)t->rows * t->cols * sizeof *t->alt);
		for (i = 0; i < (long)t->rows * t->cols; i++)
			tm_blank(t, t->alt + i);
	}
	tm_sane(t, t->g, (long)t->rows * t->cols);
	if (t->alt)
		tm_sane(t, t->alt, (long)t->rows * t->cols);
	for (i = 0; i < t->sbn; i++) {
		tm_line *l = tm_sbline(t, (int)i);
		tm_sane(t, l->c, l->w);
	}
	if (t->link > (unsigned)t->lkn)
		t->link = 0;
	t->tot = t->sbn;
	t->view = 0;
	if (!ok)
		lg(HIBR_LERR, "term: %s: ends early; kept what it had", path);
	else
		lg(HIBR_LDBG, "terminal %d loaded from %s", t->id, path);
	return ok;
}
