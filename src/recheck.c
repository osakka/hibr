#ifndef __TINYC__
#include <regex.h>
#define HIBR_RE_NOPROTO
#include "re.h"

_Static_assert(sizeof(re_off) == sizeof(regoff_t),
	       "include/re.h: re_off is not this libc's regoff_t");
_Static_assert(sizeof(re_m) == sizeof(regmatch_t),
	       "include/re.h: re_m is not this libc's regmatch_t");
_Static_assert(sizeof(re_t) >= sizeof(regex_t),
	       "include/re.h: HIBR_REG_SLOP is smaller than regex_t");
_Static_assert(HIBR_REG_EXTENDED == REG_EXTENDED &&
	       HIBR_REG_ICASE == REG_ICASE && HIBR_REG_NOTBOL == REG_NOTBOL,
	       "include/re.h: a regex flag differs from this libc's");
#endif

/* Checks include/re.h against the real <regex.h>, where one can be read. */
int re_checked(void)
{
	return 1;
}
