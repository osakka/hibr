#ifndef CT_H
#define CT_H

#include "hibr.h"

#ifndef CT_FLAGS
#define CT_NUM 1u
#define CT_NUMNB 2u
#define CT_SQUEEZE 4u
#define CT_ENDS 8u
#define CT_TABS 16u
#define CT_NONPRINT 32u
#define CT_COLOUR 64u
#define CT_FORCE 128u
#define CT_PLAIN 256u
#define CT_FLAGS 1
#endif

typedef struct ct_opt ct_opt;
struct ct_opt {
	unsigned f;
	int tty;
	long n;
	int blank;
	const char *name;
};

int ct_raw(int fd, const char *nm);
int ct_cook(int fd, ct_opt *o);
int ct_binary(const char *b, size_t n);
const char *ct_lang(const char *nm);
void ct_line(str *out, const char *p, size_t n, ct_opt *o, const char *lang);
void ct_hl(str *out, const char *p, size_t n, const char *lang, unsigned f);
void ct_vis(str *o, unsigned char c, unsigned f);
int ct_wr(int fd, const char *p, size_t n);
int ct_plainish(const ct_opt *o);
void ct_number(str *o, long n, unsigned f);

#endif
