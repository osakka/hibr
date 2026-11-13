#include "pri.h"
#include <ctype.h>
#include <string.h>

node *p_list(lex *l);
node *p_cmd(lex *l);
node *p_cmd2(lex *l);
word *p_arrel(lex *l);
void p_where(lex *l);
void perr_near(lex *l);
node *p_body(lex *l);

/* Allocate an AST node in the parse arena. */
node *nd(lex *l, int k)
{
	node *n = ar_alloc(l->a, sizeof *n);

	n->k = (short)k;
	n->ln = lx_line(l);
	return n;
}

/* Return the literal text of a bare unquoted word, else NULL. */
char *w_lit(word *w)
{
	if (!w || !w->p || w->p->nx || w->p->k != P_TXT || w->p->q)
		return 0;
	return w->p->t;
}

/* True if a word carries a leading assignment prefix. */
int w_asg(word *w)
{
	part *p = w->p;
	size_t i, j;

	if (!p || p->k != P_TXT || p->q || !p->n)
		return 0;
	if (isdigit((unsigned char)p->t[0]))
		return 0;
	for (i = 0; i < p->n; i++) {
		if (p->t[i] == '=')
			return i > 0;
		if (p->t[i] == '+' && i > 0 && i + 1 < p->n && p->t[i + 1] == '=')
			return 1;
		if (p->t[i] == '[') {
			if (!i)
				return 0;
			for (j = i + 1; j + 1 < p->n; j++)
				if (p->t[j] == ']' && (p->t[j + 1] == '=' ||
						       (p->t[j + 1] == '+' && j + 2 < p->n &&
							p->t[j + 2] == '=')))
					return 1;
			for (p = p->nx; p; p = p->nx) {
				if (p->k != P_TXT)
					continue;
				for (j = 0; j + 1 < p->n; j++)
					if (p->t[j] == ']' && (p->t[j + 1] == '=' ||
							       p->t[j + 1] == '+'))
						return 1;
			}
			return 0;
		}
		if (!(isalnum((unsigned char)p->t[i]) || p->t[i] == '_'))
			return 0;
	}
	return 0;
}

/* True if the word is an assignment with nothing after the equals sign. */
int w_asgbare(word *w)
{
	part *p = w->p;

	return p && !p->nx && p->k == P_TXT && p->n && p->t[p->n - 1] == '=';
}

/* Whether a syntax error can be reported now: not after one already was,
   and not at the end of the text, which only means more is needed. */
int perr_due(lex *l)
{
	if (l->err)
		return 0;
	if (l->tk == T_EOF) {
		l->more = 1;
		return 0;
	}
	return 1;
}

/* Report a syntax error at the current token, once. */
void perr(lex *l, const char *m)
{
	if (!perr_due(l))
		return;
	if (!l->more) {
		p_where(l);
		l->errmsg = ar_dup(l->a, m, strlen(m));
		l->errat = l->tkb ? l->tkb : l->p;
		l->errln = lg_ln;
		l->errcol = lg_col;
		lg(HIBR_LERR, "syntax error: %s", m);
		lg_ln = lg_col = 0;
	}
	l->err = 1;
}

/* Point the diagnostics at the current token: its line and column. */
void p_where(lex *l)
{
	lg_ln = lx_line(l);
	lg_col = l->tkb && l->bol && l->tkb >= l->bol ?
		(unsigned)(l->tkb - l->bol) + 1 : 0;
}

/* Report the current token as one that cannot stand here, naming it the
   way bash does. */
void perr_near(lex *l)
{
	const char *b = l->tkb, *e = l->p;

	if (!perr_due(l))
		return;
	if (!l->more) {
		while (b && e > b && (e[-1] == ' ' || e[-1] == '\t'))
			e--;
		if (b && e > b && e[-1] == '\n')
			e--;
		p_where(l);
		{
			str msg;

			s_init(&msg);
			s_fmt(&msg, "syntax error near unexpected token `%.*s'",
			      b ? (int)(e - b) : 0, b ? b : "");
			l->errmsg = ar_dup(l->a, msg.p, msg.n);
			s_free(&msg);
		}
		l->errat = l->tkb ? l->tkb : l->p;
		l->errln = lg_ln;
		l->errcol = lg_col;
		lg(HIBR_LERR, "syntax error near unexpected token `%.*s'",
		   b ? (int)(e - b) : 0, b ? b : "");
		lg_ln = lg_col = 0;
	}
	l->err = 1;
}

