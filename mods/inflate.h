#ifndef HIBR_INFLATE_H
#define HIBR_INFLATE_H

#include "hibr.h"

/* Deflate, as RFC 1951 defines it, with the two wrappers that carry it:
   zlib's (RFC 1950) and gzip's (RFC 1952). Written here rather than linked
   against libz, the same reasoning as the rest of hibr -- it is a few
   hundred lines and a dependency fewer -- and shared between the modules
   that need it: the prompt reads git's objects and packs with it, and the
   tar module reads a .tar.gz.

   Each takes the whole compressed stream and gives the whole of what it
   inflates, bounded by max so a crafted stream cannot be asked to allocate
   without end (0 for no bound). used, when given, is how many input bytes
   were consumed, which is what tells a pack entry where the next one
   starts. 1 when it inflated, 0 when the stream is not what it claims or
   runs out, with nothing written. */
int inf_raw(const unsigned char *in, size_t n, size_t max, str *out,
	    size_t *used);
int inf_zlib(const unsigned char *in, size_t n, size_t max, str *out,
	     size_t *used);
int inf_gzip(const unsigned char *in, size_t n, size_t max, str *out,
	     size_t *used);

#endif
