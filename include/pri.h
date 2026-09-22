#ifndef HIBR_PRI_H
#define HIBR_PRI_H

#include "hibr.h"
#include <stdio.h>

#define T_EOF 0
#define T_WORD 1
#define T_NL 2
#define T_SEMI 3
#define T_AMP 4
#define T_PIPE 5
#define T_AND 6
#define T_OR 7
#define T_LP 8
#define T_RP 9
#define T_LT 10
#define T_GT 11
#define T_APP 12
#define T_DIN 13
#define T_DOUT 14
#define T_RW 15
#define T_HERE 16
#define T_DSEMI 17
#define T_SEMIAMP 18
#define T_DSEMIAMP 19
#define T_HERES 20
#define T_ARITH 21
#define T_PIPEAMP 22

typedef struct lex lex;
struct lex {
	sh *s;
	arena *a;
	const char *p, *e, *tkb;
	word *w;
	int tk, fd, nb, more, err, dash, both, clob, depth;
	char *fdvar;
	vec hq;
};

unsigned vh(const char *k);
var *v_find(sh *s, const char *k);
void v_del(sh *s, const char *k);
void v_env(sh *s);
int v_shadowed(vec *extra, const char *k);
char **v_envp(sh *s, vec *extra);
const char *sh_ifs(sh *s);
void v_names(sh *s, const char *pre, vec *out);
unsigned v_tycode(const char *nm);
const char *v_tyname(unsigned at);
const char *v_coerce(sh *s, var *e, const char *v, str *tmp);
void v_pos(sh *s, int ac, char **av);
void v_free_el(var *v);
void v_arr(sh *s, const char *k, vec *vals);
void v_setel(sh *s, const char *k, long i, const char *val);
const char *v_getel(sh *s, const char *k, long i);
size_t v_alen(sh *s, const char *k);
ent *mp_find(ent *m, const char *k);
ent *v_path(sh *s, const char *nm, char **ks, int nk, int make);
int b_json(sh *s, int ac, char **av);
int b_str(sh *s, int ac, char **av);
int b_arr(sh *s, int ac, char **av);
ent *mp_add(ent **m, size_t *n, const char *k);
void mp_free(ent *m);
const char *v_getp(sh *s, const char *nm, char **ks, int nk);
void v_setp(sh *s, const char *nm, char **ks, int nk, const char *val);
size_t v_count(sh *s, const char *nm, char **ks, int nk);
void v_list(sh *s, const char *nm, char **ks, int nk, vec *out, int keys);
int v_delp(sh *s, const char *nm, char **ks, int nk);
void pf_esc(str *o, const char *p, int stop_at_c);

int ismeta(int c);
int isname(const char *t);
void lx_init(lex *l, sh *s, const char *src);
int lx_next(lex *l);
word *lx_word(lex *l);
char *lx_span(lex *l);
char *lx_arrow(lex *l);
word *lx_sub(lex *l, const char *b, const char *e);
void lx_brace1(lex *l, part *p, const char *b, const char *e);
void lx_here(lex *l, redir *r, word *d);

char *w_lit(word *w);
int w_asg(word *w);
node *hibr_parse(sh *s, const char *src, int *more);

int ax_digit(int c, long base, long *out);
long ax_run(sh *s, const char *src);
long ax_text(sh *s, const char *t);
void xwm(sh *s, word *w, vec *out, int fl, vec *outm);
void xoutq(sh *s, vec *out, vec *outm, const char *t, size_t n);
void xpad(vec *out, vec *outm);
char *xone_q(sh *s, word *w, char **mask);
int w_hasq(word *w);
int w_simple(word *w);
extern const char w_meta[256];
int w_simple(word *w);
char *xnum(sh *s, long v);
char *xcap(sh *s, const char *src);
char *xpat(sh *s, word *w);
char *xone(sh *s, word *w);
char *xkey(sh *s, char *t);
const char *xbyname(sh *s, const char *r);
char *xkey_q(sh *s, char *t, const char *mk);
char *xquote(sh *s, const char *v, int bs);
int xqsafe(int c, int first);
char *xneg(sh *s, const char *nm, char **ks, int lvl);
char *xpsub(sh *s, part *p);
void xpsub_done(sh *s);
char **xargv(sh *s, word *w, int *ac, char ***am);