/* A compound command's list, which bash will not let be empty: then, do,
   { } and ( ) with nothing in them name the token that came too soon. */
node *p_body(lex *l)
{
	node *n = p_list(l);

	if (!n && !l->err && !l->more)
		perr_near(l);
	return n;
}

/* True if the current token is the given reserved word. */
int kw(lex *l, const char *s)
{
	char *t;

	if (l->tk != T_WORD)
		return 0;
	t = w_lit(l->w);
	return t && !strcmp(t, s);
}

/* Skip any run of newline tokens. */
void p_nl(lex *l)
{
	while (l->tk == T_NL)
		lx_next(l);
}

/* True if the current token closes an enclosing list. */
int p_end(lex *l)
{
	char *t;

	if (l->tk == T_EOF || l->tk == T_RP || l->tk == T_DSEMI ||
	    l->tk == T_SEMIAMP || l->tk == T_DSEMIAMP)
		return 1;
	if (l->tk != T_WORD)
		return 0;
	t = w_lit(l->w);
	if (!t)
		return 0;
	return !strcmp(t, "then") || !strcmp(t, "else") || !strcmp(t, "elif") ||
	       !strcmp(t, "fi") || !strcmp(t, "do") || !strcmp(t, "done") ||
	       !strcmp(t, "esac") || !strcmp(t, "}");
}

/* Map a redirection token to its node kind. */
int r_kind(int tk)
{
	switch (tk) {
	case T_LT:
		return R_IN;
	case T_GT:
		return R_OUT;
	case T_APP:
		return R_APP;
	case T_DIN:
	case T_DOUT:
		return R_DUP;
	case T_RW:
		return R_RW;
	case T_HERE:
	case T_HERES:
		return R_HERE;
	}
	return -1;
}

/* Parse one redirection and append it to a list tail. */
int p_redir(lex *l, redir ***rt)
{
	int k = r_kind(l->tk);
	int tk = l->tk;
	redir *r;

	if (k < 0)
		return 0;
	r = ar_alloc(l->a, sizeof *r);
	r->k = (short)k;
	r->fd = l->fd >= 0 ? l->fd :
		(tk == T_LT || tk == T_RW || tk == T_DIN || tk == T_HERE ||
				 tk == T_HERES ?
			 0 :
			 1);
	if (l->both)
		r->fl |= RF_BOTH;
	if (l->clob)
		r->fl |= RF_CLOB;
	if (tk == T_HERES)
		r->fl |= RF_STR;
	if (tk == T_DOUT && (l->fd < 0 || l->fd == 1) && !l->fdvar)
		r->fl |= RF_NOFD;
	r->var = l->fdvar;
	lx_next(l);
	if (l->tk != T_WORD) {
		perr(l, "expected redirection target");
		return 1;
	}
	if (tk == T_HERE)
		lx_here(l, r, l->w);
	else
		r->w = l->w;
	lx_next(l);
	**rt = r;
	*rt = &r->nx;
	return 1;
}

/* Collect redirections trailing a compound command. */
void p_rtail(lex *l, node *n)
{
	redir **rt = &n->rd;

	while (*rt)
		rt = &(*rt)->nx;
	while (r_kind(l->tk) >= 0)
		if (!p_redir(l, &rt))
			break;
}

/* Parse the ( elements ) of name=( ... ), the name taken from w. */
node *p_arrasg(lex *l, word *w)
{
	node *cl;
	word **et;

	lx_next(l);
	p_nl(l);
	cl = nd(l, N_CLAUSE);
	cl->s = ar_dup(l->a, w->p->t, w->p->n - 1);
	et = &cl->w;
	while (l->tk == T_WORD) {
		*et = p_arrel(l);
		et = &(*et)->nx;
		lx_next(l);
		p_nl(l);
	}
	if (l->tk != T_RP) {
		perr(l, "expected ) in array assignment");
		return 0;
	}
	lx_next(l);
	return cl;
}

