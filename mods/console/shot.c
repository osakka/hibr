#define _GNU_SOURCE

#include "cn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern cn_grid cn_front;
extern unsigned cn_deffg, cn_defbg;
void cn_sgrcol(str *b, unsigned v, int bgp);
void cn_sgr(str *b, unsigned fg, unsigned bg, unsigned at);
void cn_enc(str *b, unsigned cp);

/* A colour as #rrggbb, for HTML: a palette index by xterm's own table, the
   terminal's default as whatever the desktop said its ink and face are. */
void cn_hexcol(str *b, unsigned v, int bgp)
{
	static const unsigned base[16] = {
		0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd,
		0x00cdcd, 0xe5e5e5, 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00,
		0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff
	};
	unsigned rgb, i;
	char t[8];

	if (v == DP_DEFAULT) {
		v = bgp ? cn_defbg : cn_deffg;
		if (v == DP_DEFAULT)
			v = DP_RGB | (bgp ? 0x000000u : 0xc0c0c0u);
	}
	if (v & DP_RGB) {
		rgb = v & 0xFFFFFFu;
	} else {
		i = v & 0xFF;
		if (i < 16) {
			rgb = base[i];
		} else if (i < 232) {
			unsigned k = i - 16, r = k / 36, g = (k / 6) % 6, bl = k % 6;
			r = r ? 55 + r * 40 : 0;
			g = g ? 55 + g * 40 : 0;
			bl = bl ? 55 + bl * 40 : 0;
			rgb = (r << 16) | (g << 8) | bl;
		} else {
			unsigned gr = 8 + (i - 232) * 10;
			rgb = (gr << 16) | (gr << 8) | gr;
		}
	}
	snprintf(t, sizeof t, "#%06x", rgb);
	s_cat(b, t);
}

/* One cell's text, its combining marks included; a blank for a cell that
   holds nothing. */
void cn_shotch(str *b, const cn_cell *c, int html)
{
	unsigned cp = c->cp ? c->cp : ' ';

	if (cp == 0xFFFFFFFFu)
		cp = ' ';
	if (html && (cp == '<' || cp == '>' || cp == '&')) {
		s_cat(b, cp == '<' ? "&lt;" : cp == '>' ? "&gt;" : "&amp;");
		return;
	}
	cn_enc(b, cp);
	if (c->ext)
		s_cat(b, c->ext);
}

/* The opening of an HTML run in one pen. */
void cn_span(str *b, const cn_cell *c)
{
	unsigned fg = c->fg, bg = c->bg, t;

	if (c->attr & DP_REV) {
		t = fg ? fg : (cn_deffg ? cn_deffg : DP_RGB | 0xc0c0c0u);
		fg = bg ? bg : (cn_defbg ? cn_defbg : DP_RGB);
		bg = t;
	}
	s_cat(b, "<span style=\"color:");
	cn_hexcol(b, fg, 0);
	s_cat(b, ";background:");
	cn_hexcol(b, bg, 1);
	if (c->attr & DP_BOLD)
		s_cat(b, ";font-weight:bold");
	if (c->attr & DP_ITAL)
		s_cat(b, ";font-style:italic");
	if (c->attr & DP_DIM)
		s_cat(b, ";opacity:0.6");
	if (c->attr & (DP_UNDER | DP_STRIKE)) {
		s_cat(b, ";text-decoration:");
		if (c->attr & DP_UNDER)
			s_cat(b, " underline");
		if (c->attr & DP_STRIKE)
			s_cat(b, " line-through");
	}
	s_cat(b, "\">");
}

/* Write what is on screen now, the rectangle given, as fmt: "ansi" the
   cells with their colours as a terminal draws them, "html" the same for a
   browser, "text" the characters alone. Reads the front grid -- what was
   last flushed, which is what is on screen -- so a caller flushes first. */
int cn_shot(const char *path, const char *fmt, int r0, int c0, int h, int w)
{
	int html = !strcmp(fmt, "html"), ansi = !strcmp(fmt, "ansi");
	int r, c, end;
	const cn_cell *k, *p;
	str b;
	FILE *f;

	if (!html && !ansi && strcmp(fmt, "text")) {
		lg(HIBR_LERR, "console shot: %s: not ansi, html or text", fmt);
		return 2;
	}
	if (!cn_front.c) {
		lg(HIBR_LERR, "console shot: nothing on screen");
		return HIBR_FAIL;
	}
	if (r0 < 0)
		r0 = 0;
	if (c0 < 0)
		c0 = 0;
	if (h <= 0 || r0 + h > cn_front.rows)
		h = cn_front.rows - r0;
	if (w <= 0 || c0 + w > cn_front.cols)
		w = cn_front.cols - c0;
	if (h <= 0 || w <= 0) {
		lg(HIBR_LERR, "console shot: that rectangle is off the screen");
		return HIBR_FAIL;
	}
	s_init(&b);
	if (html) {
		s_cat(&b, "<!doctype html>\n<meta charset=\"utf-8\">\n"
			  "<title>hibr</title>\n<pre style=\"font-family:"
			  "monospace;line-height:1.15;margin:0;display:inline-block;"
			  "background:");
		cn_hexcol(&b, DP_DEFAULT, 1);
		s_cat(&b, "\">");
	}
	for (r = r0; r < r0 + h; r++) {
		end = c0 + w;
		if (!ansi && !html)
			while (end > c0) {
				k = cn_front.c + r * cn_front.cols + end - 1;
				if ((k->cp && k->cp != ' ' && k->cp != 0xFFFFFFFFu) ||
				    k->cont)
					break;
				end--;
			}
		p = 0;
		for (c = c0; c < end; c++) {
			k = cn_front.c + r * cn_front.cols + c;
			if (k->cont)
				continue;
			if (k->w == 2 && c + 1 >= end) {
				s_ch(&b, ' ');
				continue;
			}
			if (ansi && (!p || p->fg != k->fg || p->bg != k->bg ||
				     p->attr != k->attr))
				cn_sgr(&b, k->fg, k->bg, k->attr);
			if (html && (!p || p->fg != k->fg || p->bg != k->bg ||
				     p->attr != k->attr)) {
				if (p)
					s_cat(&b, "</span>");
				cn_span(&b, k);
			}
			cn_shotch(&b, k, html);
			p = k;
		}
		if (html && p)
			s_cat(&b, "</span>");
		if (ansi)
			s_cat(&b, "\033[0m");
		s_ch(&b, '\n');
	}
	if (html)
		s_cat(&b, "</pre>\n");
	f = fopen(path, "w");
	if (f) {
		r = fwrite(b.p, 1, b.n, f) != b.n;
		r |= fclose(f) != 0;
	}
	if (!f || r) {
		lg(HIBR_LERR, "console shot: %s: cannot write it", path);
		s_free(&b);
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "screenshot of %dx%d at %d,%d written to %s as %s", h, w,
	   r0, c0, path, fmt);
	s_free(&b);
	return HIBR_OK;
}
