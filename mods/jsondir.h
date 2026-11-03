#ifndef HIBR_JSONDIR_H
#define HIBR_JSONDIR_H

#include "hibr.h"

/* A catalogue of JSON files a module reads its data from, the way hcal
   reads calendars and salat reads methods: every *.json file in each
   folder of the colon-separated variable env, then in
   $XDG_CONFIG_HOME/hibr/<sub> (~/.config/hibr/<sub> without it), then in
   share/<sub>, each file's text handed to fn with its path. Compiled into
   each module that needs it, the way b64.c is; hcal and salat each wrote
   it out until 0.99.434 (Gitea #356). */
void jd_walk(const char *env, const char *sub, const char *share,
	     void (*fn)(const char *path, const char *text));

#endif
