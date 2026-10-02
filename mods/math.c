#define _GNU_SOURCE

#include "hibr.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MT_DEPTH
#define MT_DEPTH 200
#endif


typedef struct mt_p mt_p;
struct mt_p {
	sh *s;
	const char *p;
	int depth;
	char *err;
};

double mt_tern(mt_p *m);

/* Say what went wrong, the first time only. */
void mt_fail(mt_p *m, const char *what, const char *at)
{
	str e;

	if (m->err)
		return;
	s_init(&e);
	s_cat(&e, what);
	if (at && *at) {
		s_cat(&e, " near '");
		s_add(&e, at, strcspn(at, " \t") < 12 ? strcspn(at, " \t") : 12);
		s_ch(&e, '\'');
	}
	m->err = xs(e.p ? e.p : what);
	s_free(&e);
}

/* Say what went wrong about a name, the first time only. */
void mt_failn(mt_p *m, const char *pre, const char *nm, const char *post)
{
	str e;

	if (m->err)
		return;
	s_init(&e);
	s_cat(&e, pre);
	s_cat(&e, nm);
	s_cat(&e, post);
	m->err = xs(e.p);
	s_free(&e);
}

/* Skip blanks. */
void mt_ws(mt_p *m)
{
	while (*m->p == ' ' || *m->p == '\t' || *m->p == '\n')
		m->p++;
}

/* Whether the next text is op, taking it if so. */
int mt_eat(mt_p *m, const char *op)
{
	size_t n = strlen(op);

	mt_ws(m);
	if (strncmp(m->p, op, n))
		return 0;
	m->p += n;
	return 1;
}

/* Text as a number: the whole of it, blanks either side allowed. */
int mt_num(const char *t, double *v)
{
	char *e;

	while (*t == ' ' || *t == '\t')
		t++;
	if (!*t)
		return 0;
	errno = 0;
	*v = strtod(t, &e);
	while (*e == ' ' || *e == '\t')
		e++;
	return !*e;
}

/* The numbers a name holds: its own value, or every element of an array,
   for the functions that take a list. */
int mt_vals(mt_p *m, const char *nm, vec *out)
{
	vec t = { 0, 0, 0 };
	size_t i;

	hibr_list(m->s, nm, 0, 0, &t, 0);
	if (!t.n) {
		v_free(&t);
		return 0;
	}
	for (i = 0; i < t.n; i++)
		v_add(out, xs(t.p[i] ? t.p[i] : ""));
	v_free(&t);
	return 1;
}

/* A function of a list: sum, avg, min, max or count, over numbers and
   arrays alike; text that is not a number is passed over, as a spreadsheet
   does, and count counts only numbers. */
double mt_listfn(mt_p *m, const char *fn)
{
	double acc = 0, x;
	long n = 0;
	int first = 1;
	vec vs;
	size_t i;
	const char *start;

	if (!mt_eat(m, ")")) {
		do {
			mt_ws(m);
			start = m->p;
			vs.p = 0;
			vs.n = vs.cap = 0;
			if ((*m->p >= 'A' && *m->p <= 'Z') || (*m->p >= 'a' && *m->p <= 'z') ||
			    *m->p == '_') {
				const char *e = m->p;
				while ((*e >= 'A' && *e <= 'Z') || (*e >= 'a' && *e <= 'z') ||
				       (*e >= '0' && *e <= '9') || *e == '_')
					e++;
				const char *after = e;
				while (*after == ' ' || *after == '\t')
					after++;
				if ((*after == ',' || *after == ')') && e > m->p) {
					char *nm = xm((size_t)(e - m->p) + 1);
					memcpy(nm, m->p, (size_t)(e - m->p));
					nm[e - m->p] = 0;
					if (strcmp(nm, "pi") && strcmp(nm, "e") &&
					    mt_vals(m, nm, &vs)) {
						m->p = e;
						free(nm);
						for (i = 0; i < vs.n; i++) {
							if (mt_num(vs.p[i], &x)) {
								if (!strcmp(fn, "min"))
									acc = first || x < acc ? x : acc;
								else if (!strcmp(fn, "max"))
									acc = first || x > acc ? x : acc;
								else
									acc += x;
								first = 0;
								n++;
							}
							free(vs.p[i]);
						}
						v_free(&vs);
						continue;
					}
					free(nm);
				}
			}
			m->p = start;
			x = mt_tern(m);
			if (!strcmp(fn, "min"))
				acc = first || x < acc ? x : acc;
			else if (!strcmp(fn, "max"))
				acc = first || x > acc ? x : acc;
			else
				acc += x;
			first = 0;
			n++;
		} while (!m->err && mt_eat(m, ","));
		if (!m->err && !mt_eat(m, ")"))
			mt_fail(m, "math: a ) is missing", m->p);
	}
	if (!strcmp(fn, "count"))
		return (double)n;
	if (!strcmp(fn, "avg")) {
		if (!n)
			mt_fail(m, "math: avg of nothing", 0);
		return n ? acc / (double)n : 0;
	}
	if (first && (!strcmp(fn, "min") || !strcmp(fn, "max")))
		mt_fail(m, "math: min or max of nothing", 0);
	return acc;
}