/* Parse a simple command, assignment list or function definition. */
node *p_simple(lex *l)
{
	node *n = nd(l, N_CMD);
	word **wt = &n->w, **at = &n->aw;
	redir **rt = &n->rd;
	node **xt = &n->x, *cl;
	word *w;
	char *t, *cmd0 = 0;
	int got = 0;

	for (;;) {
		if (l->tk == T_WORD) {
			w = l->w;
			t = w_lit(w);
			lx_next(l);
			if (!got && t && *t && !strchr(t, '=') && l->tk == T_LP) {
				node *f;
				lx_next(l);
				if (l->tk != T_RP) {
					perr(l, "expected ) in function definition");
					return n;
				}
				lx_next(l);
				p_nl(l);
				f = nd(l, N_FUNC);
				f->s = t;
				f->r = p_cmd(l);
				l->s->keep = 1;
				return f;
			}
			if (!got && l->tk == T_WORD && w_lit(l->w) &&
			    !strcmp(w_lit(l->w), ":=") &&
			    ((t && isname(t)) || w_bind(w))) {
				if (t && isname(t))
					n->s = t;
				else
					n->bw = w;
				n->f = 2;
				lx_next(l);
				continue;
			}
			if (!got && l->tk == T_LP && w_asg(w) && w_asgbare(w)) {
				cl = p_arrasg(l, w);
				if (!cl)
					return n;
				*xt = cl;
				xt = &cl->x;
				continue;
			}
			if (!got && w_asg(w)) {
				*at = w;
				at = &w->nx;
				continue;
			}
			if (got && cmd0 && l->tk == T_LP && w_asg(w) &&
			    w_asgbare(w) && (bi_argk(cmd0) & 2)) {
				word *nw;
				part *np;
				cl = p_arrasg(l, w);
				if (!cl)
					return n;
				cl->f = (bi_argk(cmd0) & 4) ? 5 : 4;
				nw = ar_alloc(l->a, sizeof *nw);
				np = ar_alloc(l->a, sizeof *np);
				np->k = P_TXT;
				np->n = w->p->n - 1;
				if (np->n && w->p->t[np->n - 1] == '+')
					np->n--;
				np->t = ar_dup(l->a, w->p->t, np->n);
				nw->p = np;
				*wt = nw;
				wt = &nw->nx;
				*xt = cl;
				xt = &cl->x;
				continue;
			}
			if (!got)
				cmd0 = t;
			got = 1;
			*wt = w;
			wt = &w->nx;
			continue;
		}
		if (r_kind(l->tk) >= 0) {
			if (!p_redir(l, &rt))
				break;
			if (l->err)
				break;
			continue;
		}
		break;
	}
	if (!got && !n->aw && !n->rd && !n->x)
		return 0;
	return n;
}

/* Parse an if/elif chain. */
node *p_if(lex *l)
{
	node *n = nd(l, N_IF);

	lx_next(l);
	n->l = p_body(l);
	if (!kw(l, "then")) {
		perr(l, "expected then");
		return n;
	}
	lx_next(l);
	n->r = p_body(l);
	if (kw(l, "elif")) {
		n->x = p_if(l);
		return n;
	}
	if (kw(l, "else")) {
		lx_next(l);
		n->x = p_body(l);
	}
	if (!kw(l, "fi"))
		perr(l, "expected fi");
	else
		lx_next(l);
	return n;
}

/* Parse a while or until loop. */
node *p_while(lex *l)
{
	node *n = nd(l, kw(l, "until") ? N_UNTIL : N_WHILE);

	lx_next(l);
	n->l = p_body(l);
	if (!kw(l, "do")) {
		perr(l, "expected do");
		return n;
	}
	lx_next(l);
	n->r = p_body(l);
	if (!kw(l, "done"))
		perr(l, "expected done");
	else
		lx_next(l);
	return n;
}

