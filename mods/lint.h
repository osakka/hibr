#ifndef HIBR_LINT_H
#define HIBR_LINT_H

#include "hibr.h"

#ifndef LI_API_VER
#define LI_API_VER 2u
#endif

/* What the lint module offers under "lint": explain reads a parsed script
   and reports each mistake it finds, returning how many it found. src is
   the text it was parsed from, for what the tree does not keep: a comment
   on a line that excuses it (`# fork: reason`). */
typedef struct li_api {
	int (*explain)(sh *s, node *n, const char *src);
} li_api;

#endif