/* Expand a word into fields, with no interest in which bytes were quoted. */
#ifndef xw
#define xw(s_, w_, out_, fl_) xwm((s_), (w_), (out_), (fl_), 0)
#endif

/* Add one expanded field, keeping the parallel mask vector aligned. */
#ifndef xout
#define xout(s_, out_, outm_, t_, mk_, n_) \
	do { \
		const char *xo_t = (t_), *xo_m = (mk_); \
		size_t xo_n = (n_); \
		vec *xo_v = (outm_); \
		v_add((out_), ar_dup((s_)->xa, xo_t ? xo_t : "", xo_n)); \
		if (xo_v) \
			v_add(xo_v, xo_m ? ar_dup((s_)->xa, xo_m, xo_n) : 0); \
	} while (0)
#endif
const char *gnext(const char *a, const char *close);
int gneg(const char *body, const char *close, const char *rest, const char *t);
int gext(const char *p, const char *t);
int gmatch(const char *p, const char *t);
int gcmp(const void *a, const void *b);

int ex_asg(sh *s, char *kv, const char *mask, int ex_flag);
int ex(sh *s, node *n);
int rd_do(sh *s, redir *r, vec *sv);
void rd_undo(vec *sv);
node *fn_find(sh *s, const char *nm);
int kw_name(const char *t);
int m_isload(sh *s, const char *nm, const char *path);
void m_probe(sh *s, const char *path, const char *file);
int m_cmp(const void *a, const void *b);
void m_scan(sh *s, const char *dir, vec *seen);
void m_avail(sh *s);
void m_apifree(sh *s);
int m_need(sh *s, const char *nm);
void *m_offered(sh *s, const char *nm, unsigned ver, int *wrong);
int m_declares(const char *path, const char *iface);
int m_seek(sh *s, const char *dir, const char *iface);
int cmd_what(sh *s, const char *nm, int vb);
int fn_call(sh *s, node *f, int ac, char **av);
const char *hsh_get(sh *s, const char *nm);
void hsh_clear(sh *s, const char *nm);
void hsh_put(sh *s, const char *nm, const char *path);
char *findx(sh *s, const char *nm);

struct sav { char *k, *v; unsigned ex; };

void asg_keep(sh *s, vec *old, const char *k);
void asg_pop(sh *s, vec *old);
int bi_mask(const char *nm);
int bi_argk(const char *nm);
int w_assign(word *w);
int w_bind(word *w);
void ex_bind(sh *s, node *n);
size_t u8n(const char *t, size_t n);
size_t u8off(const char *t, size_t n, size_t c);
char *bi_keys(sh *s, char *word, const char *mk, vec *ks);
void b_decl1(sh *s, var *v);
int b_decl(sh *s, int ac, char **av);
int b_ro(sh *s, int ac, char **av);
int b_local(sh *s, int ac, char **av);

struct signm { const char *nm; int sig; };
extern const struct signm jc_sigs[];