/* Make a literal word from a slice of arithmetic text. */
word *p_litw(lex *l, const char *b, size_t n)
{
	word *w = ar_alloc(l->a, sizeof *w);
	part *p = ar_alloc(l->a, sizeof *p);

	p->k = P_TXT;
	p->q = 1;
	p->t = ar_dup(l->a, b, n);
	p->n = n;
	w->p = p;
	return w;
}

/* Parse the body of a C-style for loop after its (( header. */
node *p_cfor(lex *l)
{
	node *n = nd(l, N_CFOR);
	const char *t = l->w->p->t, *b = t, *q;
	word **wt = &n->w;
	int d = 0, k = 0;

	for (q = t;; q++) {
		if (*q == '(')
			d++;
		else if (*q == ')')
			d--;
		if (!*q || (*q == ';' && !d)) {
			*wt = p_litw(l, b, (size_t)(q - b));
			wt = &(*wt)->nx;
			k++;
			if (!*q)
				break;
			b = q + 1;
		}
	}
	if (k != 3) {
		perr(l, "for (( )) needs init; condition; step");
		return n;
	}
	lx_next(l);
	while (l->tk == T_SEMI || l->tk == T_NL)
		lx_next(l);
	if (kw(l, "{")) {
		n->r = p_cmd(l);
		return n;
	}
	if (!kw(l, "do")) {
		perr(l, "expected do");
		return n;
	}
	lx_next(l);
	n->r = p_body(l);
	if (!kw(l, "done"))
		perr(l, "expected done");
	else
		lx_next(l);
	return n;
}

/* The end of a word that keeps its brackets and parens balanced and its
   quotes whole: an array element's [key]=value and the right side of =~
   both end at a blank, or at a ) that closes nothing. */
const char *p_balw(const char *e, const char *end)
{
	int d = 0;

	while (e < end) {
		if (*e == '\\' && e + 1 < end) {
			e += 2;
			continue;
		}
		if (*e == '\'' || *e == '"') {
			char qc = *e++;
			while (e < end && *e != qc)
				e++;
			if (e < end)
				e++;
			continue;
		}
		if (*e == '(' || *e == '[')
			d++;
		else if ((*e == ')' || *e == ']') && d > 0)
			d--;
		else if (!d && (*e == ' ' || *e == '\t' || *e == '\n' || *e == ')'))
			break;
		e++;
	}
	return e;
}

/* The array element the lexer has just read, whole: one opening with a [ is
   re-read from its start to its matching ], so a key holding blanks stays one
   element, as bash has it -- [c d]=2 is the key "c d". */
word *p_arrel(lex *l)
{
	const char *b = l->tkb, *e, *t;
	word *w;

	if (!b || b >= l->p || *b != '[')
		return l->w;
	for (t = b; t < l->p; t++)
		if (*t == ']')
			return l->w;
	e = p_balw(b, l->e);
	lg(HIBR_LTRC, "array element %.*s read whole", (int)(e - b), b);
	l->p = e;
	w = lx_sub(l, b, e, 0);
	return w ? w : l->w;
}

/* Build a synthetic operator word for a [[ ]] expression. */
word *p_opw(lex *l, const char *op)
{
	word *w = p_litw(l, op, strlen(op));

	w->p->op = -1;
	return w;
}

/* Parse a [[ ]] conditional into a flat list of operands and operators. */
node *p_cond(lex *l)
{
	node *n = nd(l, N_COND);
	word **wt = &n->w, *w;
	const char *op, *b, *e;

	lx_next(l);
	for (;;) {
		while (l->tk == T_NL)
			lx_next(l);
		if (kw(l, "]]")) {
			lx_next(l);
			return n;
		}
		op = 0;
		switch (l->tk) {
		case T_AND: op = "&&"; break;
		case T_OR: op = "||"; break;
		case T_LP: op = "("; break;
		case T_RP: op = ")"; break;
		case T_LT: op = "<"; break;
		case T_GT: op = ">"; break;
		case T_WORD: break;
		default:
			perr(l, "unexpected token inside [[ ]]");
			return n;
		}
		w = op ? p_opw(l, op) : l->w;
		*wt = w;
		wt = &w->nx;
		if (!op && w_lit(w) && !strcmp(w_lit(w), "=~")) {
			b = l->p;
			while (b < l->e && (*b == ' ' || *b == '\t'))
				b++;
			e = p_balw(b, l->e);
			if (memchr(b, '$', (size_t)(e - b)) ||
			    memchr(b, '\'', (size_t)(e - b)) ||
			    memchr(b, '"', (size_t)(e - b))) {
				w = lx_sub(l, b, e, 0);
				if (!w) {
					if (!l->err && !l->more)
						perr(l, "bad regular expression after =~");
					return n;
				}
			} else {
				w = p_litw(l, b, (size_t)(e - b));
				w->p->op = -2;
			}
			*wt = w;
			wt = &w->nx;
			l->p = e;
		}
		lx_next(l);
	}
}