/* A named function's arguments, after its (. */
double mt_call(mt_p *m, const char *fn)
{
	double a[2] = { 0, 0 };
	int n = 0;

	if (!strcmp(fn, "sum") || !strcmp(fn, "avg") || !strcmp(fn, "min") ||
	    !strcmp(fn, "max") || !strcmp(fn, "count"))
		return mt_listfn(m, fn);
	if (!mt_eat(m, ")")) {
		do {
			if (n == 2) {
				mt_failn(m, "math: too many arguments to ", fn, "");
				return 0;
			}
			a[n++] = mt_tern(m);
		} while (!m->err && mt_eat(m, ","));
		if (!m->err && !mt_eat(m, ")"))
			mt_fail(m, "math: a ) is missing", m->p);
	}
	if (m->err)
		return 0;
	if (!strcmp(fn, "round")) {
		double f = pow(10, n > 1 ? a[1] : 0);
		return round(a[0] * f) / f;
	}
	if (n != 1 && strcmp(fn, "pow") && strcmp(fn, "atan2") &&
	    strcmp(fn, "hypot")) {
		mt_failn(m, "math: ", fn, " takes one argument");
		return 0;
	}
	if (n != 2 && (!strcmp(fn, "pow") || !strcmp(fn, "atan2") ||
		       !strcmp(fn, "hypot"))) {
		mt_failn(m, "math: ", fn, " takes two arguments");
		return 0;
	}
	if (!strcmp(fn, "abs")) return fabs(a[0]);
	if (!strcmp(fn, "ceil")) return ceil(a[0]);
	if (!strcmp(fn, "floor")) return floor(a[0]);
	if (!strcmp(fn, "trunc")) return trunc(a[0]);
	if (!strcmp(fn, "sqrt")) return sqrt(a[0]);
	if (!strcmp(fn, "cbrt")) return cbrt(a[0]);
	if (!strcmp(fn, "exp")) return exp(a[0]);
	if (!strcmp(fn, "ln")) return log(a[0]);
	if (!strcmp(fn, "log") || !strcmp(fn, "log10")) return log10(a[0]);
	if (!strcmp(fn, "log2")) return log2(a[0]);
	if (!strcmp(fn, "sin")) return sin(a[0]);
	if (!strcmp(fn, "cos")) return cos(a[0]);
	if (!strcmp(fn, "tan")) return tan(a[0]);
	if (!strcmp(fn, "asin")) return asin(a[0]);
	if (!strcmp(fn, "acos")) return acos(a[0]);
	if (!strcmp(fn, "atan")) return atan(a[0]);
	if (!strcmp(fn, "pow")) return pow(a[0], a[1]);
	if (!strcmp(fn, "atan2")) return atan2(a[0], a[1]);
	if (!strcmp(fn, "hypot")) return hypot(a[0], a[1]);
	mt_failn(m, "math: no function ", fn, "");
	return 0;
}

