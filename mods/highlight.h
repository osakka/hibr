#ifndef HIBR_HIGHLIGHT_H
#define HIBR_HIGHLIGHT_H

#include "hibr.h"

/* The interface a lexical colourer offers through hibr_provide, under the
   name "highlight". The cat module offers it; anything that draws text can
   ask for it rather than growing a second copy of the same tables. */

#ifndef HL_API_VER
#define HL_API_VER 2u
#endif

typedef struct hl_api hl_api;
struct hl_api {
	/* The language a file name implies, or null when none is known. */
	const char *(*lang)(const char *name);
	/* One line, coloured with ANSI escapes. `state` carries whatever runs
	   across lines -- a block comment, a string, a heredoc and the word
	   that ends it -- and is the colourer's own to read and write: the
	   caller starts it empty (`n = 0` puts it back), keeps it between
	   calls and frees it. */
	void (*line)(str *out, const char *p, size_t n, const char *lang,
		     str *state);
};

#endif
