#ifndef HIBR_H
#define HIBR_H

#include <stddef.h>

#ifndef HIBR_ABI
#define HIBR_ABI 6u
#endif
#ifndef HIBR_VER
#define HIBR_VER "0.21"
#endif
#ifndef HIBR_OK
#define HIBR_OK 0
#endif
#ifndef HIBR_FAIL
#define HIBR_FAIL 1
#endif
#ifndef HIBR_NOCMD
#define HIBR_NOCMD 127
#endif
#ifndef HIBR_NOEXEC
#define HIBR_NOEXEC 126
#endif
#ifndef HIBR_IOCH
#define HIBR_IOCH 4096
#endif
#ifndef HIBR_ARCH
#define HIBR_ARCH 4096
#endif
#ifndef HIBR_HIST
#define HIBR_HIST 500
#endif
#ifndef HIBR_HDMAX
#define HIBR_HDMAX 32768
#endif
#ifndef HIBR_DEPTH
#define HIBR_DEPTH 2000
#endif
#ifndef HIBR_AXDEPTH
#define HIBR_AXDEPTH 1000
#endif
#ifndef HIBR_TAB0
#define HIBR_TAB0 64
#endif

#ifndef HIBR_LERR
#define HIBR_LERR 0
#endif
#ifndef HIBR_LWRN
#define HIBR_LWRN 1
#endif
#ifndef HIBR_LINF
#define HIBR_LINF 2
#endif
#ifndef HIBR_LDBG
#define HIBR_LDBG 3
#endif
#ifndef HIBR_LTRC
#define HIBR_LTRC 4
#endif

#ifndef HIBR_XONE
#define HIBR_XONE 1
#endif
#ifndef HIBR_XPAT
#define HIBR_XPAT 2
#endif

#ifndef P_TXT
#define P_TXT 0
#define P_VAR 1
#define P_CMD 2
#define P_ARI 3
#define P_PSUB 4
#endif

#ifndef V_NONE
#define V_NONE 0
#define V_DEF 1
#define V_ASG 2
#define V_ERR 3
#define V_ALT 4
#define V_LEN 5
#define V_RS 6
#define V_RL 7
#define V_SS 8
#define V_SL 9
#define V_SUB 10
#define V_SUBA 11
#define V_KEYS 12
#define V_SUBSTR 13
#define V_UP 14
#define V_UPALL 15
#define V_LOW 16
#define V_LOWALL 17
#define V_SUBP 18
#define V_SUBF 19
#define V_IND 20
#define V_NAMES 21
#define V_XQ 22
#define V_XE 23
#define V_INDF 0x100
#endif

#ifndef A_INT
#define A_INT 1u
#define A_REF 2u
#define A_TYSH 8
#define A_TYMASK 0xf00u
#endif

#ifndef O_NULLGLOB
#define O_NULLGLOB 1u
#define O_NOCASEGLOB 2u
#define O_DOTGLOB 4u
#define O_FAILGLOB 8u
#define O_NOCASEMATCH 16u
#define O_FIXON 1
#define O_FIXOFF 2
#endif

#ifndef J_STR
#define J_STR 0
#define J_NUM 1
#define J_BOOL 2
#define J_NULL 3
#define J_OBJ 4
#define J_ARR 5
#endif

#ifndef R_IN
#define R_IN 0
#define R_OUT 1
#define R_APP 2
#define R_DUP 3
#define R_RW 4
#define R_HERE 5
#endif

#ifndef RF_BOTH
#define RF_BOTH 1
#define RF_CLOB 2
#define RF_STR 4
#endif

#ifndef N_CMD
#define N_CMD 0
#define N_PIPE 1
#define N_AND 2
#define N_OR 3
#define N_SEQ 4
#define N_BG 5
#define N_NOT 6
#define N_SUB 7
#define N_GRP 8
#define N_IF 9
#define N_WHILE 10
#define N_UNTIL 11
#define N_FOR 12
#define N_FUNC 13
#define N_CASE 14
#define N_CLAUSE 15
#define N_ARITH 16
#define N_CFOR 17
#define N_COND 18
#define N_SELECT 19
#endif

typedef struct blk blk;
typedef struct arena arena;
typedef struct amark amark;
typedef struct str str;
typedef struct vec vec;
typedef struct part part;
typedef struct word word;
typedef struct redir redir;
typedef struct node node;
typedef struct var var;
typedef struct mod mod;
typedef struct ent ent;
typedef struct job job;
typedef struct sh sh;

