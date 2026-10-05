#ifndef HIBR_UN_H
#define HIBR_UN_H

#include "hibr.h"
#include <stddef.h>

#ifndef UN_MAXDEPTH
#define UN_MAXDEPTH 125
#endif
#ifndef UN_MAXBRACK
#define UN_MAXBRACK 63
#endif

enum { UN_L, UN_R, UN_AL, UN_EN, UN_ES, UN_ET, UN_AN, UN_CS, UN_NSM, UN_BN,
       UN_B, UN_S, UN_WS, UN_ON, UN_LRE, UN_LRO, UN_RLE, UN_RLO, UN_PDF,
       UN_LRI, UN_RLI, UN_FSI, UN_PDI, UN_NCLASS };

enum { UN_JU, UN_JR, UN_JL, UN_JD, UN_JC, UN_JT };

typedef struct un_rng { unsigned lo, hi; unsigned char v; } un_rng;
typedef struct un_pair { unsigned a, b; } un_pair;
typedef struct un_brk { unsigned a, b; unsigned char open; } un_brk;
typedef struct un_form { unsigned c, f[4]; } un_form;
typedef struct un_lig { unsigned alef, iso, fin; } un_lig;

extern const char un_ucdver[];
extern const un_rng un_bidi[], un_join[], un_width[];
extern const size_t un_nbidi, un_njoin, un_nwidth;
extern const un_pair un_mirror[];
extern const size_t un_nmirror;
extern const un_brk un_brack[];
extern const size_t un_nbrack;
extern const un_form un_forms[];
extern const size_t un_nforms;
extern const un_lig un_ligs[];
extern const size_t un_nligs;

int un_rngv(const un_rng *t, size_t n, unsigned c, int dflt);
int un_class(unsigned c);
int un_jtype(unsigned c);
int un_cw(unsigned c);
unsigned un_mirrorof(unsigned c);
int un_bracket(unsigned c, unsigned *pair);
int un_levels(const unsigned char *cls, const unsigned *cp, size_t n, int dir,
	      signed char *lv);
void un_order(const signed char *lv, const unsigned char *cls, size_t n, int plev,
	      size_t *ord, size_t *no);
size_t un_shape(unsigned *cp, signed char *lv, size_t *src, size_t n);

#endif
