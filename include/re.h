#ifndef HIBR_RE_H
#define HIBR_RE_H

#include <stddef.h>

#ifndef HIBR_REG_EXTENDED
#define HIBR_REG_EXTENDED 1
#endif
#ifndef HIBR_REG_ICASE
#define HIBR_REG_ICASE 2
#endif
#ifndef HIBR_REG_NOTBOL
#define HIBR_REG_NOTBOL 1
#endif
#ifndef HIBR_REG_SLOP
#define HIBR_REG_SLOP 256
#endif

typedef int re_off;
typedef struct re_m { re_off so, eo; } re_m;
typedef struct re_t { char opaque[HIBR_REG_SLOP]; } re_t;

int regcomp(re_t *re, const char *pat, int flags);
int regexec(const re_t *re, const char *s, size_t n, re_m *m, int flags);
size_t regerror(int code, const re_t *re, char *buf, size_t n);
void regfree(re_t *re);

#endif
