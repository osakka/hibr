#include "../../include/pri.h"
#include "inspect.h"
#include "../lint.h"

static const li_api *ix_lint;
const char *p_param(lex *, node *, const char *, const char *);
void j_out(str *, ent *, size_t, const char *, short, int, int);
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct ix_pos ix_pos;
struct ix_pos {
	const void *object;
	size_t start, end;
	ix_pos *next;
};

typedef struct {
	str *out;
	sh *rules;
	const char *rulespath, *ruleidentity;
	sh *caller;
	const char *identity;
	const char *src, *path, *request, *filter, *owner;
	vec names, symbols;
	ix_pos **positions;
	const char *input;
	size_t inputsize, origin;
	size_t start, end, cursor, fncursor, unitstart, unitend;
	size_t *lines, nlines;
	int count, subdepth, partial;
	vec vrecs, vfns, vused, vskip;
	size_t vfn, cstart, cend, wstart, wend;
	int wknown, tested;
} ix_ctx;

typedef struct {
	node *tree;
	size_t start, end;
} ix_unit;

/* Hash a private parser object without exposing it to consumers. */
static size_t ix_bucket(const void *object)
{
	return ((uintptr_t)object >> 3) % 8191;
}

/* Capture original byte positions before parser storage loses provenance. */
static void ix_observe(void *data, const void *object, const char *begin,
		       const char *end)
{
	ix_ctx *c = data;
	ix_pos *p;
	size_t bucket = ix_bucket(object);

	for (p = c->positions[bucket]; p && p->object != object; p = p->next) {
	}
	if ((uintptr_t)begin < (uintptr_t)c->input ||
	    (uintptr_t)end < (uintptr_t)begin ||
	    (uintptr_t)end > (uintptr_t)c->input + c->inputsize) {
		if (p)
			p->start = p->end = (size_t)-1;
		return;
	}
	if (!p) {
		p = xm(sizeof *p);
		p->object = object;
		p->next = c->positions[bucket];
		c->positions[bucket] = p;
	}
	p->start = c->origin + (size_t)(begin - c->input);
	p->end = c->origin + (size_t)(end - c->input);
}

/* Read evidence recorded by the parser rather than matching source text. */
static int ix_position(ix_ctx *c, const void *object, size_t *start, size_t *end)
{
	ix_pos *p;

	for (p = c->positions[ix_bucket(object)]; p; p = p->next)
		if (p->object == object && p->start != (size_t)-1) {
			*start = p->start;
			*end = p->end;
			return 1;
		}
	return 0;
}

/* Append a JSON string. */
static void ix_quote(str *out, const char *text)
{
	s_ch(out, '"');
	lg_jstr(out, text ? text : "");
	s_ch(out, '"');
}

/* Describe the exact bytes read, or leave unavailable identity fields null. */
static void ix_identity(str *out, const char *path, const str *src, int available)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t i;

	s_cat(out, "{\"path\":");
	ix_quote(out, path);
	if (!available) {
		s_cat(out, ",\"size\":null,\"id\":null}");
		return;
	}
	for (i = 0; i < src->n; i++) {
		hash ^= (unsigned char)src->p[i];
		hash *= UINT64_C(1099511628211);
	}
	s_fmt(out, ",\"size\":%lu,\"id\":\"fnv1a64:%016llx\"}", (unsigned long)src->n,
	      (unsigned long long)hash);
}