/* Parse a for loop over a word list or the positional parameters. */
node *p_for(lex *l)
{
	node *n = nd(l, kw(l, "select") ? N_SELECT : N_FOR);
	word **wt = &n->w;

	lx_next(l);
	if (n->k == N_FOR && l->tk == T_ARITH)
		return p_cfor(l);
	if (l->tk != T_WORD || !(n->s = w_lit(l->w)) || !isname(n->s)) {
		perr(l, "expected loop variable name");
		return n;
	}
	lx_next(l);
	if (kw(l, "in")) {
		n->f = 1;
		lx_next(l);
		while (l->tk == T_WORD) {
			*wt = l->w;
			wt = &l->w->nx;
			lx_next(l);
		}
	}
	while (l->tk == T_SEMI || l->tk == T_NL)
		lx_next(l);
	if (kw(l, "{")) {
		n->r = p_cmd(l);
		return n;
	}
	if (!kw(l, "do")) {
		perr(l, "expected do");
		return n;
	}
	lx_next(l);
	n->r = p_body(l);
	if (!kw(l, "done"))
		perr(l, "expected done");
	else
		lx_next(l);
	return n;
}

/* Parse one parameter declaration out of a signature. */
const char *p_param(lex *l, node *pm, const char *p, const char *e)
{
	const char *b;
	str t;

	while (p < e && (*p == ' ' || *p == '\t' || *p == ',' || *p == '\n'))
		p++;
	if (p >= e)
		return 0;
	if (p + 2 < e && p[0] == '.' && p[1] == '.' && p[2] == '.') {
		pm->f = 1;
		p += 3;
	}
	b = p;
	while (p < e && (isalnum((unsigned char)*p) || *p == '_'))
		p++;
	if (p < e && *p == '?')
		p++;
	s_init(&t);
	s_add(&t, b, (size_t)(p - b));
	while (p < e && (*p == ' ' || *p == '\t'))
		p++;
	if (p < e && (isalnum((unsigned char)*p) || *p == '_')) {
		pm->tx = ar_dup(l->a, t.p ? t.p : "", t.n);
		pm->f |= ty_code(pm->tx);
		b = p;
		while (p < e && (isalnum((unsigned char)*p) || *p == '_'))
			p++;
		t.n = 0;
		s_add(&t, b, (size_t)(p - b));
	}
	pm->s = ar_dup(l->a, t.p ? t.p : "", t.n);
	s_free(&t);
	while (p < e && (*p == ' ' || *p == '\t'))
		p++;
	if (p < e && *p == '=') {
		int d = 0;
		p++;
		while (p < e && (*p == ' ' || *p == '\t'))
			p++;
		b = p;
		while (p < e) {
			if (*p == '\'' || *p == '"') {
				char q = *p++;
				while (p < e && *p != q)
					p++;
				if (p < e)
					p++;
				continue;
			}
			if (*p == '(')
				d++;
			if (*p == ')')
				d--;
			if (*p == ',' && !d)
				break;
			p++;
		}
		pm->w = lx_sub(l, b, p, 0);
	}
	while (p < e && (*p == ' ' || *p == '\t' || *p == ','))
		p++;
	return p;
}