struct blk { blk *nx; size_t cap, use; char d[]; };
struct arena { blk *b; size_t ch; };
struct amark { blk *b; size_t use; };
struct str { char *p; size_t n, cap; };
struct vec { void **p; size_t n, cap; };

struct part { part *nx; word *arg, *idx; char *t; size_t n; short k, op; unsigned q, col, arr; };
struct word { word *nx; part *p; };
struct redir { redir *nx; word *w; char *var; int fd; short k, fl; };
struct node { node *l, *r, *x; word *w, *aw; redir *rd; char *s, *tx, *rt; short k, f; };
struct ent { ent *nx; char *k, *s; ent *map; size_t n; short ty; };
struct var { var *nx; char *k, *v; ent *map; size_t n; short ty; unsigned ex, ro, am, at; };

struct sh {
	arena *ar, *xa;
	vec held, mods, fns, sbf, vbf, hist, jobs, scope, psub;
	vec als, axp, dirs, schemes, opts, cmds;
	char *odesc;
	var **tab;
	size_t tsz, tn;
	char **av;
	int ac, avo;
	char *arg0, *rty, *etrap, *dtrap;
	int st, lv, it, dep, nofork, tst, pfs, stop, uset, intry, noclob, noexec, strict, bind, hx, xerr;
	long t0;
	char **trap;
	int tty, jid, jcur, jprv;
	long pgid, pid;
	char *hfile;
	int ret, brk, cont, quit, keep, xtr, errx;
	char **amask;
	unsigned sopt;
};

typedef int (*hibr_fn)(sh *s, int ac, char **av);

typedef struct hibr_bi { const char *nm; hibr_fn fn; const char *hp; } hibr_bi;

typedef struct hibr_mod {
	unsigned abi;
	const char *nm, *ver, *dsc;
	const hibr_bi *bi;
	int (*ini)(sh *s);
	void (*fin)(sh *s);
} hibr_mod;

#ifndef HIBR_BI_END
#define HIBR_BI_END { 0, 0, 0 }
#endif

#ifndef HIBR_MODULE
#define HIBR_MODULE(nm_, ver_, dsc_, bi_, ini_, fin_) \
	const hibr_mod hibr_module = { HIBR_ABI, nm_, ver_, dsc_, bi_, ini_, fin_ }
#endif

extern int hibr_lv;

void lg(int lv, const char *f, ...);
void *xm(size_t n);
void *xr(void *p, size_t n);
char *xs(const char *s);

arena *ar_new(size_t ch);
void *ar_alloc(arena *a, size_t n);
char *ar_dup(arena *a, const char *s, size_t n);
amark ar_mark(arena *a);
void ar_rel(arena *a, amark m);
void ar_reset(arena *a);
void ar_free(arena *a);

void s_init(str *s);
void s_grow(str *s, size_t n);
void s_add(str *s, const char *p, size_t n);
void s_cat(str *s, const char *p);
void s_ch(str *s, int c);
void s_free(str *s);
void s_num(str *s, long v);
void v_add(vec *v, void *x);
void v_free(vec *v);
str *sb_get(sh *s);
void sb_put(sh *s, str *b);
vec *vb_get(sh *s);
void vb_put(sh *s, vec *v);

const char *hibr_get(sh *s, const char *k);
int hibr_set(sh *s, const char *k, const char *v, int ex);
int hibr_run(sh *s, const char *src);

const char *hibr_getp(sh *s, const char *nm, char **ks, int nk);
void hibr_setp(sh *s, const char *nm, char **ks, int nk, const char *v);
size_t hibr_count(sh *s, const char *nm, char **ks, int nk);
void hibr_list(sh *s, const char *nm, char **ks, int nk, vec *out, int keys);
void hibr_ret(sh *s, const char *v);
void hibr_retn(sh *s, char **vals, size_t n);
void hibr_fail(sh *s, const char *msg);
int hibr_dial(const char *host, const char *port, int udp);

typedef int (*hibr_open_fn)(sh *s, const char *rest);
int hibr_scheme(sh *s, const char *nm, hibr_open_fn fn);
int hibr_unscheme(sh *s, const char *nm);

#endif