/* Find a byte offset's line using the source's line index. */
static size_t ix_line(ix_ctx *c, size_t offset)
{
	size_t lo = 0, hi = c->nlines, mid;

	while (lo + 1 < hi) {
		mid = lo + (hi - lo) / 2;
		if (c->lines[mid] <= offset)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* Append a source range using byte-based coordinates. */
static void ix_span(ix_ctx *c, size_t start, size_t end)
{
	size_t line, eline;

	line = ix_line(c, start);
	eline = ix_line(c, end > start ? end - 1 : start);

	s_fmt(c->out,
	      "\"span\":{\"start\":%lu,\"end\":%lu,\"line\":%lu,\"col\":%lu,"
	      "\"endline\":%lu}",
	      (unsigned long)start, (unsigned long)end, (unsigned long)(line + 1),
	      (unsigned long)(start - c->lines[line] + 1), (unsigned long)(eline + 1));
}

/* Read a literal command word without expanding it. */
static const char *ix_literal(word *w)
{
	return w && w->p && !w->p->nx && w->p->k == P_TXT ? w->p->t : 0;
}

/* Collect function names without resolving or loading commands. */
static void ix_names(ix_ctx *c, node *n)
{
	while (n && n->k == N_SEQ) {
		ix_names(c, n->l);
		n = n->r;
	}
	if (!n)
		return;
	if (n->k == N_FUNC && n->s)
		v_add(&c->names, n->s);
	ix_names(c, n->l);
	ix_names(c, n->r);
	if (n->k != N_FUNC)
		ix_names(c, n->x);
}

/* Recognize a function defined in the inspected source. */
static int ix_named(ix_ctx *c, const char *name)
{
	size_t i;

	for (i = 0; name && i < c->names.n; i++)
		if (!strcmp(name, c->names.p[i]))
			return 1;
	return 0;
}

/* Recognize an explicitly supplied literal-reference candidate without resolving it. */
static int ix_candidate(ix_ctx *c, const char *name)
{
	size_t i;

	if (ix_named(c, name))
		return 1;
	for (i = 0; name && i < c->symbols.n; i++)
		if (!strcmp(name, c->symbols.p[i]))
			return 1;
	return 0;
}

/* Locate a node from private parser evidence. */
static size_t ix_start(ix_ctx *c, node *n)
{
	size_t start, end;

	if (ix_position(c, n, &start, &end))
		return start;
	return n->ln && n->ln <= c->nlines ? c->lines[n->ln - 1] : 0;
}

/* Start one evidence record with its source identity and location. */
static void ix_record(ix_ctx *c, size_t start, size_t end, const char *known)
{
	if (c->count++)
		s_ch(c->out, ',');
	s_cat(c->out, "{\"source\":");
	s_cat(c->out, c->identity);
	s_cat(c->out, ",\"known\":");
	ix_quote(c->out, known);
	s_ch(c->out, ',');
	ix_span(c, start, end);
	if (c->subdepth)
		s_cat(c->out, ",\"scope\":\"substitution\"");
}

/* Describe the author's preceding comment without treating it as behavior. */
static void ix_summary(ix_ctx *c, size_t start)
{
	size_t line = ix_line(c, start), first = line, begin, pos, end;
	str text;

	while (first > 0) {
		pos = c->lines[first - 1];
		while (c->src[pos] == ' ' || c->src[pos] == '\t')
			pos++;
		if (c->src[pos] != '#')
			break;
		first--;
	}
	if (first == line)
		return;
	begin = c->lines[first];
	s_init(&text);
	for (; first < line; first++) {
		pos = c->lines[first];
		end = c->lines[first + 1];
		while (pos < end && (c->src[pos] == ' ' || c->src[pos] == '\t'))
			pos++;
		if (pos < end && c->src[pos] == '#')
			pos++;
		if (pos < end && c->src[pos] == ' ')
			pos++;
		while (end > pos &&
		       (c->src[end - 1] == '\n' || c->src[end - 1] == '\r'))
			end--;
		if (text.n)
			s_ch(&text, ' ');
		s_add(&text, c->src + pos, end - pos);
	}
	s_cat(c->out, ",\"summary\":{\"known\":\"declared\",\"text\":");
	ix_quote(c->out, text.p);
	s_cat(c->out, ",\"source\":");
	s_cat(c->out, c->identity);
	s_ch(c->out, ',');
	ix_span(c, begin, start);
	s_ch(c->out, '}');
	s_free(&text);
}

/* Locate a function's body through lexer tokens without reading its contents. */
static void ix_body(ix_ctx *c, node *n, size_t start, size_t end)
{
	sh own;
	lex l;
	const char *first;

	if (!n->tx)
		return;
	memset(&own, 0, sizeof own);
	own.ar = ar_new(4096);
	lx_init(&l, &own, n->tx);
	lx_next(&l);
	first = ix_literal(l.w);
	if (first && (!strcmp(first, "fn") || !strcmp(first, "function")))
		lx_next(&l);
	lx_next(&l);
	if (l.tk == T_LP) {
		lx_span(&l);
		lx_arrow(&l);
		lx_next(&l);
	}
	while (l.tk == T_NL)
		lx_next(&l);
	if (l.tkb && l.tk != T_EOF) {
		s_cat(c->out, ",\"body\":{");
		ix_span(c, start + (size_t)(l.tkb - n->tx), end);
		s_ch(c->out, '}');
	}
	v_free(&l.hq);
	ar_free(own.ar);
}

/* Recover a declared parameter's span and its default's with the same parser. */
static int ix_paramat(node *function, int index, size_t start, size_t *from,
		      size_t *to, size_t *defat, str *deftext)
{
	sh own;
	lex l;
	node parameter;
	char *signature;
	const char *at, *next = 0, *equal, *last;
	size_t base;
	int i, found = 0;

	memset(&own, 0, sizeof own);
	own.ar = ar_new(4096);
	lx_init(&l, &own, function->tx ? function->tx : "");
	lx_next(&l);
	lx_next(&l);
	lx_next(&l);
	base = start + (size_t)(l.p - (function->tx ? function->tx : ""));
	signature = l.tk == T_LP ? lx_span(&l) : 0;
	at = signature;
	*defat = (size_t)-1;
	for (i = 0; at && i <= index; i++) {
		while (*at == ' ' || *at == '\t' || *at == '\n' || *at == ',')
			at++;
		memset(&parameter, 0, sizeof parameter);
		next = p_param(&l, &parameter, at, signature + strlen(signature));
		if (!next)
			break;
		if (i == index) {
			last = next;
			while (last > at && (last[-1] == ',' || last[-1] == ' ' ||
					     last[-1] == '\t' || last[-1] == '\n'))
				last--;
			*from = base + (size_t)(at - signature);
			*to = base + (size_t)(last - signature);
			equal = parameter.w ? memchr(at, '=', (size_t)(last - at)) : 0;
			if (equal) {
				equal++;
				while (equal < last && (*equal == ' ' || *equal == '\t'))
					equal++;
				*defat = base + (size_t)(equal - signature);
				s_add(deftext, equal, (size_t)(last - equal));
			}
			found = 1;
			break;
		}
		at = next;
	}
	v_free(&l.hq);
	ar_free(own.ar);
	return found;
}

/* Recover exact declared parameter and default spans using the same parser. */
static void ix_parameter(ix_ctx *c, node *function, int index, size_t start, size_t end)
{
	size_t from, to, defat;
	str text;

	s_init(&text);
	s_ch(c->out, ',');
	if (!ix_paramat(function, index, start, &from, &to, &defat, &text)) {
		ix_span(c, start, end);
		s_free(&text);
		return;
	}
	ix_span(c, from, to);
	if (defat != (size_t)-1) {
		s_cat(c->out, ",\"default\":{\"known\":\"declared\","
			      "\"evaluated\":false,\"text\":");
		ix_quote(c->out, text.p);
		s_ch(c->out, ',');
		ix_span(c, defat, to);
		s_cat(c->out, ",\"source\":");
		s_cat(c->out, c->identity);
		s_ch(c->out, '}');
	} else
		s_cat(c->out, ",\"default\":null");
	s_free(&text);
}

/* Copy only the dynamic command word, keeping argument literals private. */
static void ix_expression(ix_ctx *c, node *n)
{
	sh own;
	lex l;
	word *w;
	str text;

	memset(&own, 0, sizeof own);
	own.ar = ar_new(4096);
	lx_init(&l, &own, n->tx ? n->tx : "");
	lx_next(&l);
	for (w = n->aw; w; w = w->nx)
		lx_next(&l);
	if (n->f == 2) {
		lx_next(&l);
		lx_next(&l);
	}
	s_init(&text);
	if (l.tkb && l.p >= l.tkb)
		s_add(&text, l.tkb, (size_t)(l.p - l.tkb));
	ix_quote(c->out, text.p);
	s_free(&text);
	v_free(&l.hq);
	ar_free(own.ar);
}

/* Locate the original command word from lexer evidence. */
static int ix_callspan(ix_ctx *c, node *n, size_t *start, size_t *end)
{
	return ix_position(c, n->w, start, end);
}

/* Interpret registration argument positions from the supplied convention data. */
static const char *ix_registration(ix_ctx *c, node *n, const char **role)
{
	vec roles = {0};
	char **keys;
	const char *command = ix_literal(n->w), *value, *prefix = 0;
	word *w;
	size_t i;
	long position;

	if (!c->rules || !command)
		return 0;
	keys = xm(4 * sizeof *keys);
	keys[0] = "roles";
	hibr_list(c->rules, "RULES", keys, 1, &roles, 1);
	for (i = 0; i < roles.n; i++) {
		keys[1] = roles.p[i];
		keys[2] = "registrations";
		keys[3] = (char *)command;
		value = hibr_getp(c->rules, "RULES", keys, 4);
		position = value ? strtol(value, 0, 10) : 0;
		if (position <= 0 || position > 4096)
			continue;
		for (w = n->w; w && position; position--)
			w = w->nx;
		prefix = ix_literal(w);
		if (prefix && isname(prefix)) {
			*role = roles.p[i];
			break;
		}
		prefix = 0;
	}
	free(keys);
	v_free(&roles);
	return prefix;
}

/* Describe a data-derived registration as an inference, not a language fact. */
static void ix_registration_record(ix_ctx *c, node *n)
{
	const char *role = 0, *prefix = ix_registration(c, n, &role);

	if (!prefix)
		return;
	s_cat(c->out, ",\"registration\":{\"known\":\"inferred\",\"prefix\":");
	ix_quote(c->out, prefix);
	s_cat(c->out, ",\"role\":");
	ix_quote(c->out, role);
	s_cat(c->out, ",\"rule\":");
	ix_quote(c->out, role);
	s_cat(c->out, ",\"rules_path\":");
	ix_quote(c->out, c->rulespath);
	s_cat(c->out, ",\"rules_source\":");
	s_cat(c->out, c->ruleidentity);
	s_ch(c->out, '}');
}

/* Infer callback edges only from an explicitly supplied desktop rule file. */
static void ix_convention(ix_ctx *c, node *n, size_t start, size_t end)
{
	const char *prefix, *role = 0, *id;
	vec suffixes = {0};
	char **keys;
	str target, rule;
	size_t i;

	if (!c->rules || strcmp(c->request, "calls"))
		return;
	prefix = ix_registration(c, n, &role);
	if (!prefix)
		return;
	keys = xm(5 * sizeof *keys);
	keys[0] = "roles";
	keys[1] = (char *)role;
	keys[2] = "callbacks";
	hibr_list(c->rules, "RULES", keys, 3, &suffixes, 1);
	for (i = 0; i < suffixes.n; i++) {
		s_init(&target);
		s_init(&rule);
		s_fmt(&target, "%s_%s", prefix, (char *)suffixes.p[i]);
		keys[3] = suffixes.p[i];
		keys[4] = "id";
		id = hibr_getp(c->rules, "RULES", keys, 5);
		if (id && *id)
			s_cat(&rule, id);
		else
			s_fmt(&rule, "%s.%s", role, (char *)suffixes.p[i]);
		if (ix_named(c, target.p)) {
			ix_record(c, start, end, "inferred");
			s_cat(c->out, ",\"name\":");
			ix_quote(c->out, target.p);
			s_cat(c->out, ",\"rule\":");
			ix_quote(c->out, rule.p);
			s_cat(c->out, ",\"rules_path\":");
			ix_quote(c->out, c->rulespath);
			s_cat(c->out, ",\"rules_source\":");
			s_cat(c->out, c->ruleidentity);
			s_cat(c->out, ",\"resolution\":\"function\",\"dynamic\":false,"
				      "\"bound\":false}");
		}
		s_free(&target);
		s_free(&rule);
	}
	v_free(&suffixes);
	free(keys);
}

static void ix_walk(ix_ctx *, node *);
static void iv_node(ix_ctx *, node *);

/* Parse one substitution's unmodified bytes with their original source coordinates. */
static void ix_subst(ix_ctx *c, part *p, size_t *search)
{
	sh own;
	lex l;
	node *tree;
	const char *input;
	size_t start, end, names, origin, inputsize;
	int lv, vars = !strcmp(c->request, "vars");

	if (!ix_position(c, p, &start, &end)) {
		c->partial = 1;
		return;
	}
	if (end - start != strlen(p->t) || memcmp(c->src + start, p->t, end - start) ||
	    c->subdepth >= 32) {
		c->partial = 1;
		if (vars)
			return;
		ix_record(c, start, end, "syntax");
		s_cat(c->out, ",\"name\":null,\"dynamic\":true,"
			      "\"resolution\":\"unresolved\",\"reason\":"
			      "\"transformed-or-deep-substitution\"}");
		return;
	}
	names = c->names.n;
	input = c->input;
	origin = c->origin;
	inputsize = c->inputsize;
	c->input = p->t;
	c->origin = start;
	c->inputsize = end - start;
	memset(&own, 0, sizeof own);
	own.ar = ar_new(4096);
	own.ln = (unsigned)(ix_line(c, start) + 1);
	lx_init(&l, &own, p->t);
	l.observe = ix_observe;
	l.observer = c;
	lv = hibr_lv;
	hibr_lv = -1;
	lx_next(&l);
	tree = p_list(&l);
	c->subdepth++;
	if (tree && !l.err && !l.more) {
		ix_names(c, tree);
		if (vars)
			iv_node(c, tree);
		else
			ix_walk(c, tree);
	} else {
		c->partial = 1;
		if (!vars) {
			ix_record(c, start, end, "syntax");
			s_cat(c->out, ",\"name\":null,\"dynamic\":true,"
				      "\"resolution\":\"unresolved\",\"reason\":"
				      "\"substitution-not-analysed\"}");
		}
	}
	c->subdepth--;
	c->names.n = names;
	c->input = input;
	c->origin = origin;
	c->inputsize = inputsize;
	hibr_lv = lv;
	v_free(&l.hq);
	ar_free(own.ar);
	*search = end;
}

/* Parse unmodified substitution bytes with their original source coordinates. */
static void ix_words(ix_ctx *c, word *w, size_t *search)
{
	part *p;

	for (; w; w = w->nx)
		for (p = w->p; p; p = p->nx) {
			if ((p->k == P_CMD || p->k == P_PSUB) && p->t && *p->t)
				ix_subst(c, p, search);
			ix_words(c, p->arg, search);
			ix_words(c, p->idx, search);
		}
}

/* Identify command-name code whose literal arguments must remain private. */
static int ix_namecode(part *p)
{
	word *w;

	for (; p; p = p->nx) {
		if (p->k == P_CMD || p->k == P_PSUB)
			return 1;
		for (w = p->arg; w; w = w->nx)
			if (ix_namecode(w->p))
				return 1;
		for (w = p->idx; w; w = w->nx)
			if (ix_namecode(w->p))
				return 1;
	}
	return 0;
}

/* Walk parsed units to produce function, call and literal-reference records. */
/* Whether a node's first child's status is tested, as the shell tests it: a
   condition of if, while and until, the left of && and ||, what ! negates;
   a pipeline, a group, a subshell and time pass on what their own status
   is; anything else -- a loop's body, a background job -- is not. */
static int ix_testsl(int k, int t)
{
	if (k == N_AND || k == N_OR || k == N_IF || k == N_WHILE || k == N_UNTIL || k == N_NOT)
		return 1;
	return k == N_PIPE || k == N_GRP || k == N_SUB || k == N_TIME ? t : 0;
}

static void ix_walk(ix_ctx *c, node *n)
{
	node *p;
	word *w;
	const char *was, *name, *kind;
	size_t start, end, search;
	redir *r;
	int count, located, t = c->tested;

	while (n && n->k == N_SEQ) {
		c->tested = 0;
		ix_walk(c, n->l);
		c->tested = t;
		n = n->r;
	}
	if (!n)
		return;
	start = ix_start(c, n);
	end = n->tx ? start + strlen(n->tx) : start;
	ix_position(c, n, &start, &end);
	if (end > c->end)
		end = c->end;
	if (n->k == N_FUNC) {
		c->fncursor = start + 1;
		was = c->owner;
		c->owner = n->s;
		if (!strcmp(c->request, "functions")) {
			ix_record(c, start, end, "syntax");
			s_cat(c->out, ",\"name\":");
			ix_quote(c->out, n->s);
			if (!c->subdepth)
				s_fmt(c->out, ",\"line\":%u", n->ln);
			s_cat(c->out, ",\"params\":[");
			for (p = n->x, count = 0; p; p = p->x) {
				if (count++)
					s_ch(c->out, ',');
				s_cat(c->out, "{\"name\":");
				ix_quote(c->out, p->s);
				s_cat(c->out, ",\"known\":\"declared\",\"type\":");
				ix_quote(c->out, p->tx);
				s_fmt(c->out, ",\"rest\":%s,\"has_default\":%s",
				      p->f & TY_REST ? "true" : "false",
				      p->w ? "true" : "false");
				s_cat(c->out, ",\"source\":");
				s_cat(c->out, c->identity);
				ix_parameter(c, n, count - 1, start, end);
				s_ch(c->out, '}');
			}
			s_ch(c->out, ']');
			ix_summary(c, start);
			ix_body(c, n, start, end);
			s_fmt(c->out, ",\"declared_signature\":%s",
			      n->f == 1 ? "true" : "false");
			s_ch(c->out, '}');
		}
		c->tested = 0;
		ix_walk(c, n->r);
		c->tested = t;
		c->owner = was;
		return;
	}
	search = start;
	if ((!strcmp(c->request, "calls") || !strcmp(c->request, "refs")) && n->aw)
		ix_words(c, n->aw, &search);
	if (n->k == N_CMD && n->w &&
	    (!c->filter || (c->owner && !strcmp(c->filter, c->owner)))) {
		name = ix_literal(n->w);
		located = ix_callspan(c, n, &start, &end);
		ix_convention(c, n, start, end);
		if (!strcmp(c->request, "calls")) {
			kind = !name			 ? "unresolved"
			       : ix_named(c, name)	 ? "function"
			       : bi_find(name)		 ? "builtin"
			       : m_find(c->caller, name) ? "module"
							 : "unresolved";
			ix_record(c, start, end, "syntax");
			s_cat(c->out, ",\"name\":");
			ix_quote(c->out, name);
			if (!c->subdepth)
				s_fmt(c->out, ",\"line\":%u", n->ln);
			s_cat(c->out, ",\"owner\":");
			ix_quote(c->out, c->owner);
			s_cat(c->out, ",\"resolution\":");
			ix_quote(c->out, kind);
			s_fmt(c->out, ",\"dynamic\":%s,\"bound\":%s,\"tested\":%s",
			      name ? "false" : "true", n->f == 2 ? "true" : "false",
			      c->tested ? "true" : "false");
			if (n->f == 2) {
				s_cat(c->out, ",\"binding\":");
				if (n->s)
					ix_quote(c->out, n->s);
				else {
					size_t bs, be;
					s_cat(c->out, "null");
					if (n->bw && ix_position(c, n->bw, &bs, &be)) {
						s_cat(c->out, ",\"binding_source\":{");
						ix_span(c, bs, be);
						s_ch(c->out, '}');
					}
				}
			}
			if (!name) {
				s_cat(c->out, ",\"expression\":");
				if (ix_namecode(n->w->p))
					s_cat(c->out, "null,\"expression_state\":"
						      "\"source-span-required\"");
				else if (located && end > start) {
					str expression;
					s_init(&expression);
					s_add(&expression, c->src + start, end - start);
					ix_quote(c->out, expression.p);
					s_free(&expression);
				} else
					ix_expression(c, n);
			}
			ix_registration_record(c, n);
			s_ch(c->out, '}');
		}
		if (!strcmp(c->request, "refs"))
			for (w = n->w->nx; w; w = w->nx)
				if ((name = ix_literal(w)) && ix_candidate(c, name)) {
					ix_position(c, w, &start, &end);
					ix_record(c, start, end, "syntax");
					s_cat(c->out, ",\"name\":");
					ix_quote(c->out, name);
					if (!c->subdepth)
						s_fmt(c->out, ",\"line\":%u", n->ln);
					s_cat(c->out, ",\"owner\":");
					ix_quote(c->out, c->owner);
					s_cat(c->out, ",\"resolution\":");
					ix_quote(c->out, ix_named(c, name)
							     ? "function"
							     : "unresolved");
					s_cat(c->out, ",\"lookup\":");
					ix_quote(c->out, ix_named(c, name)
							     ? "inspected-file"
							     : "supplied-symbol");
					s_ch(c->out, '}');
				}
	}
	if (!strcmp(c->request, "calls") || !strcmp(c->request, "refs"))
		ix_words(c, n->w, &search);
	c->tested = ix_testsl(n->k, t);
	ix_walk(c, n->l);
	c->tested = n->k == N_AND || n->k == N_OR || n->k == N_PIPE ? t : 0;
	ix_walk(c, n->r);
	c->tested = 0;
	ix_walk(c, n->x);
	c->tested = t;
	if (!strcmp(c->request, "calls") || !strcmp(c->request, "refs"))
		for (r = n->rd; r; r = r->nx)
			ix_words(c, r->w, &search);
}

/* A variable fact; the record format gives the first two subscripts, so two. */
typedef struct {
	char *name, *owner, *key[2], *verb, *callee;
	const char *access, *form, *known, *rule, *lookup;
	size_t start, end, fn, seq, arg;
	unsigned subs;
	char quote[2];
	int exact, lexical, subdepth;
} iv_rec;

/* A function's declared parameters, its locals and the arguments it takes by name. */
typedef struct {
	char *name;
	vec params, locals, byname;
} iv_fn;

/* A target word as read: the record format gives the first two subscripts, so two. */
typedef struct {
	str name, key[2];
	size_t ks[2], ke[2];
	char quote[2];
	unsigned subs;
	int append;
} iv_tgt;

/* Whether the source holds exactly these bytes at an offset. */
static int iv_at(ix_ctx *c, size_t at, const char *text, size_t n)
{
	return at != (size_t)-1 && at <= c->end && n <= c->end - at &&
	       !memcmp(c->src + at, text, n);
}

/* Whether a source byte can continue a variable name. */
static int iv_namech(ix_ctx *c, size_t at)
{
	unsigned char ch = at < c->end ? (unsigned char)c->src[at] : 0;

	return isalnum(ch) || ch == '_';
}

/* Whether a vector of names holds one. */
static int iv_has(vec *v, const char *name)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		if (!strcmp(v->p[i], name))
			return 1;
	return 0;
}

/* Record one variable fact; an unknown start takes the enclosing word or command. */
static iv_rec *iv_add(ix_ctx *c, const char *name, const char *access, const char *form,
		      size_t start, size_t end, int exact)
{
	iv_rec *r;

	if (!name || !*name)
		return 0;
	r = xm(sizeof *r);
	memset(r, 0, sizeof *r);
	if (start == (size_t)-1) {
		start = c->wknown ? c->wstart : c->cstart;
		end = c->wknown ? c->wend : c->cend;
		exact = 0;
	}
	r->name = xs(name);
	r->owner = xs(c->owner ? c->owner : "");
	r->access = access;
	r->form = form;
	r->known = "syntax";
	r->start = start;
	r->end = end < start ? start : end;
	r->exact = exact;
	r->fn = c->vfn;
	r->seq = c->vrecs.n;
	r->subdepth = c->subdepth;
	v_add(&c->vrecs, r);
	return r;
}

/* Give a fact one of its first two subscripts. */
static void iv_key(iv_rec *r, unsigned k, const char *text, char quote)
{
	if (!r || k > 1)
		return;
	r->key[k] = text ? xs(text) : 0;
	r->quote[k] = quote;
}

/* The current function's frame. */
static iv_fn *iv_frame(ix_ctx *c)
{
	return c->vfns.p[c->vfn];
}

/* Note that the current function treats an argument position as a variable's name. */
static void iv_byname(ix_ctx *c, part *p)
{
	iv_fn *f = iv_frame(c);
	size_t i, position = 0;
	char *end;

	if (!c->vfn || !p || p->k != P_VAR || !p->t)
		return;
	if (isdigit((unsigned char)*p->t)) {
		position = strtoul(p->t, &end, 10);
		if (*end)
			position = 0;
	} else
		for (i = 0; i < f->params.n; i++)
			if (!strcmp(f->params.p[i], p->t))
				position = i + 1;
	if (!position)
		return;
	for (i = 0; i < f->byname.n; i++)
		if ((size_t)(uintptr_t)f->byname.p[i] == position)
			return;
	lg(HIBR_LDBG, "inspect: %s takes argument %lu by name", f->name, (unsigned long)position);
	v_add(&f->byname, (void *)(uintptr_t)position);
}

/* Flatten a word into its characters, marking quoted and expanded ones. */
static void iv_cells(word *w, str *text, str *kind)
{
	part *p;
	size_t i;

	for (p = w ? w->p : 0; p; p = p->nx)
		if (p->k == P_TXT)
			for (i = 0; i < p->n; i++) {
				s_ch(text, p->t[i]);
				s_ch(kind, p->q ? 'q' : 'u');
			}
		else {
			s_ch(text, '$');
			s_ch(kind, 'd');
		}
}

/* Read a target's name, subscripts and += from a word's parts. */
static int iv_parse(word *w, int quoted, int bare, iv_tgt *t)
{
	str text, kind;
	size_t i = 0, n;
	int depth, literal, allq;
	str *key;

	memset(t, 0, sizeof *t);
	s_init(&text);
	s_init(&kind);
	iv_cells(w, &text, &kind);
	n = text.n;
	while (!bare && i < n && kind.p[i] != 'd' && (kind.p[i] == 'u' || quoted) &&
	       (isalpha((unsigned char)text.p[i]) || text.p[i] == '_' ||
		(i && isdigit((unsigned char)text.p[i]))))
		s_ch(&t->name, text.p[i++]);
	while ((bare || i) && i < n && kind.p[i] != 'd' && (kind.p[i] == 'u' || quoted) &&
	       text.p[i] == '[') {
		depth = 1;
		literal = 1;
		allq = 1;
		key = t->subs < 2 ? &t->key[t->subs] : 0;
		for (i++; i < n; i++) {
			if (kind.p[i] != 'd' && (kind.p[i] == 'u' || quoted)) {
				if (text.p[i] == '[')
					depth++;
				else if (text.p[i] == ']' && !--depth)
					break;
			}
			if (kind.p[i] == 'd')
				literal = 0;
			else {
				if (kind.p[i] == 'u')
					allq = 0;
				if (key)
					s_ch(key, text.p[i]);
			}
		}
		if (i >= n)
			break;
		i++;
		if (key)
			t->quote[t->subs] = !literal ? 'd' : allq ? 'q' : 'u';
		t->subs++;
		if (bare)
			break;
	}
	if (i < n && kind.p[i] == 'u' && text.p[i] == '+')
		t->append = 1;
	s_free(&text);
	s_free(&kind);
	return bare ? t->subs > 0 : t->name.n > 0;
}

/* Release a parsed target. */
static void iv_tgtfree(iv_tgt *t)
{
	s_free(&t->name);
	s_free(&t->key[0]);
	s_free(&t->key[1]);
}

/* Find where a target's subscripts end in its own recorded bytes. */
static int iv_brackets(ix_ctx *c, size_t at, size_t end, unsigned subs, size_t *ks, size_t *ke,
		       size_t *stop)
{
	unsigned k;
	size_t i = at;
	int depth;
	char q;

	for (k = 0; k < subs; k++) {
		if (i >= end || c->src[i] != '[')
			return 0;
		if (k < 2)
			ks[k] = i + 1;
		for (depth = 1, i++; i < end && depth;) {
			if (c->src[i] == '\\') {
				i += 2;
				continue;
			}
			if (c->src[i] == '\'' || c->src[i] == '"') {
				for (q = c->src[i++]; i < end && c->src[i] != q; i++)
					if (q == '"' && c->src[i] == '\\')
						i++;
				i++;
				continue;
			}
			if (c->src[i] == '[')
				depth++;
			else if (c->src[i] == ']')
				depth--;
			i++;
		}
		if (depth || i > end)
			return 0;
		if (k < 2)
			ke[k] = i - 1;
	}
	*stop = i;
	return 1;
}

/* Show a dynamic subscript's own source unless it holds command code. */
static char *iv_dyntext(ix_ctx *c, size_t start, size_t end, str *out)
{
	size_t i;

	for (i = start; i < end; i++)
		if (c->src[i] == '`' || ((c->src[i] == '$' || c->src[i] == '<' || c->src[i] == '>') &&
					 i + 1 < end && c->src[i + 1] == '('))
			return 0;
	s_add(out, c->src + start, end - start);
	return out->p ? out->p : "";
}

/* Record a word naming a variable: name, subscripts and += read by the parser. */
static iv_rec *iv_target(ix_ctx *c, word *w, const char *access, const char *form, int quoted,
			 int bare, const char *array)
{
	iv_tgt t;
	iv_rec *r;
	size_t ws, we, ns = (size_t)-1, stop = 0;
	const char *name;
	unsigned k;
	int located, exact = 0;
	str dyn;

	if (!w)
		return 0;
	if (!iv_parse(w, quoted, bare, &t)) {
		if (w->p && !w->p->nx && w->p->k == P_VAR)
			iv_byname(c, w->p);
		iv_tgtfree(&t);
		return 0;
	}
	name = bare ? array : t.name.p;
	located = ix_position(c, w, &ws, &we);
	if (located && bare)
		ns = ws;
	else if (located && iv_at(c, ws, name, t.name.n) && !iv_namech(c, ws + t.name.n))
		ns = ws;
	else if (located && ws < c->end && (c->src[ws] == '"' || c->src[ws] == '\'') &&
		 iv_at(c, ws + 1, name, t.name.n))
		ns = ws + 1;
	if (ns != (size_t)-1)
		exact = iv_brackets(c, ns + (bare ? 0 : t.name.n), we, t.subs, t.ks, t.ke, &stop);
	if (exact)
		r = iv_add(c, name, access, t.append && !strcmp(form, "assign") ? "append" : form, ns,
			   stop, 1);
	else if (located)
		r = iv_add(c, name, access, t.append && !strcmp(form, "assign") ? "append" : form, ws,
			   we, 0);
	else
		r = iv_add(c, name, access, t.append && !strcmp(form, "assign") ? "append" : form,
			   (size_t)-1, 0, 0);
	if (r) {
		r->subs = t.subs;
		for (k = 0; k < 2 && k < t.subs; k++) {
			s_init(&dyn);
			if (t.quote[k] != 'd')
				iv_key(r, k, t.key[k].p ? t.key[k].p : "", t.quote[k]);
			else
				iv_key(r, k, exact ? iv_dyntext(c, t.ks[k], t.ke[k], &dyn) : 0, 'd');
			s_free(&dyn);
		}
	}
	iv_tgtfree(&t);
	return r;
}

/* Record a name written inside a literal word, such as read -aNAME. */
static iv_rec *iv_inline(ix_ctx *c, word *w, size_t offset, const char *name, const char *access,
			 const char *form)
{
	size_t ws, we;

	if (!isname(name))
		return 0;
	if (ix_position(c, w, &ws, &we) && iv_at(c, ws + offset, name, strlen(name)))
		return iv_add(c, name, access, form, ws + offset, ws + offset + strlen(name), 1);
	return iv_add(c, name, access, form, (size_t)-1, 0, 0);
}

static void iv_word(ix_ctx *, word *);
static void iv_words(ix_ctx *, word *);

/* Lex arithmetic or default text as words, keeping coordinates when they map. */
static void iv_relex(ix_ctx *c, const char *text, size_t at, int exact)
{
	sh own;
	lex l;
	const char *input = c->input;
	size_t origin = c->origin, inputsize = c->inputsize;
	int lv;

	memset(&own, 0, sizeof own);
	own.ar = ar_new(4096);
	own.ln = (unsigned)(ix_line(c, exact ? at : c->cstart) + 1);
	lx_init(&l, &own, text);
	if (exact) {
		c->input = text;
		c->origin = at;
		c->inputsize = strlen(text);
		l.observe = ix_observe;
		l.observer = c;
	}
	lv = hibr_lv;
	hibr_lv = -1;
	for (lx_next(&l); l.tk != T_EOF && !l.err && !l.more; lx_next(&l))
		if (l.tk == T_WORD)
			iv_word(c, l.w);
	if (l.err || l.more)
		c->partial = 1;
	hibr_lv = lv;
	c->input = input;
	c->origin = origin;
	c->inputsize = inputsize;
	v_free(&l.hq);
	ar_free(own.ar);
}

/* Whether arithmetic text after a name assigns to it. */
static int iv_assigns(const char *z)
{
	while (*z == ' ' || *z == '\t')
		z++;
	return (z[0] == '=' && z[1] != '=') || ((z[0] == '+' || z[0] == '-') && z[1] == z[0]) ||
	       (z[0] && strchr("+-*/%&^|", z[0]) && z[1] == '=') ||
	       ((z[0] == '<' || z[0] == '>') && z[1] == z[0] && z[2] == '=');
}

/* Give an arithmetic fact its first two subscripts as written. */
static void iv_arithkeys(iv_rec *r, const char *z)
{
	const char *kb, *ke;
	str key;
	unsigned k;
	int depth;

	for (k = 0; *z == '['; k++) {
		kb = ++z;
		for (depth = 1; *z && depth; z++)
			depth += *z == '[' ? 1 : *z == ']' ? -1 : 0;
		ke = depth ? z : z - 1;
		if (k > 1)
			continue;
		while (kb < ke && (*kb == ' ' || *kb == '\t'))
			kb++;
		while (ke > kb && (ke[-1] == ' ' || ke[-1] == '\t'))
			ke--;
		s_init(&key);
		if (memchr(kb, '$', (size_t)(ke - kb))) {
			s_add(&key, kb, (size_t)(ke - kb));
			iv_key(r, k, strstr(key.p, "$(") || strchr(key.p, '`') ? 0 : key.p, 'd');
		} else if (ke - kb >= 2 && (*kb == '"' || *kb == '\'') && ke[-1] == *kb) {
			s_add(&key, kb + 1, (size_t)(ke - kb - 2));
			iv_key(r, k, key.p ? key.p : "", 'q');
		} else {
			s_add(&key, kb, (size_t)(ke - kb));
			iv_key(r, k, key.p ? key.p : "", 'u');
		}
		s_free(&key);
	}
	r->subs = k;
}

/* Find names in arithmetic text by a lexical scan: it has no parse tree until it runs. */
static void iv_arith(ix_ctx *c, const char *text, size_t at, int exact)
{
	const char *q, *b;
	str name;
	iv_rec *r;
	int write, depth;
	char open, close, quote;

	if (!text)
		return;
	if (strchr(text, '$'))
		iv_relex(c, text, at, exact);
	for (q = text; *q;) {
		if (*q == '$') {
			q++;
			if (*q == '{' || *q == '(') {
				open = *q;
				close = open == '{' ? '}' : ')';
				for (depth = 1, q++; *q && depth; q++)
					depth += *q == open ? 1 : *q == close ? -1 : 0;
			} else
				while (isalnum((unsigned char)*q) || *q == '_')
					q++;
			continue;
		}
		if (*q == '\'' || *q == '"') {
			for (quote = *q++; *q && *q != quote; q++) {
			}
			if (*q)
				q++;
			continue;
		}
		if (isdigit((unsigned char)*q)) {
			while (isalnum((unsigned char)*q) || *q == '_' || *q == '#' || *q == '@')
				q++;
			continue;
		}
		if (!isalpha((unsigned char)*q) && *q != '_') {
			q++;
			continue;
		}
		b = q;
		while (isalnum((unsigned char)*q) || *q == '_')
			q++;
		s_init(&name);
		s_add(&name, b, (size_t)(q - b));
		write = b >= text + 2 && b[-1] == b[-2] && (b[-1] == '+' || b[-1] == '-');
		while (*q == '[')
			for (depth = 1, q++; *q && depth; q++)
				depth += *q == '[' ? 1 : *q == ']' ? -1 : 0;
		write = write || iv_assigns(q);
		r = iv_add(c, name.p, write ? "write" : "read", "arithmetic",
			   exact ? at + (size_t)(b - text) : (size_t)-1,
			   exact ? at + (size_t)(q - text) : 0, exact);
		if (r) {
			r->lexical = 1;
			r->known = "inferred";
			r->rule = "arithmetic-text";
			iv_arithkeys(r, b + name.n);
		}
		s_free(&name);
	}
}

/* Treat a word's own recorded bytes as arithmetic text. */
static void iv_arithword(ix_ctx *c, word *w, int unquote)
{
	size_t ws, we;
	str text;
	const char *literal = ix_literal(w);

	if (ix_position(c, w, &ws, &we) && we > ws) {
		if (unquote && we - ws >= 2 && (c->src[ws] == '"' || c->src[ws] == '\'') &&
		    c->src[we - 1] == c->src[ws]) {
			ws++;
			we--;
		}
		s_init(&text);
		s_add(&text, c->src + ws, we - ws);
		iv_arith(c, text.p ? text.p : "", ws, 1);
		s_free(&text);
	} else if (literal)
		iv_arith(c, literal, (size_t)-1, 0);
	else
		iv_word(c, w);
}

/* Record a parameter expansion and walk its subscripts and operands. */
static void iv_var(ix_ctx *c, part *p)
{
	int op = p->op & ~V_INDF, indirect = (p->op & V_INDF) || op == V_IND;
	size_t start, end, at, ws, we;
	const char *form;
	iv_rec *r = 0, *d = 0;
	word *w;
	unsigned k;
	str text;
	int located;

	if (indirect)
		iv_byname(c, p);
	if (p->t && isname(p->t)) {
		form = op == V_LEN ? "length" : op == V_KEYS ? "keys" : op == V_NAMES ? "names"
		       : indirect ? "indirect" : "expand";
		located = ix_position(c, p, &start, &end) && iv_at(c, start, "$", 1);
		at = start + 1;
		if (located && iv_at(c, at, "{", 1)) {
			at++;
			if (iv_at(c, at, "#", 1) || iv_at(c, at, "!", 1))
				at++;
		}
		located = located && iv_at(c, at, p->t, p->n);
		r = iv_add(c, p->t, "read", form, located ? start : (size_t)-1, end, located);
		if (op == V_ASG)
			d = iv_add(c, p->t, "write", "default-assign", located ? start : (size_t)-1,
				   end, located);
		for (k = 0, w = p->idx; w; w = w->nx, k++) {
			if (k > 1)
				continue;
			s_init(&text);
			if (w->p && !ix_namecode(w->p) && ix_literal(w)) {
				iv_key(r, k, ix_literal(w), w->p->q ? 'q' : 'u');
				iv_key(d, k, ix_literal(w), w->p->q ? 'q' : 'u');
			} else if (!w->p) {
				iv_key(r, k, "", 'u');
				iv_key(d, k, "", 'u');
			} else {
				if (!ix_namecode(w->p) && ix_position(c, w, &ws, &we))
					s_add(&text, c->src + ws, we - ws);
				iv_key(r, k, text.n ? text.p : 0, 'd');
				iv_key(d, k, text.n ? text.p : 0, 'd');
			}
			s_free(&text);
		}
		if (r)
			r->subs = k;
		if (d)
			d->subs = k;
	}
	iv_words(c, p->idx);
	for (w = p->arg; w; w = w->nx)
		if (op == V_SUBSTR)
			iv_arithword(c, w, 0);
		else
			iv_word(c, w);
}

/* Walk one part of a word for the variables it reads. */
static void iv_part(ix_ctx *c, part *p)
{
	size_t start, end, inner, search = 0;
	int exact;

	switch (p->k) {
	case P_VAR:
		iv_var(c, p);
		return;
	case P_CMD:
	case P_PSUB:
		if (p->t && *p->t)
			ix_subst(c, p, &search);
		break;
	case P_ARI:
		exact = ix_position(c, p, &start, &end) && iv_at(c, start, "$", 1);
		inner = exact ? start + (iv_at(c, start + 1, "((", 2) ? 3 : 2) : (size_t)-1;
		exact = exact && iv_at(c, inner, p->t, p->n);
		iv_arith(c, p->t, exact ? inner : (size_t)-1, exact);
		break;
	}
	iv_words(c, p->arg);
	iv_words(c, p->idx);
}

/* Walk a word, remembering its span for facts with no span of their own. */
static void iv_word(ix_ctx *c, word *w)
{
	size_t ws = c->wstart, we = c->wend, start, end;
	int known = c->wknown;
	part *p;

	if (!w)
		return;
	if (ix_position(c, w, &start, &end)) {
		c->wknown = 1;
		c->wstart = start;
		c->wend = end;
	}
	for (p = w->p; p; p = p->nx)
		iv_part(c, p);
	c->wknown = known;
	c->wstart = ws;
	c->wend = we;
}

/* Walk a list of words. */
static void iv_words(ix_ctx *c, word *w)
{
	for (; w; w = w->nx)
		iv_word(c, w);
}

/* Whether a word was already taken as a variable's name in this command. */
static int iv_used(vec *v, word *w)
{
	size_t i;

	for (i = 0; i < v->n; i++)
		if (v->p[i] == w)
			return 1;
	return 0;
}

/* Record a word taken as a target, so it is not reported again as an argument. */
static iv_rec *iv_take(ix_ctx *c, word *w, const char *access, const char *form, int quoted)
{
	if (!w)
		return 0;
	v_add(&c->vused, w);
	return iv_target(c, w, access, form, quoted, 0, 0);
}

/* The argument at a position, the command word being 0. */
static word *iv_arg(node *n, unsigned k)
{
	word *w = n->w;

	while (w && k--)
		w = w->nx;
	return w;
}

/* A command's name as a form that outlives the parse it came from. */
static const char *iv_formof(const char *cmd)
{
	return !strcmp(cmd, "local")	  ? "local"
	       : !strcmp(cmd, "declare")  ? "declare"
	       : !strcmp(cmd, "typeset")  ? "typeset"
	       : !strcmp(cmd, "export")	  ? "export"
	       : !strcmp(cmd, "readonly") ? "readonly"
	       : !strcmp(cmd, "match")	  ? "match"
					  : "rsub";
}

/* Record a declaration's nameref target, or note a parameter used by name. */
static void iv_nameref(ix_ctx *c, word *w, iv_rec *ref)
{
	part *p, *dynamic = 0;
	str target;
	const char *eq;
	int seen = 0, other = 0;
	size_t at;

	s_init(&target);
	for (p = w->p; p; p = p->nx) {
		if (!seen) {
			if (p->k == P_TXT && (eq = memchr(p->t, '=', p->n))) {
				seen = 1;
				s_add(&target, eq + 1, p->n - (size_t)(eq + 1 - p->t));
			}
			continue;
		}
		if (p->k == P_TXT)
			s_add(&target, p->t, p->n);
		else if (p->k == P_VAR && !dynamic)
			dynamic = p;
		else
			other = 1;
	}
	if (dynamic && !other && !target.n)
		iv_byname(c, dynamic);
	else if (!dynamic && !other && target.p && isname(target.p)) {
		at = ref && ref->exact && iv_at(c, ref->end, "=", 1) ? ref->end + 1 : (size_t)-1;
		if (at != (size_t)-1 && at < c->end && (c->src[at] == '"' || c->src[at] == '\''))
			at++;
		if (!iv_at(c, at, target.p, target.n))
			at = (size_t)-1;
		iv_add(c, target.p, "name", "nameref", at, at + target.n, at != (size_t)-1);
	}
	s_free(&target);
}

/* Record what a declaration builtin declares, and whether it is local here. */
static void iv_decl(ix_ctx *c, node *n, const char *cmd)
{
	word *w;
	node *cl;
	const char *a;
	iv_rec *r;
	size_t ws, we;
	int global = 0, nameref = 0, functions = 0, print = 0, local, attr, clause, typed;

	attr = strcmp(cmd, "export") && strcmp(cmd, "readonly");
	a = n->w->nx ? ix_literal(n->w->nx) : 0;
	typed = attr && (strcmp(cmd, "local") || (a && a[0] == '-' && a[1] && strcmp(a, "--")));
	for (w = n->w->nx; w; w = w->nx) {
		a = ix_literal(w);
		v_add(&c->vused, w);
		if (a && (a[0] == '-' || a[0] == '+') && a[1] && !strchr(a, '=')) {
			global |= strchr(a, 'g') != 0;
			nameref |= attr && a[0] == '-' && strchr(a, 'n');
			functions |= strchr(a, 'f') || strchr(a, 'F');
			print |= strchr(a, 'p') != 0;
			continue;
		}
		if (typed && a && w->nx && v_tycode(a))
			continue;
		if (functions)
			continue;
		for (clause = 0, cl = n->x; a && cl && !clause; cl = cl->x)
			clause = cl->k == N_CLAUSE && cl->s && !strncmp(cl->s, a, strlen(a)) &&
				 (!cl->s[strlen(a)] || !strcmp(cl->s + strlen(a), "+")) &&
				 !ix_position(c, w, &ws, &we);
		if (clause)
			continue;
		local = c->vfn && (!strcmp(cmd, "local") || (attr && !global));
		r = iv_target(c, w, print ? "read" : "write", nameref ? "nameref" : iv_formof(cmd), 0, 0,
			       0);
		if (r && local && !print)
			v_add(&iv_frame(c)->locals, r->name);
		if (nameref)
			iv_nameref(c, w, r);
	}
}

/* Whether a word is one of a space-separated list. */
static int iv_oneof(const char *list, const char *word)
{
	size_t n = strlen(word);
	const char *at;

	for (at = list; n && (at = strstr(at, word)); at++)
		if ((at == list || at[-1] == ' ') && (!at[n] || at[n] == ' '))
			return 1;
	return 0;
}

/* Record names given to builtins that read or write a variable by its name. */
static void iv_named(ix_ctx *c, node *n, const char *cmd)
{
	word *w = n->w->nx, *x;
	const char *a, *verb = w ? ix_literal(w) : 0;
	iv_rec *r;
	unsigned i, need, out = 0;
	int skip = 0, done = 0;

	if (!strcmp(cmd, "unset")) {
		for (; w; w = w->nx) {
			a = ix_literal(w);
			if (!done && a && a[0] == '-' && a[1]) {
				skip |= strchr(a, 'f') != 0;
				done = !strcmp(a, "--");
				continue;
			}
			done = 1;
			if (!skip)
				iv_take(c, w, "write", "unset", 1);
		}
	} else if (!strcmp(cmd, "read")) {
		for (; w; w = w->nx) {
			a = ix_literal(w);
			if (!done && a && a[0] == '-' && a[1]) {
				if (!strcmp(a, "--")) {
					done = 1;
					continue;
				}
				for (i = 1; a[i]; i++) {
					if (a[i] == 'a') {
						v_add(&c->vused, w);
						if (a[i + 1])
							iv_inline(c, w, i + 1, a + i + 1, "write", "read-array");
						else if (w->nx) {
							w = w->nx;
							r = iv_take(c, w, "write", "read-array", 1);
						}
						break;
					}
					if (strchr("dinNptu", a[i])) {
						if (!a[i + 1] && w->nx)
							w = w->nx;
						break;
					}
				}
				continue;
			}
			done = 1;
			iv_take(c, w, "write", "read", 1);
		}
	} else if (!strcmp(cmd, "mapfile") || !strcmp(cmd, "readarray")) {
		for (; w; w = w->nx) {
			a = ix_literal(w);
			if (!done && a && a[0] == '-' && a[1]) {
				if (a[2] == 0 && strchr("dnOsuCc", a[1]) && w->nx)
					w = w->nx;
				done = !strcmp(a, "--");
				continue;
			}
			done = 1;
			iv_take(c, w, "write", "mapfile", 1);
		}
	} else if (!strcmp(cmd, "printf")) {
		if (verb && !strcmp(verb, "-v"))
			iv_take(c, w->nx, "write", "printf", 1);
		else if (verb && !strncmp(verb, "-v", 2) && verb[2])
			iv_inline(c, w, 2, verb + 2, "write", "printf");
	} else if (!strcmp(cmd, "getopts")) {
		iv_take(c, iv_arg(n, 2), "write", "getopts", 1);
	} else if (!strcmp(cmd, "let")) {
		for (; w; w = w->nx) {
			v_add(&c->vskip, w);
			iv_arithword(c, w, 1);
		}
	} else if (!strcmp(cmd, "arr") && verb) {
		x = iv_arg(n, 2);
		r = iv_oneof("len contains push pop sort uniq reverse map filter", verb)
			? iv_take(c, x, "read", "arr", 1)
			: 0;
		if (r)
			r->verb = xs(verb);
		if (r && iv_oneof("push pop sort uniq reverse map filter", verb) &&
		    (r = iv_target(c, x, "write", "arr", 1, 0, 0)))
			r->verb = xs(verb);
		if (r && (!strcmp(verb, "len") || !strcmp(verb, "pop")) &&
		    (r = iv_take(c, iv_arg(n, 3), "write", "arr", 1)))
			r->verb = xs(verb);
	} else if (!strcmp(cmd, "json") && verb) {
		x = iv_arg(n, 2);
		r = iv_oneof("set emit get keys len type", verb) ? iv_take(c, x, "read", "json", 1)
								 : 0;
		if (r)
			r->verb = xs(verb);
		if (iv_oneof("parse set", verb) && (r = iv_take(c, x, "write", "json", 1)))
			r->verb = xs(verb);
		x = iv_arg(n, 4);
		a = x ? ix_literal(x) : 0;
		if (r && (!strcmp(verb, "get") || !strcmp(verb, "keys")) && x && (!a || a[0] != '-') &&
		    (r = iv_take(c, x, "write", "json", 1)))
			r->verb = xs(verb);
	} else if (!strcmp(cmd, "str") && verb) {
		if (iv_oneof("len width upper lower trim", verb))
			out = 3;
		else if (iv_oneof("index split join repeat", verb))
			out = 4;
		else if (iv_oneof("slice replace pad", verb))
			out = 5;
		if (!strcmp(verb, "join") && (r = iv_take(c, iv_arg(n, 2), "read", "str", 1)))
			r->verb = xs(verb);
		if (out && (r = iv_take(c, iv_arg(n, out), "write", "str", 1)))
			r->verb = xs(verb);
	} else if (!strcmp(cmd, "match") || !strcmp(cmd, "rsub")) {
		need = !strcmp(cmd, "match") ? 2 : 3;
		for (i = 0; w; w = w->nx) {
			a = ix_literal(w);
			if (!done && a && a[0] == '-' && a[1]) {
				done = !strcmp(a, "--");
				continue;
			}
			done = 1;
			if (i++ == need)
				iv_take(c, w, "write", iv_formof(cmd), 1);
		}
	} else if (!strcmp(cmd, "test") || !strcmp(cmd, "[")) {
		for (; w; w = w->nx)
			if ((a = ix_literal(w)) && !strcmp(a, "-v") && w->nx)
				iv_take(c, w->nx, "read", "test", 1);
	}
}

/* Find a function of this file whose arguments are taken by name. */
static iv_fn *iv_callee(ix_ctx *c, const char *name)
{
	size_t i;
	iv_fn *f;

	for (i = 1; name && i < c->vfns.n; i++) {
		f = c->vfns.p[i];
		if (f->byname.n && !strcmp(f->name, name))
			return f;
	}
	return 0;
}

/* Record literal arguments that may name variables: to this file's functions, or supplied. */
static void iv_names(ix_ctx *c, node *n, const char *cmd)
{
	word *w;
	const char *a;
	iv_rec *r;
	size_t k, ws, we;
	int local = ix_named(c, cmd);

	for (w = n->w->nx, k = 1; w; w = w->nx, k++) {
		a = ix_literal(w);
		if (!a || !isname(a) || iv_used(&c->vused, w) || (!local && !iv_has(&c->symbols, a)))
			continue;
		if (ix_position(c, w, &ws, &we))
			r = iv_add(c, a, "name", "argument", ws, we, 1);
		else
			r = iv_add(c, a, "name", "argument", (size_t)-1, 0, 0);
		if (!r)
			continue;
		r->callee = cmd ? xs(cmd) : 0;
		r->arg = k;
	}
}

/* Settle an argument once every function's by-name parameters are known. */
static int iv_settle(ix_ctx *c, iv_rec *r)
{
	iv_fn *f = iv_callee(c, r->callee);
	size_t i;

	for (i = 0; f && i < f->byname.n; i++)
		if ((size_t)(uintptr_t)f->byname.p[i] == r->arg) {
			r->known = "inferred";
			r->rule = "by-name-parameter";
			return 1;
		}
	r->lookup = "supplied-symbol";
	return iv_has(&c->symbols, r->name);
}

/* Where a := target's name stands, after blanks from a recorded offset, if it does. */
static size_t iv_bindat(ix_ctx *c, size_t at, const char *name)
{
	while (at < c->end && (c->src[at] == ' ' || c->src[at] == '\t'))
		at++;
	if (!iv_at(c, at, name, strlen(name)) || iv_namech(c, at + strlen(name)))
		return (size_t)-1;
	return at;
}

/* Whether a declaration's flags include -g. */
static int iv_global(node *n)
{
	word *w;
	const char *a;

	for (w = n->w ? n->w->nx : 0; w; w = w->nx)
		if ((a = ix_literal(w)) && a[0] == '-' && !strchr(a, '=') && strchr(a, 'g'))
			return 1;
	return 0;
}

/* Record a simple command's assignments, bindings, arrays and named variables. */
static void iv_cmd(ix_ctx *c, node *n)
{
	word *w;
	node *cl;
	const char *cmd = ix_literal(n->w), *form;
	iv_rec *r;
	str name;
	size_t at, start, end, used = c->vused.n, skipped = c->vskip.n;
	int decl = cmd && (!strcmp(cmd, "local") || !strcmp(cmd, "declare") ||
			   !strcmp(cmd, "typeset") || !strcmp(cmd, "export") ||
			   !strcmp(cmd, "readonly"));

	for (w = n->aw; w; w = w->nx) {
		iv_target(c, w, "write", n->w ? "prefix" : "assign", 0, 0, 0);
		iv_word(c, w);
	}
	if (n->f == 2 && n->s) {
		at = iv_bindat(c, c->cstart, n->s);
		for (w = n->aw; w && at == (size_t)-1; w = w->nx)
			if (ix_position(c, w, &start, &end))
				at = iv_bindat(c, end, n->s);
		iv_add(c, n->s, "write", "bind", at, at + strlen(n->s), at != (size_t)-1);
	} else if (n->f == 2 && n->bw) {
		iv_target(c, n->bw, "write", "bind", 0, 0, 0);
		iv_word(c, n->bw);
	}
	for (cl = n->x; cl; cl = cl->x) {
		if (cl->k != N_CLAUSE || !cl->s)
			continue;
		s_init(&name);
		s_cat(&name, cl->s);
		if (name.n && name.p[name.n - 1] == '+')
			name.p[--name.n] = 0;
		form = decl ? iv_formof(cmd) : cl->s[strlen(cl->s) - 1] == '+' ? "array-append" : "array";
		if (ix_position(c, cl, &start, &end) && iv_at(c, start, name.p, name.n))
			r = iv_add(c, name.p, "write", form, start, start + name.n, 1);
		else
			r = iv_add(c, name.p, "write", form, (size_t)-1, 0, 0);
		if (r && decl && c->vfn &&
		    (!strcmp(cmd, "local") ||
		     ((!strcmp(cmd, "declare") || !strcmp(cmd, "typeset")) && !iv_global(n))))
			v_add(&iv_frame(c)->locals, r->name);
		for (w = cl->w; w; w = w->nx) {
			if (w->p && w->p->k == P_TXT && !w->p->q && w->p->n && w->p->t[0] == '[' &&
			    (r = iv_target(c, w, "write", "array-key", 0, 1, name.p)))
				r->subs = 1;
			iv_word(c, w);
		}
		s_free(&name);
	}
	if (decl)
		iv_decl(c, n, cmd);
	else if (cmd)
		iv_named(c, n, cmd);
	if (n->w)
		iv_names(c, n, cmd);
	for (w = n->w; w; w = w->nx)
		if (!iv_used(&c->vskip, w))
			iv_word(c, w);
	c->vused.n = used;
	c->vskip.n = skipped;
}

/* Skip a keyword and the blanks after it at a node's recorded start. */
static size_t iv_after(ix_ctx *c, size_t at, const char *keyword)
{
	if (!iv_at(c, at, keyword, strlen(keyword)))
		return (size_t)-1;
	at += strlen(keyword);
	while (at < c->end && (c->src[at] == ' ' || c->src[at] == '\t'))
		at++;
	return at;
}

/* Record a function's parameters and walk its body in a frame of its own. */
static void iv_func(ix_ctx *c, node *n, size_t start)
{
	iv_fn *f;
	node *pm;
	const char *owner = c->owner;
	size_t was = c->vfn, from, to, defat;
	str text;
	int index;

	f = xm(sizeof *f);
	memset(f, 0, sizeof *f);
	f->name = xs(n->s ? n->s : "");
	for (pm = n->x; pm; pm = pm->x)
		if (pm->s && !(pm->f & TY_REST))
			v_add(&f->params, xs(pm->s));
	v_add(&c->vfns, f);
	c->vfn = c->vfns.n - 1;
	c->owner = n->s;
	for (pm = n->x, index = 0; pm; pm = pm->x, index++) {
		if (!pm->s)
			continue;
		s_init(&text);
		if (ix_paramat(n, index, start, &from, &to, &defat, &text)) {
			iv_add(c, pm->s, "write", "param", from, to, 1);
			if (defat != (size_t)-1)
				iv_relex(c, text.p ? text.p : "", defat,
					 iv_at(c, defat, text.p ? text.p : "", text.n));
		} else {
			iv_add(c, pm->s, "write", "param", (size_t)-1, 0, 0);
			iv_word(c, pm->w);
		}
		s_free(&text);
	}
	iv_node(c, n->r);
	c->vfn = was;
	c->owner = owner;
}

/* Walk parsed units for every variable each command reads, writes or names. */
static void iv_node(ix_ctx *c, node *n)
{
	size_t start, end, at, cs = c->cstart, ce = c->cend;
	word *w;
	redir *r;
	const char *a;
	int exact;

	while (n && n->k == N_SEQ) {
		iv_node(c, n->l);
		n = n->r;
	}
	if (!n)
		return;
	start = ix_start(c, n);
	end = n->tx ? start + strlen(n->tx) : start;
	ix_position(c, n, &start, &end);
	if (end > c->end)
		end = c->end;
	c->cstart = start;
	c->cend = end;
	switch (n->k) {
	case N_FUNC:
		iv_func(c, n, start);
		break;
	case N_CMD:
		iv_cmd(c, n);
		break;
	case N_ARITH:
		exact = n->w && n->w->p && iv_at(c, start, "((", 2) &&
			iv_at(c, start + 2, n->w->p->t, n->w->p->n);
		iv_arith(c, n->w && n->w->p ? n->w->p->t : 0, exact ? start + 2 : (size_t)-1, exact);
		break;
	case N_CFOR:
		at = iv_after(c, start, "for");
		at = iv_at(c, at, "((", 2) ? at + 2 : (size_t)-1;
		for (w = n->w; w; w = w->nx) {
			a = w->p ? w->p->t : "";
			exact = iv_at(c, at, a, strlen(a));
			iv_arith(c, a, exact ? at : (size_t)-1, exact);
			at = exact ? at + strlen(a) + 1 : (size_t)-1;
		}
		iv_node(c, n->r);
		break;
	case N_FOR:
	case N_SELECT:
		at = iv_after(c, start, n->k == N_FOR ? "for" : "select");
		exact = n->s && iv_at(c, at, n->s, strlen(n->s)) && !iv_namech(c, at + strlen(n->s));
		iv_add(c, n->s, "write", n->k == N_FOR ? "for" : "select", exact ? at : (size_t)-1,
		       exact ? at + strlen(n->s) : 0, exact);
		iv_words(c, n->w);
		iv_node(c, n->l);
		iv_node(c, n->r);
		iv_node(c, n->x);
		break;
	case N_COND:
		for (w = n->w; w; w = w->nx)
			if ((a = ix_literal(w)) && w->p->op != -1 && !strcmp(a, "-v") && w->nx)
				iv_target(c, w->nx, "read", "test", 1, 0, 0);
		iv_words(c, n->w);
		break;
	default:
		iv_words(c, n->w);
		iv_words(c, n->aw);
		iv_node(c, n->l);
		iv_node(c, n->r);
		iv_node(c, n->x);
		break;
	}
	c->cstart = start;
	c->cend = end;
	for (r = n->rd; r; r = r->nx) {
		if (r->var && isname(r->var)) {
			exact = ix_position(c, r->var, &at, &end) && iv_at(c, at, r->var, strlen(r->var));
			iv_add(c, r->var, "write", "fd-variable", exact ? at : (size_t)-1,
			       at + strlen(r->var), exact);
		}
		iv_word(c, r->w);
	}
	c->cstart = cs;
	c->cend = ce;
}

/* Order facts by where they start, then by when they were found. */
static int iv_order(const void *a, const void *b)
{
	const iv_rec *x = *(iv_rec *const *)a, *y = *(iv_rec *const *)b;

	if (x->start != y->start)
		return x->start < y->start ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* Say how a subscript was written. */
static const char *iv_quote(char quote)
{
	return quote == 'q' ? "quoted" : quote == 'd' ? "dynamic" : "unquoted";
}

/* Emit the collected facts as records, each with its function's declarations. */
static void iv_emit(ix_ctx *c)
{
	iv_rec *r;
	iv_fn *f;
	size_t i;
	unsigned k;

	if (c->vrecs.n)
		qsort(c->vrecs.p, c->vrecs.n, sizeof *c->vrecs.p, iv_order);
	for (i = 0; i < c->vrecs.n; i++) {
		r = c->vrecs.p[i];
		if ((c->filter && strcmp(c->filter, r->owner)) || (r->arg && !iv_settle(c, r)))
			continue;
		f = c->vfns.p[r->fn];
		c->subdepth = r->subdepth;
		ix_record(c, r->start, r->end, r->known);
		c->subdepth = 0;
		s_fmt(c->out, ",\"exact\":%s,\"name\":", r->exact ? "true" : "false");
		ix_quote(c->out, r->name);
		s_cat(c->out, ",\"access\":");
		ix_quote(c->out, r->access);
		s_cat(c->out, ",\"form\":");
		ix_quote(c->out, r->form);
		s_cat(c->out, ",\"owner\":");
		ix_quote(c->out, r->owner);
		s_cat(c->out, ",\"declared\":");
		if (r->fn && iv_has(&f->params, r->name))
			s_cat(c->out, "\"param\"");
		else if (r->fn && iv_has(&f->locals, r->name))
			s_cat(c->out, "\"local\"");
		else
			s_cat(c->out, "null");
		s_fmt(c->out, ",\"lexical\":%s,\"subscripts\":%u,\"keys\":[",
		      r->lexical ? "true" : "false", r->subs);
		for (k = 0; k < 2 && r->quote[k]; k++) {
			s_cat(c->out, k ? ",{\"text\":" : "{\"text\":");
			if (r->key[k])
				ix_quote(c->out, r->key[k]);
			else
				s_cat(c->out, "null");
			s_cat(c->out, ",\"quote\":");
			ix_quote(c->out, iv_quote(r->quote[k]));
			s_ch(c->out, '}');
		}
		s_ch(c->out, ']');
		if (r->verb) {
			s_cat(c->out, ",\"verb\":");
			ix_quote(c->out, r->verb);
		}
		if (r->callee) {
			s_cat(c->out, ",\"callee\":");
			ix_quote(c->out, r->callee);
		}
		if (r->rule) {
			s_cat(c->out, ",\"rule\":");
			ix_quote(c->out, r->rule);
		}
		if (r->lookup) {
			s_cat(c->out, ",\"lookup\":");
			ix_quote(c->out, r->lookup);
		}
		s_ch(c->out, '}');
	}
}

/* Release the collected facts and function frames. */
static void iv_free(ix_ctx *c)
{
	size_t i, j;
	iv_rec *r;
	iv_fn *f;

	for (i = 0; i < c->vrecs.n; i++) {
		r = c->vrecs.p[i];
		free(r->name);
		free(r->owner);
		free(r->key[0]);
		free(r->key[1]);
		free(r->verb);
		free(r->callee);
		free(r);
	}
	for (i = 0; i < c->vfns.n; i++) {
		f = c->vfns.p[i];
		for (j = 0; j < f->params.n; j++)
			free(f->params.p[j]);
		free(f->name);
		v_free(&f->params);
		v_free(&f->locals);
		v_free(&f->byname);
		free(f);
	}
	v_free(&c->vrecs);
	v_free(&c->vfns);
	v_free(&c->vused);
	v_free(&c->vskip);
}

/* Walk every unit for its facts, then settle arguments and emit them in order. */
static void iv_run(ix_ctx *c, vec *units)
{
	iv_fn *top = xm(sizeof *top);
	ix_unit *unit;
	size_t i;

	memset(top, 0, sizeof *top);
	top->name = xs("");
	v_add(&c->vfns, top);
	c->vfn = 0;
	c->owner = 0;
	for (i = 0; i < units->n; i++) {
		unit = units->p[i];
		c->unitstart = unit->start;
		c->unitend = unit->end;
		c->cstart = unit->start;
		c->cend = unit->end;
		iv_node(c, unit->tree);
	}
	iv_emit(c);
	iv_free(c);
}

/* Refuse invalid UTF-8 rather than emitting malformed JSON text. */
static int ix_utf8(const str *src)
{
	size_t i = 0;
	unsigned cp;
	int n;

	while (i < src->n) {
		if ((unsigned char)src->p[i] < 128) {
			i++;
			continue;
		}
		n = u8dec(src->p + i, src->n - i, &cp);
		if (n == 1 || n > 4 || cp > 0x10ffff ||
		    (cp >= 0xd800 && cp <= 0xdfff) || (n == 2 && cp < 0x80) ||
		    (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000))
			return 0;
		i += (size_t)n;
	}
	return 1;
}

/* Read a bounded regular file or an already registered local scheme. */
static int ix_read_impl(sh *s, const char *path, str *src)
{
	int fd;
	char *buf;
	ssize_t n;
	struct stat st;

	if (net_schnm(s, path) && !net_sch(s, path))
		return 2;
	fd = net_sch(s, path) ? net_open(s, path) : open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return 1;
	if (fstat(fd, &st) || (!S_ISREG(st.st_mode) && !net_sch(s, path))) {
		close(fd);
		return 3;
	}
	buf = xm(HIBR_IOCH);
	while ((n = read(fd, buf, HIBR_IOCH)) > 0) {
		if (src->n + (size_t)n > 4194304) {
			free(buf);
			close(fd);
			return 4;
		}
		s_add(src, buf, (size_t)n);
	}
	free(buf);
	close(fd);
	if (n < 0)
		return 5;
	if (src->p && strlen(src->p) != src->n)
		return 6;
	return ix_utf8(src) ? 0 : 7;
}

/* Read silently, including diagnostics produced by a registered local scheme. */
static int ix_read(sh *s, const char *path, str *src)
{
	int lv = hibr_lv, status;

	hibr_lv = -1;
	status = ix_read_impl(s, path, src);
	hibr_lv = lv;
	return status;
}

/* Explain a refused source without describing unread bytes as inspected facts. */
static const char *ix_read_error(int status)
{
	switch (status) {
	case 1:
		return "source could not be opened";
	case 2:
		return "a non-local source scheme is refused";
	case 3:
		return "source is not a regular file or registered local scheme";
	case 4:
		return "source exceeds the 4 MiB size limit";
	case 5:
		return "source read failed";
	case 6:
		return "source contains a NUL byte";
	case 7:
		return "source is not valid UTF-8";
	default:
		return "invalid inspection options or convention data";
	}
}

/* Convert a shared lint finding to an evidence record. */
static void ix_finding(void *data, unsigned line, const char *rule, const char *message)
{
	ix_ctx *c = data;
	size_t start = line && line <= c->nlines ? c->lines[line - 1] : 0;
	size_t end = line < c->nlines ? c->lines[line] : strlen(c->src);

	ix_record(c, start, end, "inferred");
	s_cat(c->out, ",\"rule\":");
	ix_quote(c->out, rule);
	s_cat(c->out, ",\"message\":");
	ix_quote(c->out, message);
	s_cat(
	    c->out,
	    ",\"reference\":\"CLAUDE.md#traps-already-found--dont-reintroduce-them\"}");
}

/* Parse JSON settings into an isolated variable table. */
static int ix_json(sh *s, const char *name, const char *text)
{
	char **args = xm(4 * sizeof *args);
	int status, lv = hibr_lv;

	args[0] = "json";
	args[1] = "parse";
	args[2] = (char *)name;
	args[3] = (char *)text;
	hibr_lv = -1;
	status = b_json(s, 4, args);
	hibr_lv = lv;
	free(args);
	return status;
}

/* Release only the isolated settings table, without global shell cleanup. */
static void ix_settingsfree(sh *s)
{
	size_t i;
	var *v, *next;

	for (i = 0; i < s->tsz; i++)
		for (v = s->tab[i]; v; v = next) {
			next = v->nx;
			v_free_el(v);
			free(v->k);
			free(v->v);
			free(v);
		}
	free(s->tab);
}

/* Validate and collect a bounded caller-supplied reference whitelist. */
static const char *ix_symbols(sh *settings, ix_ctx *c)
{
	char **keys = xm(sizeof *keys);
	ent *list, *e;
	var *options = v_find(settings, "OPTIONS");
	size_t count = 0;

	if (!options || options->ty != J_OBJ) {
		free(keys);
		return "inspection options must be an object";
	}
	keys[0] = "symbols";
	list = v_path(settings, "OPTIONS", keys, 1, 0);
	free(keys);
	if (!list)
		return 0;
	if (list->ty != J_ARR)
		return "symbols must be an array of identifier strings";
	for (e = list->map; e; e = e->nx) {
		if (++count > 4096)
			return "symbols exceeds 4096 names";
		if (e->ty != J_STR || !e->s)
			return "symbols must be an array of identifier strings";
		if (strlen(e->s) > 128)
			return "symbol exceeds 128 bytes";
		if (!isname(e->s))
			return "symbols must be an array of identifier strings";
		v_add(&c->symbols, e->s);
	}
	return 0;
}

/* Produce preliminary records in private parser storage, without execution. */
static int ix_query(sh *caller, const char *request, const char *path,
		    const char *options, str *out)
{
	sh own, settings;
	lex l;
	ix_ctx c;
	str src, records, identity, diagnostics, ruletext, ruleidentity;
	const char *filter = 0, *rulespath = 0;
	vec units = {0};
	node *n;
	ix_unit *unit;
	size_t unitstart;
	int lv, bad = 0, ndiag = 0, sourcebad;
	unsigned oldln = lg_ln, oldcol = lg_col;
	size_t i;
	const char *state = "ok", *optionerror = 0;

	memset(&own, 0, sizeof own);
	memset(&c, 0, sizeof c);
	s_init(&src);
	s_init(&records);
	s_init(&identity);
	s_init(&diagnostics);
	s_init(&ruletext);
	s_init(&ruleidentity);
	memset(&settings, 0, sizeof settings);
	if (strcmp(request, "functions") && strcmp(request, "calls") &&
	    strcmp(request, "refs") && strcmp(request, "check") && strcmp(request, "vars")) {
		s_cat(out,
		      "{\"inspect\":1,\"state\":\"unsupported\",\"diagnostics\":[]}");
		return 1;
	}
	if (options && strlen(options) > 1048576)
		optionerror = "inspection options exceed 1 MiB";
	if (optionerror || (options && ix_json(&settings, "OPTIONS", options)))
		state = "unreadable";
	if (options && strcmp(state, "unreadable")) {
		char **keys = xm(sizeof *keys);
		optionerror = ix_symbols(&settings, &c);
		if (optionerror)
			state = "unreadable";
		keys[0] = "function";
		filter = hibr_getp(&settings, "OPTIONS", keys, 1);
		keys[0] = "rules";
		rulespath = hibr_getp(&settings, "OPTIONS", keys, 1);
		free(keys);
		if (rulespath && *rulespath) {
			if (ix_read(caller, rulespath, &ruletext) ||
			    ix_json(&settings, "RULES", ruletext.p))
				state = "unreadable";
			else {
				c.rules = &settings;
				c.rulespath = rulespath;
			}
		}
	}
	sourcebad = ix_read(caller, path, &src);
	if (sourcebad)
		state = "unreadable";
	ix_identity(&identity, path, &src, !sourcebad);
	if (c.rules) {
		ix_identity(&ruleidentity, rulespath, &ruletext, 1);
		c.ruleidentity = ruleidentity.p;
	}
	c.out = &records;
	c.src = src.p ? src.p : "";
	c.request = request;
	c.path = path;
	c.filter = filter && *filter ? filter : 0;
	c.caller = caller;
	c.identity = identity.p;
	c.nlines = 1;
	for (i = 0; i < src.n; i++)
		if (src.p[i] == '\n')
			c.nlines++;
	c.lines = xm(c.nlines * sizeof *c.lines);
	c.lines[0] = 0;
	for (i = 0, c.nlines = 1; i < src.n; i++)
		if (src.p[i] == '\n')
			c.lines[c.nlines++] = i + 1;
	if (!strcmp(state, "unreadable")) {
		c.out = &diagnostics;
		ix_record(&c, 0, 0, "syntax");
		s_cat(&diagnostics, ",\"rule\":\"input\",\"message\":");
		ix_quote(&diagnostics, optionerror ? optionerror : ix_read_error(sourcebad));
		s_ch(&diagnostics, '}');
		c.out = &records;
		c.count = 0;
	}
	if (strcmp(state, "unreadable")) {
		own.ar = ar_new(4096);
		c.positions = xm(8191 * sizeof *c.positions);
		memset(c.positions, 0, 8191 * sizeof *c.positions);
		c.input = c.src;
		c.inputsize = src.n;
		lx_init(&l, &own, c.src);
		l.observe = ix_observe;
		l.observer = &c;
		lv = hibr_lv;
		hibr_lv = -1;
		lx_next(&l);
		while (l.tk != T_EOF || l.err || l.more) {
			if (l.tk == T_NL) {
				lx_next(&l);
				continue;
			}
			unitstart = (size_t)((l.tkb ? l.tkb : l.p) - c.src);
			n = p_line(&l);
			if (l.err || l.more) {
				bad = 1;
				state = l.more ? "incomplete" : "invalid";
				c.out = &diagnostics;
				c.count = ndiag++;
				ix_record(&c,
					  (size_t)((l.errat ? l.errat : l.p) - c.src),
					  (size_t)(l.p - c.src), "syntax");
				s_cat(&diagnostics, ",\"rule\":\"parse\",\"message\":");
				ix_quote(&diagnostics, l.errmsg ? l.errmsg
						       : l.more ? "incomplete input"
								: "invalid syntax");
				s_ch(&diagnostics, '}');
				c.out = &records;
				c.count = 0;
				if (l.more)
					break;
				if (l.errat)
					l.p = l.errat;
				while (l.p < l.e && *l.p != '\n')
					l.p++;
				l.lnp = l.bol = c.src;
				l.ln = 1;
				l.err = 0;
				l.errmsg = l.errat = 0;
				lx_next(&l);
				continue;
			}
			unit = ar_alloc(own.ar, sizeof *unit);
			unit->tree = n;
			unit->start = unitstart;
			unit->end = (size_t)((l.tkb ? l.tkb : l.p) - c.src);
			v_add(&units, unit);
			ix_names(&c, n);
			if (l.tk == T_EOF)
				break;
			lx_next(&l);
		}
		hibr_lv = lv;
		c.start = 0;
		c.end = src.n;
		if (!strcmp(request, "vars"))
			iv_run(&c, &units);
		else
			for (i = 0; i < units.n; i++) {
				unit = units.p[i];
				c.unitstart = unit->start;
				c.unitend = unit->end;
				ix_walk(&c, unit->tree);
			}
		c.unitstart = c.unitend = 0;
		if (!strcmp(request, "check") && ix_lint) {
			node *all = 0, **tail = &all, *seq;
			for (i = 0; i < units.n; i++) {
				seq = ar_alloc(own.ar, sizeof *seq);
				unit = units.p[i];
				seq->k = N_SEQ;
				seq->l = unit->tree;
				*tail = seq;
				tail = &seq->r;
			}
			own.src = path;
			lv = hibr_lv;
			hibr_lv = -1;
			ix_lint->records(&own, all, c.src, ix_finding, &c);
			hibr_lv = lv;
		}
		for (i = 0; i < 8191; i++) {
			ix_pos *p = c.positions[i], *next;
			while (p) {
				next = p->next;
				free(p);
				p = next;
			}
		}
		free(c.positions);
		v_free(&units);
		v_free(&c.names);
		v_free(&l.hq);
		ar_free(own.ar);
		if (!bad && !src.n)
			state = "empty";
	}
	s_cat(out, "{\"inspect\":1,\"state\":");
	ix_quote(out, state);
	s_fmt(out, ",\"partial\":%s", (bad || c.partial) ? "true" : "false");
	s_cat(out, ",\"source\":");
	s_cat(out, identity.p);
	s_cat(out, ",\"");
	s_cat(out, !strcmp(request, "check") ? "findings" : request);
	s_cat(out, "\":[");
	s_cat(out, records.p ? records.p : "");
	s_cat(out, "],\"diagnostics\":[");
	s_cat(out, diagnostics.p ? diagnostics.p : "");
	s_cat(out, "]}");
	s_free(&records);
	s_free(&src);
	s_free(&identity);
	s_free(&diagnostics);
	free(c.lines);
	s_free(&ruletext);
	s_free(&ruleidentity);
	ix_settingsfree(&settings);
	v_free(&c.symbols);
	lg_ln = oldln;
	lg_col = oldcol;
	return bad || !strcmp(state, "unreadable");
}

/* Render every field from the typed evidence, including future extensions. */
static void ix_fields(ent *fields, unsigned depth)
{
	ent *e;
	str text;
	unsigned i;

	for (e = fields; e; e = e->nx) {
		for (i = 0; i < depth; i++)
			printf("  ");
		printf("%s:", e->k);
		if (e->map) {
			printf("\n");
			ix_fields(e->map, depth + 1);
		} else {
			s_init(&text);
			j_out(&text, e->map, e->n, e->s, e->ty, 0, 0);
			printf(" %s\n", text.p ? text.p : "null");
			s_free(&text);
		}
	}
}

/* Render the same typed records for a person without parsing Hibr again. */
static void ix_plain(sh *s, const char *request, const char *path)
{
	char **keys = xm(4 * sizeof *keys);
	vec rows = {0}, params = {0};
	const char *state, *name, *line, *message, *known, *value;
	size_t i, j;

	keys[0] = "state";
	state = hibr_getp(s, "RET", keys, 1);
	printf("%s: %s (%s)\n", path, request, state ? state : "unknown");
	keys[0] = !strcmp(request, "check") ? "findings" : (char *)request;
	hibr_list(s, "RET", keys, 1, &rows, 1);
	for (i = 0; i < rows.n; i++) {
		keys[1] = rows.p[i];
		keys[2] = "name";
		name = hibr_getp(s, "RET", keys, 3);
		keys[2] = "message";
		message = hibr_getp(s, "RET", keys, 3);
		keys[2] = "known";
		known = hibr_getp(s, "RET", keys, 3);
		keys[2] = "span";
		keys[3] = "line";
		line = hibr_getp(s, "RET", keys, 4);
		printf("  %s: %s", line ? line : "?",
		       name && *name ? name
		       : message     ? message
				     : "unresolved command");
		if (!strcmp(request, "calls")) {
			keys[2] = "resolution";
			value = hibr_getp(s, "RET", keys, 3);
			printf(" %s", value ? value : "unresolved");
			keys[2] = "bound";
			value = hibr_getp(s, "RET", keys, 3);
			if (value && !strcmp(value, "true")) {
				keys[2] = "binding";
				value = hibr_getp(s, "RET", keys, 3);
				printf(" bound (%s :=)", value ? value : "?");
			}
			keys[2] = "tested";
			value = hibr_getp(s, "RET", keys, 3);
			if (value && !strcmp(value, "true"))
				printf(" tested");
		}
		if (!strcmp(request, "vars")) {
			keys[2] = "access";
			value = hibr_getp(s, "RET", keys, 3);
			printf(" %s", value ? value : "?");
			keys[2] = "form";
			value = hibr_getp(s, "RET", keys, 3);
			printf(" %s", value ? value : "?");
			keys[2] = "owner";
			value = hibr_getp(s, "RET", keys, 3);
			if (value && *value)
				printf(" in %s", value);
		}
		if (!strcmp(request, "functions")) {
			keys[2] = "params";
			hibr_list(s, "RET", keys, 3, &params, 1);
			printf("(");
			for (j = 0; j < params.n; j++) {
				char **pk = xm(5 * sizeof *pk);
				pk[0] = keys[0];
				pk[1] = keys[1];
				pk[2] = "params";
				pk[3] = params.p[j];
				pk[4] = "type";
				value = hibr_getp(s, "RET", pk, 5);
				printf("%s%s ", j ? ", " : "", value ? value : "str");
				pk[4] = "name";
				value = hibr_getp(s, "RET", pk, 5);
				printf("%s", value ? value : "?");
				free(pk);
			}
			printf(")");
			v_free(&params);
			memset(&params, 0, sizeof params);
		}
		printf(" [%s]\n", known ? known : "unknown");
	}
	v_free(&rows);
	memset(&rows, 0, sizeof rows);
	keys[0] = "diagnostics";
	hibr_list(s, "RET", keys, 1, &rows, 1);
	for (i = 0; i < rows.n; i++) {
		keys[1] = rows.p[i];
		keys[2] = "message";
		message = hibr_getp(s, "RET", keys, 3);
		keys[2] = "span";
		keys[3] = "line";
		line = hibr_getp(s, "RET", keys, 4);
		printf("  %s: %s\n", line ? line : "?",
		       message ? message : "diagnostic");
	}
	v_free(&rows);
	free(keys);
	printf("Evidence (byte offsets, end exclusive; endline is the last covered "
	       "line):\n");
	ix_fields(v_find(s, "RET")->map, 1);
}

/* Render the preliminary JSON records without exposing parser nodes. */
static int ix_builtin(sh *s, int ac, char **av)
{
	str out, options;
	int status, i, json = 0;

	if (ac < 3)
		return 2;
	s_init(&out);
	s_init(&options);
	s_ch(&options, '{');
	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-j")) {
			json = 1;
			continue;
		}
		if (options.n > 1)
			s_ch(&options, ',');
		if (!strcmp(av[i], "--symbols")) {
			if (++i >= ac) {
				s_free(&out);
				s_free(&options);
				return 2;
			}
			s_cat(&options, "\"symbols\":");
			s_cat(&options, av[i]);
		} else if (!strcmp(av[i], "--rules")) {
			if (++i >= ac) {
				s_free(&out);
				s_free(&options);
				return 2;
			}
			s_cat(&options, "\"rules\":");
			ix_quote(&options, av[i]);
		} else {
			s_cat(&options, "\"function\":");
			ix_quote(&options, av[i]);
		}
	}
	s_ch(&options, '}');
	status = ix_query(s, av[1], av[2], options.p, &out);
	s_free(&options);
	{
		char **args = xm(4 * sizeof *args);
		args[0] = "json";
		args[1] = "parse";
		args[2] = "RET";
		args[3] = out.p;
		b_json(s, 4, args);
		free(args);
	}
	if (!s->bind) {
		if (json)
			printf("%s\n", out.p);
		else
			ix_plain(s, av[1], av[2]);
	}
	s_free(&out);
	return status;
}

static const ix_api ix_table = {ix_query};
static const hibr_bi ix_builtins[] = {
    {"inspect", ix_builtin, "experimental inspection: inspect functions|calls|refs|vars|check FILE -j"},
    HIBR_BI_END};

/* Register the inspection producer. */
static int ix_init(sh *s)
{
	ix_lint = hibr_require(s, "lint", LI_API_VER);
	if (!ix_lint)
		return 1;
	return hibr_provide(s, "inspect", IX_API_VER, (void *)&ix_table);
}

/* Withdraw the inspection producer. */
static void ix_fini(sh *s)
{
	hibr_unprovide(s, "inspect");
}
HIBR_MODULE_P("inspect", "0.0.2", "experimental source inspection records", ix_builtins,
	      ix_init, ix_fini, "inspect");