/* Parse bash's function name { ... }, with or without the () after the name. */
node *p_function(lex *l)
{
	node *f = nd(l, N_FUNC);
	char *t;

	lx_next(l);
	t = l->tk == T_WORD ? w_lit(l->w) : 0;
	if (!t || !*t) {
		perr_near(l);
		return f;
	}
	f->s = t;
	lx_next(l);
	if (l->tk == T_LP) {
		lx_next(l);
		if (l->tk != T_RP) {
			perr(l, "expected ) in function definition");
			return f;
		}
		lx_next(l);
	}
	p_nl(l);
	f->r = p_cmd(l);
	l->s->keep = 1;
	return f;
}

/* Parse a typed function declaration. */
node *p_fn(lex *l)
{
	node *n = nd(l, N_FUNC), *pm, **pt = &n->x;
	char *sig;
	const char *p, *e;

	lx_next(l);
	if (l->tk != T_WORD || !w_lit(l->w) || !isname(w_lit(l->w))) {
		perr(l, "expected a function name after fn");
		return n;
	}
	n->s = w_lit(l->w);
	lx_next(l);
	if (l->tk != T_LP) {
		perr(l, "expected ( after the function name");
		return n;
	}
	sig = lx_span(l);
	if (!sig) {
		perr(l, "unterminated parameter list");
		return n;
	}
	n->rt = lx_arrow(l);
	lx_next(l);
	p_nl(l);
	e = sig + strlen(sig);
	for (p = sig; p && p < e;) {
		const char *at = p;

		pm = nd(l, N_CLAUSE);
		p = p_param(l, pm, p, e);
		if (!p)
			break;
		if (!pm->s || !*pm->s) {
			str m;

			while (at < e && (*at == ' ' || *at == '\t' || *at == ',' ||
					  *at == '\n'))
				at++;
			s_init(&m);
			s_fmt(&m, "bad parameter `%.*s' in (%s)",
			      (int)strcspn(at, ", \t\n"), at, sig);
			perr(l, m.p);
			s_free(&m);
			return n;
		}
		*pt = pm;
		pt = &pm->x;
	}
	n->r = p_cmd(l);
	n->f = 1;
	l->s->keep = 1;
	return n;
}

/* Parse a case statement and its clauses. */
node *p_case(lex *l)
{
	node *n = nd(l, N_CASE), *cl, **ct = &n->l;
	word **wt;

	lx_next(l);
	if (l->tk != T_WORD) {
		perr(l, "expected word after case");
		return n;
	}
	n->w = l->w;
	lx_next(l);
	p_nl(l);
	if (!kw(l, "in")) {
		perr(l, "expected in after case word");
		return n;
	}
	lx_next(l);
	p_nl(l);
	while (!kw(l, "esac")) {
		if (l->tk == T_EOF) {
			perr(l, "expected esac");
			return n;
		}
		cl = nd(l, N_CLAUSE);
		if (l->tk == T_LP)
			lx_next(l);
		wt = &cl->w;
		for (;;) {
			if (l->tk != T_WORD) {
				perr(l, "expected case pattern");
				return n;
			}
			*wt = l->w;
			wt = &l->w->nx;
			lx_next(l);
			if (l->tk != T_PIPE)
				break;
			lx_next(l);
		}
		if (l->tk != T_RP) {
			perr(l, "expected ) after case pattern");
			return n;
		}
		lx_next(l);
		cl->r = p_list(l);
		if (l->tk == T_DSEMI) {
			lx_next(l);
		} else if (l->tk == T_SEMIAMP) {
			cl->f = 1;
			lx_next(l);
		} else if (l->tk == T_DSEMIAMP) {
			cl->f = 2;
			lx_next(l);
		} else if (!kw(l, "esac")) {
			perr(l, "expected ;; or esac");
			return n;
		}
		*ct = cl;
		ct = &cl->x;
		p_nl(l);
	}
	lx_next(l);
	return n;
}

/* Parse a compound or simple command. */
node *p_cmd(lex *l)
{
	node *n = 0;

	if (++l->depth > HIBR_DEPTH) {
		perr(l, "input nested too deeply");
		l->depth--;
		return 0;
	}
	n = p_cmd2(l);
	l->depth--;
	return n;
}