/* A number, a name, a call, or a parenthesised expression. */
double mt_atom(mt_p *m)
{
	const char *b;
	char *e, *nm;
	double v = 0;
	const char *val;

	mt_ws(m);
	if (mt_eat(m, "(")) {
		if (++m->depth > MT_DEPTH) {
			mt_fail(m, "math: nested too deep", 0);
			return 0;
		}
		v = mt_tern(m);
		m->depth--;
		if (!m->err && !mt_eat(m, ")"))
			mt_fail(m, "math: a ) is missing", m->p);
		return v;
	}
	b = m->p;
	if ((*b >= '0' && *b <= '9') || *b == '.') {
		v = strtod(b, &e);
		if (e == b) {
			mt_fail(m, "math: not a number", b);
			return 0;
		}
		m->p = e;
		return v;
	}
	if (!((*b >= 'A' && *b <= 'Z') || (*b >= 'a' && *b <= 'z') || *b == '_')) {
		mt_fail(m, *b ? "math: unexpected" : "math: an expression ends too soon",
			*b ? b : 0);
		return 0;
	}
	while ((*m->p >= 'A' && *m->p <= 'Z') || (*m->p >= 'a' && *m->p <= 'z') ||
	       (*m->p >= '0' && *m->p <= '9') || *m->p == '_')
		m->p++;
	nm = xm((size_t)(m->p - b) + 1);
	memcpy(nm, b, (size_t)(m->p - b));
	nm[m->p - b] = 0;
	if (mt_eat(m, "(")) {
		if (++m->depth > MT_DEPTH) {
			mt_fail(m, "math: nested too deep", 0);
			free(nm);
			return 0;
		}
		v = mt_call(m, nm);
		m->depth--;
		free(nm);
		return v;
	}
	if (!strcmp(nm, "pi")) {
		free(nm);
		return M_PI;
	}
	if (!strcmp(nm, "e")) {
		free(nm);
		return M_E;
	}
	val = hibr_get(m->s, nm);
	if (!val || !*val) {
		mt_failn(m, "math: ", nm, " has no value");
		free(nm);
		return 0;
	}
	if (!mt_num(val, &v))
		mt_failn(m, "math: ", nm, " is not a number");
	free(nm);
	return v;
}

/* A sign, a not, or a power, which binds tighter than either and is taken
   from the right: -2^2 is -4, 2^3^2 is 512. */
double mt_unary(mt_p *m)
{
	double b, v;

	if (mt_eat(m, "-"))
		return -mt_unary(m);
	if (mt_eat(m, "+"))
		return mt_unary(m);
	if (mt_eat(m, "!"))
		return !mt_unary(m);
	v = mt_atom(m);
	if (!m->err && mt_eat(m, "^")) {
		if (++m->depth > MT_DEPTH) {
			mt_fail(m, "math: nested too deep", 0);
			return 0;
		}
		b = mt_unary(m);
		m->depth--;
		v = pow(v, b);
	}
	return v;
}

/* * / % */
double mt_mul(mt_p *m)
{
	double v = mt_unary(m), r;

	while (!m->err) {
		if (mt_eat(m, "*")) {
			v *= mt_unary(m);
		} else if (mt_eat(m, "/")) {
			r = mt_unary(m);
			if (!m->err && r == 0) {
				mt_fail(m, "math: division by zero", 0);
				return 0;
			}
			v /= r;
		} else if (mt_eat(m, "%")) {
			r = mt_unary(m);
			if (!m->err && r == 0) {
				mt_fail(m, "math: division by zero", 0);
				return 0;
			}
			v = fmod(v, r);
		} else {
			break;
		}
	}
	return v;
}

/* + - */
double mt_add(mt_p *m)
{
	double v = mt_mul(m);

	while (!m->err) {
		if (mt_eat(m, "+"))
			v += mt_mul(m);
		else if (mt_eat(m, "-"))
			v -= mt_mul(m);
		else
			break;
	}
	return v;
}

/* < <= > >= == !=, each 1 or 0. */
double mt_cmp(mt_p *m)
{
	double v = mt_add(m), r;

	while (!m->err) {
		if (mt_eat(m, "<=")) {
			r = mt_add(m);
			v = v <= r;
		} else if (mt_eat(m, ">=")) {
			r = mt_add(m);
			v = v >= r;
		} else if (mt_eat(m, "==")) {
			r = mt_add(m);
			v = v == r;
		} else if (mt_eat(m, "!=")) {
			r = mt_add(m);
			v = v != r;
		} else if (mt_eat(m, "<")) {
			r = mt_add(m);
			v = v < r;
		} else if (mt_eat(m, ">")) {
			r = mt_add(m);
			v = v > r;
		} else {
			break;
		}
	}
	return v;
}

