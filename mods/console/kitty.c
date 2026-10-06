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
 *   s=,v= the payload's own width and height in pixels
 *   c=,r= the rectangle in cells, so the terminal pins the picture to it
 *         however its own cell size rounds -- sixel has to be scaled to the
 *         pixel exactly, this does not
 *   C=1   leave the cursor where it was
 *   q=2   answer nothing, not even an error: the reply would arrive in the
 *         same stream the key decoder owns, and its ESC would land in the
 *         Alt/Escape window
 *   i=    the image's id, so it can be deleted or replaced later
 *   z=-1  below the text, for a picture text is drawn over
 *   m=1   more chunks to come; the payload is split at 4096 base64 bytes,
 *         which is what the protocol allows in one escape
 *
 * Ids are ours and nobody else's: a terminal is shared, and another program
 * using the same protocol with the same id would replace our picture. The
 * band is taken from the process id, with the counter in the low bits.
 */
#include "cn.h"
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
   wherever the cursor is when this arrives. under puts it below the text. */
void kt_encode(const unsigned char *rgb, int iw, int ih, int cols, int rows,
	       unsigned id, int under, str *o)
{
	str b;
	size_t i, n, k;

	s_init(&b);
	cn_b64(&b, rgb, (size_t)iw * ih * 3);
	n = b.n;
	if (!n) {
		s_free(&b);
		return;
	}
	for (i = 0; i < n; i += k) {
		k = n - i > KT_CHUNK ? (size_t)KT_CHUNK : n - i;
		s_cat(o, "\033_G");
		if (!i) {
			s_cat(o, "a=T,f=24,q=2,C=1,i=");
			s_num(o, id);
			s_cat(o, ",s=");
			s_num(o, iw);
			s_cat(o, ",v=");
			s_num(o, ih);
			s_cat(o, ",c=");
			s_num(o, cols);
			s_cat(o, ",r=");
			s_num(o, rows);
			if (under)
				s_cat(o, ",z=-1");
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