/* Parse a compound or simple command, depth already accounted for. */
node *p_cmd2(lex *l)
{
	node *n = 0;

	if (l->tk == T_LP) {
		lx_next(l);
		n = nd(l, N_SUB);
		n->l = p_body(l);
		if (l->tk != T_RP)
			perr(l, "expected )");
		else
			lx_next(l);
	} else if (kw(l, "{")) {
		lx_next(l);
		n = nd(l, N_GRP);
		n->l = p_body(l);
		if (!kw(l, "}"))
			perr(l, "expected }");
		else
			lx_next(l);
	} else if (kw(l, "if")) {
		n = p_if(l);
	} else if (kw(l, "while") || kw(l, "until")) {
		n = p_while(l);
	} else if (kw(l, "for") || kw(l, "select")) {
		n = p_for(l);
	} else if (l->tk == T_ARITH) {
		n = nd(l, N_ARITH);
		n->w = l->w;
		lx_next(l);
	} else if (kw(l, "[[")) {
		n = p_cond(l);
	} else if (kw(l, "case")) {
		n = p_case(l);
	} else if (kw(l, "fn")) {
		return p_fn(l);
	} else if (kw(l, "function")) {
		return p_function(l);
	} else {
		return p_simple(l);
	}
	if (n)
		p_rtail(l, n);
	return n;
}

/* Attach the 2>&1 that |& stands for to the stage feeding the pipe. */
void p_errpipe(lex *l, node *n)
{
	redir *r, **rt;
	word *w;
	part *pt;

	while (n && n->k == N_PIPE && n->r)
		n = n->r;
	if (!n)
		return;
	r = ar_alloc(l->a, sizeof *r);
	w = ar_alloc(l->a, sizeof *w);
	pt = ar_alloc(l->a, sizeof *pt);
	pt->k = P_TXT;
	pt->t = ar_dup(l->a, "1", 1);
	pt->n = 1;
	w->p = pt;
	r->k = R_DUP;
	r->fd = 2;
	r->w = w;
	for (rt = &n->rd; *rt; rt = &(*rt)->nx)
		;
	*rt = r;
}

/* Parse the ! and time words in front of a pipeline, each a node to wrap it;
   a time straight after a time is the same timing, as in bash. */
node **p_pfx(lex *l, node **hole, int *np)
{
	char *t;
	node *p, *lp = 0;

	while (l->tk == T_WORD && (t = w_lit(l->w)) &&
	       ((t[0] == '!' && !t[1]) || (t[0] == 't' && !strcmp(t, "time")))) {
		if (t[0] == 't' && lp && lp->k == N_TIME) {
			lx_next(l);
			continue;
		}
		(*np)++;
		if (++l->depth > HIBR_DEPTH) {
			perr(l, "input nested too deeply");
			return hole;
		}
		p = nd(l, t[0] == '!' ? N_NOT : N_TIME);
		lx_next(l);
		if (p->k == N_TIME && kw(l, "-p")) {
			p->f = 1;
			lx_next(l);
		}
		if (p->k == N_TIME && kw(l, "--")) {
			p->f = 1;
			lx_next(l);
		}
		*hole = p;
		hole = &p->l;
		lp = p;
	}
	return hole;
}

/* Parse a pipeline with optional leading negation and timing. */
node *p_pipe(lex *l)
{
	node *n, *p, *top = 0, **hole = &top;
	int np = 0;

	if (l->tk == T_WORD) {
		hole = p_pfx(l, &top, &np);
		l->depth -= np;
		if (l->err)
			return 0;
	}
	n = p_cmd(l);
	while (n && (l->tk == T_PIPE || l->tk == T_PIPEAMP)) {
		if (l->tk == T_PIPEAMP)
			p_errpipe(l, n);
		lx_next(l);
		p_nl(l);
		p = nd(l, N_PIPE);
		p->l = n;
		p->r = p_cmd(l);
		n = p;
		if (!p->r) {
			perr(l, "expected command after |");
			break;
		}
	}
	if (!top)
		return n;
	if (!n && !(top->k == N_TIME && hole == &top->l))
		return 0;
	*hole = n;
	return top;
}

