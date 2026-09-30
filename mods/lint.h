#ifndef HIBR_LINT_H
#define HIBR_LINT_H

#include "hibr.h"

#ifndef LI_API_VER
#define LI_API_VER 1u
#endif

/* What the lint module offers under "lint": explain reads a parsed script
   and reports each mistake it finds, returning how many it found. */
typedef struct li_api {
	int (*explain)(sh *s, node *n);
} li_api;

#endif
