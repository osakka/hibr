#ifndef HIBR_B64_H
#define HIBR_B64_H

#include "hibr.h"

/* Base64, both directions, compiled into each module that needs it the
   way inflate.c is (Gitea #356): the console's pictures, hold's and the
   emulator's OSC 52, Mail's MIME, fetch's Basic login, the vault and the
   web module each wrote their own, nine copies of one algorithm.

   The decoder takes the standard alphabet and the URL-safe one (- and _)
   alike, passes over line breaks, and stops at the first =. A character
   outside both alphabets is skipped, or with strict refuses the whole text
   with -1; otherwise the answer is how many bytes were appended. */
void b64_enc(str *o, const unsigned char *p, size_t n);
long b64_dec(str *o, const char *p, size_t n, int strict);

#endif