/* Whether an and-or list keeps its own source text: a function definition (declare -f prints it), a simple command (a DEBUG trap's $CMD), a list run in the background or anything an interactive shell runs (a job's name). Every list kept it, so a compound command's text was held once per level of nesting -- 3.7 MB copied for the desktop (Gitea #463). */
int p_keeptx(lex *l, node *n)
{
	return n->k == N_FUNC || n->k == N_CMD || l->tk == T_AMP ||
	       (l->s && l->s->it);
}

/* Parse an && / || chain. */
node *p_andor(lex *l)
{
	const char *b = l->tkb;
	node *n = p_pipe(l);
	node *a;
	const char *e;
	int k;

	while (n && (l->tk == T_AND || l->tk == T_OR)) {
		k = l->tk == T_AND ? N_AND : N_OR;
		lx_next(l);
		p_nl(l);
		a = nd(l, k);
		a->l = n;
		a->r = p_pipe(l);
		n = a;
		if (!a->r) {
			perr(l, "expected command after operator");
			break;
		}
	}
	if (n && p_keeptx(l, n)) {
		e = l->tkb;
		while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
			e--;
		n->tx = ar_dup(l->a, b, (size_t)(e - b));
	}
	return n;
}

/* Append a command to a sequence, leaning right -- a; (b; (c; d)) -- so
   ex runs it in a loop rather than recursing once per command, which
   overflowed the stack for a function of 100,000 lines (Gitea #428). tail
   is the sequence's last N_SEQ, kept by the caller. */
node *p_seq(lex *l, node *a, node *b, node **tail)
{
	node *q;

	if (!a)
		return b;
	q = nd(l, N_SEQ);
	q->r = b;
	if (!*tail) {
		q->l = a;
		*tail = q;
		return q;
	}
	q->l = (*tail)->r;
	(*tail)->r = q;
	*tail = q;
	return a;
}

/* Parse a sequence of commands separated by ; & or newline. */
node *p_list(lex *l)
{
	node *n = 0, *r, *q, *tail = 0;

	p_nl(l);
	if (p_end(l))
		return 0;
	for (;;) {
		r = p_andor(l);
		if (!r)
			break;
		if (l->tk == T_AMP) {
			q = nd(l, N_BG);
			q->l = r;
			q->tx = r->tx;
			r = q;
			lx_next(l);
		} else if (l->tk == T_SEMI || l->tk == T_NL) {
			lx_next(l);
		} else {
			n = p_seq(l, n, r, &tail);
			break;
		}
		n = p_seq(l, n, r, &tail);
		p_nl(l);
		if (l->err || p_end(l))
			break;
	}
	return n;
}

/* Parse one complete command line, leaving the newline that ends it unread. */
node *p_line(lex *l)
{
	node *n = 0, *r, *q, *tail = 0;

	for (;;) {
		if (p_end(l))
			break;
		r = p_andor(l);
		if (!r)
			break;
		if (l->tk == T_AMP) {
			q = nd(l, N_BG);
			q->l = r;
			q->tx = r->tx;
			r = q;
			lx_next(l);
		} else if (l->tk == T_SEMI) {
			lx_next(l);
		} else {
			n = p_seq(l, n, r, &tail);
			break;
		}
		n = p_seq(l, n, r, &tail);
		if (l->err || l->tk == T_NL || l->tk == T_EOF)
			break;
	}
	if (!l->err && !l->more && l->tk != T_NL && l->tk != T_EOF)
		perr_near(l);
	return n;
}

/* Parse a complete input unit, reporting incomplete input. */
node *hibr_parse(sh *s, const char *src, int *more)
{
	lex l;
	node *n;

	if (more)
		*more = 0;
	lx_init(&l, s, src);
	lx_next(&l);
	p_nl(&l);
	if (l.tk == T_EOF && !l.err)
		return 0;
	n = p_list(&l);
	if (l.more) {
		v_free(&l.hq);
		if (more)
			*more = 1;
		return 0;
	}
	if (!l.err && l.tk != T_EOF)
		perr_near(&l);
	v_free(&l.hq);
	if (l.err) {
		s->st = 2;
		return 0;
	}
	return n;
}
