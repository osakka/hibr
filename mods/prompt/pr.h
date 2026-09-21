#ifndef HIBR_PROMPT_H
#define HIBR_PROMPT_H

#include "hibr.h"
#include <sys/stat.h>

#ifndef PR_DEPTH
#define PR_DEPTH 32
#endif

#ifndef OB_MAX
#define OB_MAX (64u << 20)
#endif
#ifndef OB_MAXDELTA
#define OB_MAXDELTA 256
#endif

#define OB_COMMIT 1
#define OB_TREE 2
#define OB_BLOB 3
#define OB_TAG 4
#define OB_OFS 6
#define OB_REF 7

typedef struct pctx pctx;
typedef struct seg seg;
typedef struct grepo grepo;
typedef struct gidx gidx;
typedef struct gstat gstat;

#ifndef SH_BLK
#define SH_BLK 64
#endif

typedef struct sha1 sha1;
struct sha1 {
	unsigned h[5];
	unsigned char buf[SH_BLK];
	size_t n, tot;
};

void sh_init(sha1 *s);
void sh_add(sha1 *s, const void *p, size_t n);
void sh_done(sha1 *s, unsigned char *out);
int sh_blob(const char *path, int symlink, unsigned char *out);

struct ie {
	char *path;
	unsigned char sha[20];
	unsigned mode, size, mtim, mtin, ctim, ctin, dev, ino, uid, gid;
	int stage;
};

struct ct {
	char *path;
	long cnt;
	unsigned char sha[20];
};

struct went {
	unsigned char sha[20];
	int used, flags;
};

typedef struct wmap wmap;
struct wmap {
	struct went *t;
	size_t n, cap;
};

struct wq {
	unsigned char sha[20];
	long date;
};

struct ig {
	char *pat;
	int neg, dironly, anchored;
};

struct iglev {
	vec pats;
	size_t base;
};

struct te {
	char *path;
	unsigned mode;
	unsigned char sha[20];
};

struct gstat {
	int staged, modified, deleted, untracked, conflicted, renamed;
	int ahead, behind, ok, unborn;
};

struct gidx {
	vec ents, ctrees;
	unsigned ver, mtim, mtimns;
};

gidx *ix_read(grepo *g);
char *gt_cfg(grepo *g, const char *sect, const char *sub, const char *key);
void st_worktree(grepo *g, gidx *x, gstat *o, int filemode);
void st_staged(grepo *g, gidx *x, gstat *o);
void st_untracked(grepo *g, gidx *x, gstat *o);
gstat *st_get(pctx *c);
void wk_track(pctx *c, grepo *g, gstat *o);
void ig_load(const char *text, vec *out);
void ig_free(vec *v);
int ig_test(vec *stack, const char *rel, const char *name, int isdir);
int ig_wild(const char *p, const char *t, int depth);
int gt_ref(grepo *g, const char *ref, unsigned char *sha);
int gt_headsha(grepo *g, unsigned char *sha);
void ix_free(gidx *x);
struct ct *ix_ct(gidx *x, const char *prefix);
unsigned ix_be32(const unsigned char *p);

struct pctx {
	sh *s;
	int st, jobs, gdone;
	long dur;
	char *cwd, *home;
	grepo *g;
};

struct seg {
	const char *nm, *fmt, *style, *sym, *aux;
	int off;
	int (*act)(pctx *c, const seg *g);
	char *(*val)(pctx *c, const seg *g, const char *f);
};

typedef char *(*plook)(pctx *c, void *ud, const char *nm);

struct sgc { pctx *c; const seg *g; };

const char *pr_cfg(pctx *c, const char *sg, const char *key);
const char *pr_top(pctx *c, const char *key);
int pr_cfgi(pctx *c, const char *sg, const char *key, int dflt);
void pr_sgr(const char *style, str *o);
char *pr_wrap(const char *style, const char *body);
char *pr_fmt(pctx *c, const char **pp, int stop, int depth, int *any,
	     plook lk, void *ud);

const char *pr_env(pctx *c, const char *nm);
char *pr_slurp(const char *path, size_t max);
char *pr_field(const char *txt, const char *key, int json);
char *pr_base(const char *path);
char *pr_span(const char *b, const char *e);
int pr_col(str *o, const char *v, size_t n, int bg);
int pr_hex(int c);

void pr_ctx(sh *s, pctx *c);
void pr_ctxfree(pctx *c);

extern const seg pr_segs[];
const seg *pr_find(const char *nm);
char *pr_seg(pctx *c, const char *nm);
char *pr_render(pctx *c);

int inf_raw(const unsigned char *in, size_t n, size_t max, str *out,
	    size_t *used);
int inf_zlib(const unsigned char *in, size_t n, size_t max, str *out,
	     size_t *used);

struct grepo {
	char *dir, *common, *top, *branch, *sha, *state, *cfgtxt;
	int cfgdone;
	int stash, ok, packsdone;
	size_t omax;
	vec odirs, packs;
	gstat st;
};

unsigned char *ob_map(const char *path, size_t *n);
int ob_hex(const char *hex, unsigned char *sha);
void ob_unhex(const unsigned char *sha, char *hex);
int ob_get(grepo *g, const unsigned char *sha, int *type, str *out);
const char *ob_tname(int type);
void ob_free(grepo *g);

char *gt_join(const char *dir, const char *sub);
char *gt_read(const char *dir, const char *sub, size_t max);
int gt_has(const char *dir, const char *sub);
int gt_open(pctx *c);
void gt_close(pctx *c);
int gt_act(pctx *c, const seg *g);
char *gt_val(pctx *c, const seg *g, const char *f);
char *gt_stat(pctx *c, const seg *sg, const char *f);
char *gt_rel(pctx *c);

#endif
