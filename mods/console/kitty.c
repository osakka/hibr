/* kitty -- a picture as the kitty graphics protocol's own escape.
 *
 * The other half of ADR 0037. A sixel is paint: the terminal lays the
 * bitmap down and whatever is written over those cells afterwards wins. A
 * kitty image is an *object*: it is transmitted once with an id, placed, and
 * stays on screen above the text until something deletes it. So everything
 * here comes in pairs -- a region placed is a region that must later be
 * deleted, which is why image.c keeps an id per region and queues a delete
 * on every path that drops or forgets one.
 *
 * The protocol is an APC string, ESC _ G <control> ; <base64 payload> ESC \,
 * and what is sent is:
 *
 *   a=T   transmit and display in one go, at the cursor
 *   f=24  three bytes a pixel, no alpha, which is what every caller has
 *   o=z   the payload is a zlib stream of those bytes rather than the bytes
 *         (Gitea #129): the owner's own wallpaper is 2.11 MB of base64 raw
 *         and 0.62 MB this way, which matters because hold re-emits every
 *         picture to every client that attaches, so a reattach over a link
 *         was pushing all of it before the screen appeared
 *   f=100 or the payload is a PNG, which is 0.45 MB of the same picture --
 *         better again, and not the default, because a terminal that takes
 *         f=24 and not f=100 draws nothing and q=2 means it cannot say so.
 *         That is the 0.99.72 shape exactly: every picture a blank
 *         rectangle, nothing erroring. `console imgcomp png` asks for it
 *   s=,v= the payload's own width and height in pixels; a PNG carries its
 *         own, so they are left out for f=100 rather than said twice
 *   c=,r= the rectangle in cells, so the terminal pins the picture to it
 *         however its own cell size rounds -- sixel has to be scaled to the
 *         pixel exactly, this does not
 *   C=1   leave the cursor where it was
 *   q=2   answer nothing, not even an error: the reply would arrive in the
 *         same stream the key decoder owns, and its ESC would land in the
 *         Alt/Escape window
 *   i=    the image's id, so it can be deleted or replaced later
 *   z=    below the text, for a picture text is drawn over. The protocol
 *         has two layers below the text, not one: a negative z is drawn
 *         under the glyphs but *over* each cell's own background colour,
 *         and only a z below INT32_MIN/2 is "drawn under cells with
 *         non-default background colors" (the protocol's own words). A
 *         wallpaper wants the second: the desktop paints a window's face
 *         in a real colour and blanks only the cells the picture should
 *         show through, which is the whole of `dt_wall`'s design -- so at
 *         z=-1 every window, the menu bar and every dialog had its fill
 *         composited away and only the glyphs survived, which is what a
 *         live desktop showed (Gitea #175). CN_ZUNDER is one below the
 *         threshold, not INT32_MIN, so there is still room to put
 *         something under it later.
 *   m=1   more chunks to come; the payload is split at 4096 base64 bytes,
 *         which is what the protocol allows in one escape
 *
 * Ids are ours and nobody else's: a terminal is shared, and another program
 * using the same protocol with the same id would replace our picture. The
 * band is taken from the process id, with the counter in the low bits.
 */
#include "cn.h"
#include "../inflate.h"
#include "../png.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef KT_CHUNK
#define KT_CHUNK 4096
#endif

static unsigned kt_base, kt_n;

/* The next image id, in this process's own band. */
unsigned kt_id(void)
{
	if (!kt_base)
		kt_base = 0x48000000u | (((unsigned)getpid() & 0x3FFFu) << 14);
	kt_n = (kt_n + 1) & 0x3FFFu;
	return kt_base + (kt_n ? kt_n : 1);
}

/* A picture: iw by ih pixels of RGB, to be shown in cols by rows cells at
   wherever the cursor is when this arrives. under puts it below the text.
   comp is CN_COMP_NONE, CN_COMP_ZLIB or CN_COMP_PNG -- what image.c decided
   from what kind of picture this is, since compressing a film's frame costs
   more than its bytes save. */
void kt_encode(const unsigned char *rgb, int iw, int ih, int cols, int rows,
	       unsigned id, int under, int comp, str *o)
{
	str b, c;
	size_t i, n, k;
	const unsigned char *pay = rgb;
	size_t payn = (size_t)iw * ih * 3;

	s_init(&c);
	if (comp == CN_COMP_PNG)
		pw_write(rgb, iw, ih, &c);
	else if (comp == CN_COMP_ZLIB)
		def_zlib(rgb, payn, &c);
	if (c.n) {
		pay = (const unsigned char *)c.p;
		payn = c.n;
	} else {
		comp = CN_COMP_NONE;
	}
	s_init(&b);
	cn_b64(&b, pay, payn);
	s_free(&c);
	n = b.n;
	if (!n) {
		s_free(&b);
		return;
	}
	for (i = 0; i < n; i += k) {
		k = n - i > KT_CHUNK ? (size_t)KT_CHUNK : n - i;
		s_cat(o, "\033_G");
		if (!i) {
			s_cat(o, comp == CN_COMP_PNG ? "a=T,f=100,q=2,C=1,i="
						     : "a=T,f=24,q=2,C=1,i=");
			s_num(o, id);
			if (comp == CN_COMP_ZLIB)
				s_cat(o, ",o=z");
			if (comp != CN_COMP_PNG) {
				s_cat(o, ",s=");
				s_num(o, iw);
				s_cat(o, ",v=");
				s_num(o, ih);
			}
			s_cat(o, ",c=");
			s_num(o, cols);
			s_cat(o, ",r=");
			s_num(o, rows);
			if (under) {
				s_cat(o, ",z=");
				s_num(o, (long)CN_ZUNDER);
			}
		} else {
			s_cat(o, "q=2");
		}
		s_cat(o, i + k < n ? ",m=1;" : ",m=0;");
		s_add(o, b.p + i, k);
		s_cat(o, "\033\\");
	}
	s_free(&b);
}

/* Delete one image: its placements and the data behind them. */
void kt_del(str *o, unsigned id)
{
	s_cat(o, "\033_Ga=d,d=I,q=2,i=");
	s_num(o, id);
	s_cat(o, "\033\\");
}

/* Delete everything this terminal is holding for us. Sent on the way out:
   leaving the alternate screen is not promised to take images with it. */
void kt_delall(str *o)
{
	s_cat(o, "\033_Ga=d,d=A,q=2\033\\");
}