/* && || */
double mt_logic(mt_p *m)
{
	double v = mt_cmp(m), r;

	while (!m->err) {
		if (mt_eat(m, "&&")) {
			r = mt_cmp(m);
			v = v != 0 && r != 0;
		} else if (mt_eat(m, "||")) {
			r = mt_cmp(m);
			v = v != 0 || r != 0;
		} else {
			break;
		}
	}
	return v;
}

/* c ? a : b, the loosest of all. */
double mt_tern(mt_p *m)
{
	double c = mt_logic(m), a, b;

	if (m->err || !mt_eat(m, "?"))
		return c;
	if (++m->depth > MT_DEPTH) {
		mt_fail(m, "math: nested too deep", 0);
		return 0;
	}
	a = mt_tern(m);
	if (!m->err && !mt_eat(m, ":"))
		mt_fail(m, "math: a ? wants its :", m->p);
	b = m->err ? 0 : mt_tern(m);
	m->depth--;
	return c != 0 ? a : b;
}

/* A result as text, as a spreadsheet shows one: a whole number whole, and
anything else in the fewest digits, at most fifteen significant, that read
back as it -- so 0.1+0.2 is 0.3 -- and never in exponent form between a
millionth and a thousand million million. With scale, exactly that many
decimals. */
void mt_text(double v, int scale, str *o)
{
	char b[64];
	int k, d;
	double r;

	if (isnan(v)) {
		s_cat(o, "nan");
		return;
	}
	if (isinf(v)) {
		s_cat(o, v < 0 ? "-inf" : "inf");
		return;
	}
	if (scale >= 0) {
		if (scale > 30)
			scale = 30;
		r = pow(10, scale);
		if (fabs(v * r) < 1e15)
			v = round(v * r) / r;
		snprintf(b, sizeof b, "%.*f", scale, v);
	} else if (v == trunc(v) && fabs(v) < 1e15) {
		snprintf(b, sizeof b, "%.0f", v);
	} else {
		snprintf(b, sizeof b, "%.15g", v);
		r = strtod(b, 0);
		for (k = 1; k <= 15; k++) {
			snprintf(b, sizeof b, "%.*g", k, v);
			if (strtod(b, 0) == r)
				break;
		}
		if (strchr(b, 'e') && fabs(v) >= 1e-6 && fabs(v) < 1e15) {
			for (d = 0; d <= 20; d++) {
				snprintf(b, sizeof b, "%.*f", d, v);
				if (strtod(b, 0) == r)
					break;
			}
		}
	}
	if (!strcmp(b, "-0"))
		strcpy(b, "0");
	s_cat(o, b);
}

/* math [-s n] expr...: evaluate in floating point; shell variables by name,
   arrays in sum avg min max count. */
int m_math(sh *s, int ac, char **av)
{
	mt_p m;
	str ex, o;
	int i = 1, scale = -1;
	double v;

	if (i < ac && !strcmp(av[i], "-s") && i + 1 < ac) {
		scale = atoi(av[i + 1]);
		i += 2;
	}
	if (i >= ac) {
		lg(HIBR_LERR, "usage: math [-s decimals] expression");
		return 2;
	}
	s_init(&ex);
	for (; i < ac; i++) {
		if (ex.n)
			s_ch(&ex, ' ');
		s_cat(&ex, av[i]);
	}
	memset(&m, 0, sizeof m);
	m.s = s;
	m.p = ex.p ? ex.p : "";
	v = mt_tern(&m);
	mt_ws(&m);
	if (!m.err && *m.p)
		mt_fail(&m, "math: unexpected", m.p);
	if (m.err) {
		lg(HIBR_LERR, "%s", m.err);
		free(m.err);
		s_free(&ex);
		return HIBR_FAIL;
	}
	s_init(&o);
	mt_text(v, scale, &o);
	hibr_ret(s, o.p);
	if (!s->bind)
		printf("%s\n", o.p);
	s_free(&o);
	s_free(&ex);
	return HIBR_OK;
}

const hibr_bi math_bi[] = {
	{ "math", m_math, "floating point arithmetic: math [-s decimals] expr" },
	HIBR_BI_END
};

HIBR_MODULE("math", "1.0", "floating point arithmetic, for when $(( )) is not enough",
	    math_bi, 0, 0);
