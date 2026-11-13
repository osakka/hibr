#ifndef HIBR_INSPECT_H
#define HIBR_INSPECT_H
#include "hibr.h"
#ifndef IX_API_VER
#define IX_API_VER 1u
#endif
typedef struct ix_api {
	int (*query)(sh *, const char *, const char *, const char *, str *);
} ix_api;
#endif