void tr_init(sh *s);
void tr_run(sh *s);
void tr_exit(sh *s);
void tr_debug(sh *s, const char *what);
void tr_return(sh *s);
void tr_fork(sh *s);
void tr_fini(sh *s);
int tr_pending(void);
int b_trap(sh *s, int ac, char **av);
int b_fail(sh *s, int ac, char **av);
int b_try(sh *s, int ac, char **av);
void tr_err(sh *s, int st);
int b_printf(sh *s, int ac, char **av);
int b_match(sh *s, int ac, char **av);
int b_rsub(sh *s, int ac, char **av);
int b_ret(sh *s, int ac, char **av);
char *cwd(void);
int b_cd(sh *s, int ac, char **av);
int b_alias(sh *s, int ac, char **av);
int b_unalias(sh *s, int ac, char **av);
const char *dir_at(sh *s, long k);
const char *xtilde1(sh *s, const char *t, size_t n);
int b_dirs(sh *s, int ac, char **av);
int b_pushd(sh *s, int ac, char **av);
int b_popd(sh *s, int ac, char **av);
const char *al_get(sh *s, const char *k);
int al_busy(sh *s, const char *k);
int al_run(sh *s, const char *body, int ac, char **av, char **am);
void al_fini(sh *s);
char *pr_make(sh *s, const char *ps);
char *pr_hook(sh *s);
void rc_load(sh *s);
void al_quote(str *o, const char *a, const char *mk);
int b_command(sh *s, int ac, char **av);
int b_shopt(sh *s, int ac, char **av);
int b_ulimit(sh *s, int ac, char **av);
int ul_res(int c);
const char *ul_name(int c);
long ul_scale(int c);
void ul_show(int c, int hard, int label);
int sh_optfix(const char *nm);
unsigned sh_optbit(const char *nm);
int *sh_optflag(sh *s, const char *nm);
int sh_optget(sh *s, const char *nm);
int sh_optset(sh *s, const char *nm, int on);
void sh_optlist(sh *s, int setstyle);
int b_builtin(sh *s, int ac, char **av);
int b_umask(sh *s, int ac, char **av);
int b_time(sh *s, int ac, char **av);
int b_getopts(sh *s, int ac, char **av);
int b_disown(sh *s, int ac, char **av);
int b_let(sh *s, int ac, char **av);
typedef struct { char **v; int n; int i; int err; int d; } tex;
void xposlist(sh *s, vec *lst);
void xslice(sh *s, part *p, size_t n, long *off, long *len);
int t_one(const char *op, const char *a);
int t_isun(const char *w);
int t_isbin(const char *w);
int tx_prim(tex *t);
int tx_and(tex *t);
int tx_or(tex *t);
int t_two(const char *a, const char *op, const char *b);
int b_match(sh *s, int ac, char **av);
char *hx_expand(sh *s, const char *line, int *changed, int *bad);
void pt_init(int ac, char **av);
void pt_claim(void);
int b_title(sh *s, int ac, char **av);
int b_opt(sh *s, int ac, char **av);
int b_args(sh *s, int ac, char **av);
void op_clear(sh *s);
int net_is(sh *s, const char *p);
int net_open(sh *s, const char *p);
int net_dial(const char *host, const char *port, int udp);
void sc_fini(sh *s);
int b_connect(sh *s, int ac, char **av);
int b_listen(sh *s, int ac, char **av);
int b_coproc(sh *s, int ac, char **av);
void cp_slot(sh *s, const char *nm, const char *key, int idx, int fd);
int b_accept(sh *s, int ac, char **av);
int b_send(sh *s, int ac, char **av);
int b_recv(sh *s, int ac, char **av);
int ed_search(sh *s, str *b, size_t *pos);
int ty_ok(const char *ty, const char *v);
void v_copy(sh *s, const char *dst, const char *src);

const hibr_bi *bi_find(const char *nm);
const hibr_bi *m_find(sh *s, const char *nm);
int m_load(sh *s, const char *path);
int m_drop(sh *s, const char *nm);
int m_dropall(sh *s);
void m_list(sh *s);
void m_fini(sh *s);
void m_help(sh *s);

int b_src(sh *s, int ac, char **av);
int b_need(sh *s, int ac, char **av);
int b_app(sh *s, int ac, char **av);
char *rdline(FILE *f);
int ed_init(sh *s);
int ed_cols(void);
char *ed_line(sh *s, const char *ps);
void ed_fini(sh *s);
void hs_add(sh *s, const char *line);

#define J_RUN 0
#define J_STOP 1
#define J_DONE 2

struct job {
	int id, bg, state, np, ndone, st, fail, note;
	long pgid, last;
	char *tx;
};

void jc_init(sh *s);
job *jc_new(sh *s, const char *tx, int bg);
void jc_pid(sh *s, job *j, long p);
int jc_fg(sh *s, job *j);
void jc_bgnote(sh *s, job *j);
void jc_poll(sh *s, int report);
long jc_anyone(sh *s, int *w);
void jc_fini(sh *s);
void jc_drop(sh *s, job *j);
int b_jobs(sh *s, int ac, char **av);
int b_fg(sh *s, int ac, char **av);
int b_bg(sh *s, int ac, char **av);
int b_wait(sh *s, int ac, char **av);
int b_kill(sh *s, int ac, char **av);

#endif
