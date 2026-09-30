#ifndef HIBR_RE_H
#define HIBR_RE_H

#include <stddef.h>
#include <limits.h>

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

/* regoff_t as each C library defines it: an off_t, 64 bits, on macOS and
   the BSDs; an int on glibc; a long on musl. Getting it wrong halves or
   doubles every match the library writes -- src/recheck.c asserts it
   against the real <regex.h> wherever a compiler can read that. */
#ifndef HIBR_RE_OFF
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
	defined(__NetBSD__) || defined(__DragonFly__)
#define HIBR_RE_OFF long long
#elif defined(__GLIBC__)
#define HIBR_RE_OFF int
#else
#define HIBR_RE_OFF long
#endif
#endif

typedef HIBR_RE_OFF re_off;
typedef struct re_m { re_off so, eo; } re_m;
typedef struct re_t { char opaque[HIBR_REG_SLOP]; } re_t;

#ifndef HIBR_RE_NOPROTO
int regcomp(re_t *re, const char *pat, int flags);
int regexec(const re_t *re, const char *s, size_t n, re_m *m, int flags);
size_t regerror(int code, const re_t *re, char *buf, size_t n);
void regfree(re_t *re);
#endif

#endif
