#include "../../include/pri.h"
#include "inspect.h"
#include "../lint.h"

static const li_api *ix_lint;
const char *p_param(lex *, node *, const char *, const char *);
#include <stdio.h>
#include <string.h>
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
	vec names;
	ix_pos **positions;
	const char *input;
	size_t inputsize, origin;
	size_t start, end, cursor, fncursor, unitstart, unitend;
	size_t *lines, nlines;
	int count, subdepth, partial;
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
static void ix_observe(void *data, const void *object, const char *begin, const char *end)
{
	ix_ctx *c = data;
	ix_pos *p;
	size_t bucket = ix_bucket(object);

	if ((uintptr_t)begin < (uintptr_t)c->input || (uintptr_t)end < (uintptr_t)begin ||
	    (uintptr_t)end > (uintptr_t)c->input + c->inputsize) return;
	for (p = c->positions[bucket]; p && p->object != object; p = p->next) {}
	if (!p) {
		p = xm(sizeof *p); p->object = object;
		p->next = c->positions[bucket]; c->positions[bucket] = p;
	}
	p->start = c->origin + (size_t)(begin - c->input);
	p->end = c->origin + (size_t)(end - c->input);
}

/* Read evidence recorded by the parser rather than matching source text. */
static int ix_position(ix_ctx *c, const void *object, size_t *start, size_t *end)
{
	ix_pos *p;

	for (p = c->positions[ix_bucket(object)]; p; p = p->next)
		if (p->object == object) { *start = p->start; *end = p->end; return 1; }
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

	s_cat(out, "{\"path\":"); ix_quote(out, path);
	if (!available) { s_cat(out, ",\"size\":null,\"id\":null}"); return; }
	for (i = 0; i < src->n; i++) { hash ^= (unsigned char)src->p[i]; hash *= UINT64_C(1099511628211); }
	s_fmt(out, ",\"size\":%lu,\"id\":\"fnv1a64:%016llx\"}", (unsigned long)src->n, (unsigned long long)hash);
}

/* Find a byte offset's line using the source's line index. */
static size_t ix_line(ix_ctx *c, size_t offset)
{
	size_t lo = 0, hi = c->nlines, mid;

	while (lo + 1 < hi) {
		mid = lo + (hi - lo) / 2;
		if (c->lines[mid] <= offset) lo = mid;
		else hi = mid;
	}
	return lo;
}

/* Append a source range using byte-based coordinates. */
static void ix_span(ix_ctx *c, size_t start, size_t end)
{
	size_t line, eline;

	line = ix_line(c, start); eline = ix_line(c, end);

	s_fmt(c->out, "\"span\":{\"start\":%lu,\"end\":%lu,\"line\":%lu,\"col\":%lu,\"endline\":%lu}",
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
	while (n && n->k == N_SEQ) { ix_names(c, n->l); n = n->r; }
	if (!n) return;
	if (n->k == N_FUNC && n->s) v_add(&c->names, n->s);
	ix_names(c, n->l); ix_names(c, n->r);
	if (n->k != N_FUNC) ix_names(c, n->x);
}

/* Recognize a function defined in the inspected source. */
static int ix_named(ix_ctx *c, const char *name)
{
	size_t i;

	for (i = 0; name && i < c->names.n; i++)
		if (!strcmp(name, c->names.p[i])) return 1;
	return 0;
}

/* Locate a node from private parser evidence. */
static size_t ix_start(ix_ctx *c, node *n)
{
	size_t start, end;

	if (ix_position(c, n, &start, &end)) return start;
	return n->ln && n->ln <= c->nlines ? c->lines[n->ln - 1] : 0;
}

/* Start one evidence record with its source identity and location. */
static void ix_record(ix_ctx *c, size_t start, size_t end, const char *known)
{
	if (c->count++) s_ch(c->out, ',');
	s_cat(c->out, "{\"source\":"); s_cat(c->out, c->identity);
	s_cat(c->out, ",\"known\":"); ix_quote(c->out, known);
	s_ch(c->out, ','); ix_span(c, start, end);
	if (c->subdepth) s_cat(c->out, ",\"scope\":\"substitution\"");
}

/* Describe the author's preceding comment without treating it as behavior. */
static void ix_summary(ix_ctx *c, size_t start)
{
	size_t line = ix_line(c, start), first = line, begin, pos, end;
	str text;

	while (first > 0) {
		pos = c->lines[first - 1];
		while (c->src[pos] == ' ' || c->src[pos] == '\t') pos++;
		if (c->src[pos] != '#') break;
		first--;
	}
	if (first == line) return;
	begin = c->lines[first];
	s_init(&text);
	for (; first < line; first++) {
		pos = c->lines[first]; end = c->lines[first + 1];
		while (pos < end && (c->src[pos] == ' ' || c->src[pos] == '\t')) pos++;
		if (pos < end && c->src[pos] == '#') pos++;
		if (pos < end && c->src[pos] == ' ') pos++;
		while (end > pos && (c->src[end - 1] == '\n' || c->src[end - 1] == '\r')) end--;
		if (text.n) s_ch(&text, ' ');
		s_add(&text, c->src + pos, end - pos);
	}
	s_cat(c->out, ",\"summary\":{\"known\":\"declared\",\"text\":"); ix_quote(c->out, text.p);
	s_cat(c->out, ",\"source\":"); s_cat(c->out, c->identity);
	s_ch(c->out, ','); ix_span(c, begin, start);
	s_ch(c->out, '}'); s_free(&text);
}

/* Locate a function's body through lexer tokens without reading its contents. */
static void ix_body(ix_ctx *c, node *n, size_t start, size_t end)
{
	sh own;
	lex l;
	const char *first;

	if (!n->tx) return;
	memset(&own, 0, sizeof own); own.ar = ar_new(4096);
	lx_init(&l, &own, n->tx); lx_next(&l);
	first = ix_literal(l.w);
	if (first && (!strcmp(first, "fn") || !strcmp(first, "function"))) lx_next(&l);
	lx_next(&l);
	if (l.tk == T_LP) { lx_span(&l); lx_arrow(&l); lx_next(&l); }
	while (l.tk == T_NL) lx_next(&l);
	if (l.tkb && l.tk != T_EOF) {
		s_cat(c->out, ",\"body\":{");
		ix_span(c, start + (size_t)(l.tkb - n->tx), end);
		s_ch(c->out, '}');
	}
	v_free(&l.hq); ar_free(own.ar);
}

/* Recover exact declared parameter and default spans using the same parser. */
static void ix_parameter(ix_ctx *c, node *function, int index, size_t start, size_t end)
{
	sh own;
	lex l;
	node parameter;
	char *signature;
	const char *at, *next = 0, *equal, *last;
	size_t base;
	str text;
	int i;

	memset(&own, 0, sizeof own); own.ar = ar_new(4096);
	lx_init(&l, &own, function->tx ? function->tx : "");
	lx_next(&l); lx_next(&l); lx_next(&l);
	base = start + (size_t)(l.p - (function->tx ? function->tx : ""));
	signature = l.tk == T_LP ? lx_span(&l) : 0;
	at = signature;
	for (i = 0; at && i <= index; i++) {
		while (*at == ' ' || *at == '\t' || *at == '\n' || *at == ',') at++;
		memset(&parameter, 0, sizeof parameter);
		next = p_param(&l, &parameter, at, signature + strlen(signature));
		if (!next) break;
		if (i == index) {
			last = next;
			while (last > at && (last[-1] == ',' || last[-1] == ' ' || last[-1] == '\t' || last[-1] == '\n')) last--;
			s_ch(c->out, ','); ix_span(c, base + (size_t)(at - signature), base + (size_t)(last - signature));
			equal = parameter.w ? memchr(at, '=', (size_t)(last - at)) : 0;
			if (equal) {
				equal++;
				while (equal < last && (*equal == ' ' || *equal == '\t')) equal++;
				s_init(&text); s_add(&text, equal, (size_t)(last - equal));
				s_cat(c->out, ",\"default\":{\"known\":\"declared\",\"evaluated\":false,\"text\":");
				ix_quote(c->out, text.p);
				s_ch(c->out, ','); ix_span(c, base + (size_t)(equal - signature), base + (size_t)(last - signature));
				s_cat(c->out, ",\"source\":"); s_cat(c->out, c->identity); s_ch(c->out, '}');
				s_free(&text);
			}
			break;
		}
		at = next;
	}
	if (!signature || !next) { s_ch(c->out, ','); ix_span(c, start, end); }
	v_free(&l.hq); ar_free(own.ar);
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
	for (w = n->aw; w; w = w->nx) lx_next(&l);
	if (n->f == 2) { lx_next(&l); lx_next(&l); }
	s_init(&text);
	if (l.tkb && l.p >= l.tkb) s_add(&text, l.tkb, (size_t)(l.p - l.tkb));
	ix_quote(c->out, text.p);
	s_free(&text); v_free(&l.hq); ar_free(own.ar);
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

	if (!c->rules || !command) return 0;
	keys = xm(4 * sizeof *keys);
	keys[0] = "roles";
	hibr_list(c->rules, "RULES", keys, 1, &roles, 1);
	for (i = 0; i < roles.n; i++) {
		keys[1] = roles.p[i]; keys[2] = "registrations"; keys[3] = (char *)command;
		value = hibr_getp(c->rules, "RULES", keys, 4);
		position = value ? strtol(value, 0, 10) : 0;
		if (position <= 0 || position > 4096) continue;
		for (w = n->w; w && position; position--) w = w->nx;
		prefix = ix_literal(w);
		if (prefix && isname(prefix)) { *role = roles.p[i]; break; }
		prefix = 0;
	}
	free(keys); v_free(&roles); return prefix;
}

/* Describe a data-derived registration as an inference, not a language fact. */
static void ix_registration_record(ix_ctx *c, node *n)
{
	const char *role = 0, *prefix = ix_registration(c, n, &role);

	if (!prefix) return;
	s_cat(c->out, ",\"registration\":{\"known\":\"inferred\",\"prefix\":"); ix_quote(c->out, prefix);
	s_cat(c->out, ",\"role\":"); ix_quote(c->out, role);
	s_cat(c->out, ",\"rule\":"); ix_quote(c->out, role);
	s_cat(c->out, ",\"rules_path\":"); ix_quote(c->out, c->rulespath);
	s_cat(c->out, ",\"rules_source\":"); s_cat(c->out, c->ruleidentity); s_ch(c->out, '}');
}

/* Infer callback edges only from an explicitly supplied desktop rule file. */
static void ix_convention(ix_ctx *c, node *n, size_t start, size_t end)
{
	const char *prefix, *role = 0, *id;
	vec suffixes = {0};
	char **keys;
	str target, rule;
	size_t i;

	if (!c->rules || strcmp(c->request, "calls")) return;
	prefix = ix_registration(c, n, &role);
	if (!prefix) return;
	keys = xm(5 * sizeof *keys);
	keys[0] = "roles"; keys[1] = (char *)role; keys[2] = "callbacks";
	hibr_list(c->rules, "RULES", keys, 3, &suffixes, 1);
	for (i = 0; i < suffixes.n; i++) {
		s_init(&target); s_init(&rule);
		s_fmt(&target, "%s_%s", prefix, (char *)suffixes.p[i]);
		keys[3] = suffixes.p[i]; keys[4] = "id";
		id = hibr_getp(c->rules, "RULES", keys, 5);
		if (id && *id) s_cat(&rule, id);
		else s_fmt(&rule, "%s.%s", role, (char *)suffixes.p[i]);
		if (ix_named(c, target.p)) {
			ix_record(c, start, end, "inferred");
			s_cat(c->out, ",\"name\":"); ix_quote(c->out, target.p);
			s_cat(c->out, ",\"rule\":"); ix_quote(c->out, rule.p);
			s_cat(c->out, ",\"rules_path\":"); ix_quote(c->out, c->rulespath);
			s_cat(c->out, ",\"rules_source\":"); s_cat(c->out, c->ruleidentity);
			s_cat(c->out, ",\"resolution\":\"function\",\"dynamic\":false,\"bound\":false}");
		}
		s_free(&target); s_free(&rule);
	}
	v_free(&suffixes); free(keys);
}

static void ix_walk(ix_ctx *, node *);

/* Parse unmodified substitution bytes with their original source coordinates. */
static void ix_words(ix_ctx *c, word *w, size_t *search)
{
	part *p;
	sh own;
	lex l;
	node *tree;
	const char *input;
	size_t start, end, names, origin, inputsize;
	int lv;

	for (; w; w = w->nx) for (p = w->p; p; p = p->nx) {
		if ((p->k == P_CMD || p->k == P_PSUB) && p->t && *p->t) {
			if (!ix_position(c, p, &start, &end)) { c->partial = 1; continue; }
			if (end - start != strlen(p->t) || memcmp(c->src + start, p->t, end - start) || c->subdepth >= 32) {
				c->partial = 1;
				ix_record(c, start, end, "syntax");
				s_cat(c->out, ",\"name\":null,\"dynamic\":true,\"resolution\":\"unresolved\",\"reason\":\"transformed-or-deep-substitution\"}");
				continue;
			}
			names = c->names.n; input = c->input; origin = c->origin; inputsize = c->inputsize;
			c->input = p->t; c->origin = start; c->inputsize = end - start;
			memset(&own, 0, sizeof own); own.ar = ar_new(4096);
			own.ln = (unsigned)(ix_line(c, start) + 1);
			lx_init(&l, &own, p->t); l.observe = ix_observe; l.observer = c;
			lv = hibr_lv; hibr_lv = -1;
			lx_next(&l); tree = p_list(&l);
			c->subdepth++;
			if (tree && !l.err && !l.more) { ix_names(c, tree); ix_walk(c, tree); }
			else {
				c->partial = 1;
				ix_record(c, start, end, "syntax");
				s_cat(c->out, ",\"name\":null,\"dynamic\":true,\"resolution\":\"unresolved\",\"reason\":\"substitution-not-analysed\"}");
			}
			c->subdepth--; c->names.n = names;
			c->input = input; c->origin = origin; c->inputsize = inputsize;
			hibr_lv = lv; v_free(&l.hq); ar_free(own.ar);
			*search = end;
		}
		ix_words(c, p->arg, search); ix_words(c, p->idx, search);
	}
}

/* Identify command-name code whose literal arguments must remain private. */
static int ix_namecode(part *p)
{
	word *w;

	for (; p; p = p->nx) {
		if (p->k == P_CMD || p->k == P_PSUB) return 1;
		for (w = p->arg; w; w = w->nx) if (ix_namecode(w->p)) return 1;
		for (w = p->idx; w; w = w->nx) if (ix_namecode(w->p)) return 1;
	}
	return 0;
}

/* Walk parsed units to produce function, call and literal-reference records. */
static void ix_walk(ix_ctx *c, node *n)
{
	node *p;
	word *w;
	const char *was, *name, *kind;
	size_t start, end, search;
	redir *r;
	int count, located;

	while (n && n->k == N_SEQ) { ix_walk(c, n->l); n = n->r; }
	if (!n) return;
	start = ix_start(c, n);
	end = n->tx ? start + strlen(n->tx) : start;
	ix_position(c, n, &start, &end);
	if (end > c->end) end = c->end;
	if (n->k == N_FUNC) {
		c->fncursor = start + 1;
		was = c->owner;
		c->owner = n->s;
		if (!strcmp(c->request, "functions")) {
			ix_record(c, start, end, "syntax");
			s_cat(c->out, ",\"name\":"); ix_quote(c->out, n->s);
			if (!c->subdepth) s_fmt(c->out, ",\"line\":%u", n->ln);
			s_cat(c->out, ",\"params\":[");
			for (p = n->x, count = 0; p; p = p->x) {
				if (count++) s_ch(c->out, ',');
				s_cat(c->out, "{\"name\":"); ix_quote(c->out, p->s);
				s_cat(c->out, ",\"known\":\"declared\",\"type\":"); ix_quote(c->out, p->tx);
				s_fmt(c->out, ",\"rest\":%s,\"has_default\":%s", p->f & TY_REST ? "true" : "false", p->w ? "true" : "false");
				s_cat(c->out, ",\"source\":"); s_cat(c->out, c->identity);
				ix_parameter(c, n, count - 1, start, end); s_ch(c->out, '}');
			}
			s_ch(c->out, ']');
			ix_summary(c, start);
			ix_body(c, n, start, end);
			s_fmt(c->out, ",\"declared_signature\":%s", n->f == 1 ? "true" : "false");
			s_ch(c->out, '}');
		}
		ix_walk(c, n->r);
		c->owner = was;
		return;
	}
	search = start;
	if ((!strcmp(c->request, "calls") || !strcmp(c->request, "refs")) && n->aw) ix_words(c, n->aw, &search);
	if (n->k == N_CMD && n->w && (!c->filter || (c->owner && !strcmp(c->filter, c->owner)))) {
		name = ix_literal(n->w);
		located = ix_callspan(c, n, &start, &end);
		ix_convention(c, n, start, end);
		if (!strcmp(c->request, "calls")) {
			kind = !name ? "unresolved" : ix_named(c, name) ? "function" :
			       bi_find(name) ? "builtin" : m_find(c->caller, name) ? "module" : "unresolved";
			ix_record(c, start, end, "syntax");
			s_cat(c->out, ",\"name\":"); ix_quote(c->out, name);
			if (!c->subdepth) s_fmt(c->out, ",\"line\":%u", n->ln);
			s_cat(c->out, ",\"owner\":"); ix_quote(c->out, c->owner);
			s_cat(c->out, ",\"resolution\":"); ix_quote(c->out, kind);
			s_fmt(c->out, ",\"dynamic\":%s,\"bound\":%s", name ? "false" : "true", n->f == 2 ? "true" : "false");
			if (!name) {
				s_cat(c->out, ",\"expression\":");
				if (ix_namecode(n->w->p)) s_cat(c->out, "null,\"expression_state\":\"source-span-required\"");
				else if (located && end > start) {
					str expression;
					s_init(&expression); s_add(&expression, c->src + start, end - start);
					ix_quote(c->out, expression.p); s_free(&expression);
				} else ix_expression(c, n);
			}
			ix_registration_record(c, n);
			s_ch(c->out, '}');
		}
		if (!strcmp(c->request, "refs"))
			for (w = n->w->nx; w; w = w->nx)
				if ((name = ix_literal(w)) && ix_named(c, name)) {
					ix_position(c, w, &start, &end);
					ix_record(c, start, end, "syntax");
					s_cat(c->out, ",\"name\":"); ix_quote(c->out, name);
			if (!c->subdepth) s_fmt(c->out, ",\"line\":%u", n->ln);
					s_cat(c->out, ",\"owner\":"); ix_quote(c->out, c->owner);
					s_ch(c->out, '}');
				}
	}
	if (!strcmp(c->request, "calls") || !strcmp(c->request, "refs")) ix_words(c, n->w, &search);
	ix_walk(c, n->l); ix_walk(c, n->r); ix_walk(c, n->x);
	if (!strcmp(c->request, "calls") || !strcmp(c->request, "refs"))
		for (r = n->rd; r; r = r->nx) ix_words(c, r->w, &search);
}

/* Refuse invalid UTF-8 rather than emitting malformed JSON text. */
static int ix_utf8(const str *src)
{
	size_t i = 0;
	unsigned cp;
	int n;

	while (i < src->n) {
		if ((unsigned char)src->p[i] < 128) { i++; continue; }
		n = u8dec(src->p + i, src->n - i, &cp);
		if (n == 1 || n > 4 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
		    (n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)) return 0;
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

	if (net_schnm(s, path) && !net_sch(s, path)) return 2;
	fd = net_sch(s, path) ? net_open(s, path) : open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) return 1;
	if (fstat(fd, &st) || (!S_ISREG(st.st_mode) && !net_sch(s, path))) { close(fd); return 3; }
	buf = xm(HIBR_IOCH);
	while ((n = read(fd, buf, HIBR_IOCH)) > 0) {
		if (src->n + (size_t)n > 4194304) { free(buf); close(fd); return 4; }
		s_add(src, buf, (size_t)n);
	}
	free(buf); close(fd);
	if (n < 0) return 5;
	if (src->p && strlen(src->p) != src->n) return 6;
	return ix_utf8(src) ? 0 : 7;
}

/* Read silently, including diagnostics produced by a registered local scheme. */
static int ix_read(sh *s, const char *path, str *src)
{
	int lv = hibr_lv, status;

	hibr_lv = -1; status = ix_read_impl(s, path, src); hibr_lv = lv;
	return status;
}

/* Explain a refused source without describing unread bytes as inspected facts. */
static const char *ix_read_error(int status)
{
	switch (status) {
	case 1: return "source could not be opened";
	case 2: return "a non-local source scheme is refused";
	case 3: return "source is not a regular file or registered local scheme";
	case 4: return "source exceeds the 4 MiB size limit";
	case 5: return "source read failed";
	case 6: return "source contains a NUL byte";
	case 7: return "source is not valid UTF-8";
	default: return "invalid inspection options or convention data";
	}
}

/* Convert a shared lint finding to an evidence record. */
static void ix_finding(void *data, unsigned line, const char *rule, const char *message)
{
	ix_ctx *c = data;
	size_t start = line && line <= c->nlines ? c->lines[line - 1] : 0;
	size_t end = line < c->nlines ? c->lines[line] : strlen(c->src);

	ix_record(c, start, end, "inferred");
	s_cat(c->out, ",\"rule\":"); ix_quote(c->out, rule);
	s_cat(c->out, ",\"message\":"); ix_quote(c->out, message);
	s_cat(c->out, ",\"reference\":\"CLAUDE.md#traps-already-found--dont-reintroduce-them\"}");
}

/* Parse JSON settings into an isolated variable table. */
static int ix_json(sh *s, const char *name, const char *text)
{
	char **args = xm(4 * sizeof *args);
	int status, lv = hibr_lv;

	args[0] = "json"; args[1] = "parse"; args[2] = (char *)name; args[3] = (char *)text;
	hibr_lv = -1; status = b_json(s, 4, args); hibr_lv = lv;
	free(args); return status;
}

/* Release only the isolated settings table, without global shell cleanup. */
static void ix_settingsfree(sh *s)
{
	size_t i;
	var *v, *next;

	for (i = 0; i < s->tsz; i++) for (v = s->tab[i]; v; v = next) {
		next = v->nx; v_free_el(v); free(v->k); free(v->v); free(v);
	}
	free(s->tab);
}

/* Produce preliminary records in private parser storage, without execution. */
static int ix_query(sh *caller, const char *request, const char *path, const char *options, str *out)
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
	const char *state = "ok";

	memset(&own, 0, sizeof own); memset(&c, 0, sizeof c);
	s_init(&src); s_init(&records); s_init(&identity); s_init(&diagnostics); s_init(&ruletext); s_init(&ruleidentity);
	memset(&settings, 0, sizeof settings);
	if (strcmp(request, "functions") && strcmp(request, "calls") && strcmp(request, "refs") && strcmp(request, "check")) {
		s_cat(out, "{\"inspect\":1,\"state\":\"unsupported\",\"diagnostics\":[]}");
		return 1;
	}
	if (options && ix_json(&settings, "OPTIONS", options)) state = "unreadable";
	if (options) {
		char **keys = xm(sizeof *keys);
		keys[0] = "function"; filter = hibr_getp(&settings, "OPTIONS", keys, 1);
		keys[0] = "rules"; rulespath = hibr_getp(&settings, "OPTIONS", keys, 1);
		free(keys);
		if (rulespath && *rulespath) {
			if (ix_read(caller, rulespath, &ruletext) || ix_json(&settings, "RULES", ruletext.p)) state = "unreadable";
			else { c.rules = &settings; c.rulespath = rulespath; }
		}
	}
	sourcebad = ix_read(caller, path, &src);
	if (sourcebad) state = "unreadable";
	ix_identity(&identity, path, &src, !sourcebad);
	if (c.rules) {
		ix_identity(&ruleidentity, rulespath, &ruletext, 1);
		c.ruleidentity = ruleidentity.p;
	}
	c.out = &records; c.src = src.p ? src.p : ""; c.request = request;
	c.path = path; c.filter = filter && *filter ? filter : 0; c.caller = caller; c.identity = identity.p;
	c.nlines = 1;
	for (i = 0; i < src.n; i++) if (src.p[i] == '\n') c.nlines++;
	c.lines = xm(c.nlines * sizeof *c.lines);
	c.lines[0] = 0;
	for (i = 0, c.nlines = 1; i < src.n; i++)
		if (src.p[i] == '\n') c.lines[c.nlines++] = i + 1;
	if (!strcmp(state, "unreadable")) {
		c.out = &diagnostics; ix_record(&c, 0, 0, "syntax");
		s_cat(&diagnostics, ",\"rule\":\"input\",\"message\":");
		ix_quote(&diagnostics, ix_read_error(sourcebad)); s_ch(&diagnostics, '}');
		c.out = &records; c.count = 0;
	}
	if (strcmp(state, "unreadable")) {
		own.ar = ar_new(4096);
		c.positions = xm(8191 * sizeof *c.positions);
		memset(c.positions, 0, 8191 * sizeof *c.positions); c.input = c.src; c.inputsize = src.n;
		lx_init(&l, &own, c.src);
		l.observe = ix_observe; l.observer = &c;
		lv = hibr_lv; hibr_lv = -1;
		lx_next(&l);
		while (l.tk != T_EOF || l.err || l.more) {
			if (l.tk == T_NL) { lx_next(&l); continue; }
			unitstart = (size_t)((l.tkb ? l.tkb : l.p) - c.src);
			n = p_line(&l);
			if (l.err || l.more) {
				bad = 1; state = l.more ? "incomplete" : "invalid";
				c.out = &diagnostics; c.count = ndiag++;
				ix_record(&c, (size_t)((l.errat ? l.errat : l.p) - c.src), (size_t)(l.p - c.src), "syntax");
				s_cat(&diagnostics, ",\"rule\":\"parse\",\"message\":");
				ix_quote(&diagnostics, l.errmsg ? l.errmsg : l.more ? "incomplete input" : "invalid syntax"); s_ch(&diagnostics, '}');
				c.out = &records; c.count = 0;
				if (l.more) break;
				if (l.errat) l.p = l.errat;
				while (l.p < l.e && *l.p != '\n') l.p++;
				l.lnp = l.bol = c.src; l.ln = 1;
				l.err = 0; l.errmsg = l.errat = 0;
				lx_next(&l);
				continue;
			}
			unit = ar_alloc(own.ar, sizeof *unit);
			unit->tree = n; unit->start = unitstart;
			unit->end = (size_t)((l.tkb ? l.tkb : l.p) - c.src);
			v_add(&units, unit); ix_names(&c, n);
			if (l.tk == T_EOF) break;
			lx_next(&l);
		}
		hibr_lv = lv;
		c.start = 0; c.end = src.n;
		for (i = 0; i < units.n; i++) {
			unit = units.p[i]; c.unitstart = unit->start; c.unitend = unit->end;
			ix_walk(&c, unit->tree);
		}
		c.unitstart = c.unitend = 0;
		if (!strcmp(request, "check") && ix_lint) {
			node *all = 0, **tail = &all, *seq;
			for (i = 0; i < units.n; i++) {
				seq = ar_alloc(own.ar, sizeof *seq);
				unit = units.p[i];
				seq->k = N_SEQ; seq->l = unit->tree; *tail = seq; tail = &seq->r;
			}
			own.src = path;
			lv = hibr_lv; hibr_lv = -1;
			ix_lint->records(&own, all, c.src, ix_finding, &c);
			hibr_lv = lv;
		}
			for (i = 0; i < 8191; i++) {
			ix_pos *p = c.positions[i], *next;
			while (p) { next = p->next; free(p); p = next; }
		}
		free(c.positions);
		v_free(&units); v_free(&c.names); v_free(&l.hq); ar_free(own.ar);
		if (!bad && !src.n) state = "empty";
	}
	s_cat(out, "{\"inspect\":1,\"state\":"); ix_quote(out, state);
	s_fmt(out, ",\"partial\":%s", (bad || c.partial) ? "true" : "false");
	s_cat(out, ",\"source\":"); s_cat(out, identity.p);
	s_cat(out, ",\""); s_cat(out, !strcmp(request, "check") ? "findings" : request); s_cat(out, "\":[");
	s_cat(out, records.p ? records.p : ""); s_cat(out, "],\"diagnostics\":[");
	s_cat(out, diagnostics.p ? diagnostics.p : ""); s_cat(out, "]}");
	s_free(&records); s_free(&src); s_free(&identity); s_free(&diagnostics); free(c.lines);
	s_free(&ruletext); s_free(&ruleidentity); ix_settingsfree(&settings);
	lg_ln = oldln; lg_col = oldcol;
	return bad || !strcmp(state, "unreadable");
}

/* Render the same typed records for a person without parsing Hibr again. */
static void ix_plain(sh *s, const char *request, const char *path)
{
	char **keys = xm(4 * sizeof *keys);
	vec rows = {0}, params = {0};
	const char *state, *name, *line, *message, *known, *value;
	size_t i, j;

	keys[0] = "state"; state = hibr_getp(s, "RET", keys, 1);
	printf("%s: %s (%s)\n", path, request, state ? state : "unknown");
	keys[0] = !strcmp(request, "check") ? "findings" : (char *)request;
	hibr_list(s, "RET", keys, 1, &rows, 1);
	for (i = 0; i < rows.n; i++) {
		keys[1] = rows.p[i]; keys[2] = "name";
		name = hibr_getp(s, "RET", keys, 3);
		keys[2] = "message"; message = hibr_getp(s, "RET", keys, 3);
		keys[2] = "known"; known = hibr_getp(s, "RET", keys, 3);
		keys[2] = "span"; keys[3] = "line"; line = hibr_getp(s, "RET", keys, 4);
		printf("  %s: %s", line ? line : "?", name && *name ? name : message ? message : "unresolved command");
		if (!strcmp(request, "functions")) {
			keys[2] = "params"; hibr_list(s, "RET", keys, 3, &params, 1);
			printf("(");
			for (j = 0; j < params.n; j++) {
				char **pk = xm(5 * sizeof *pk);
				pk[0] = keys[0]; pk[1] = keys[1]; pk[2] = "params"; pk[3] = params.p[j]; pk[4] = "type";
				value = hibr_getp(s, "RET", pk, 5); printf("%s%s ", j ? ", " : "", value ? value : "str");
				pk[4] = "name"; value = hibr_getp(s, "RET", pk, 5); printf("%s", value ? value : "?");
				free(pk);
			}
			printf(")"); v_free(&params); memset(&params, 0, sizeof params);
		}
		printf(" [%s]\n", known ? known : "unknown");
	}
	v_free(&rows); memset(&rows, 0, sizeof rows);
	keys[0] = "diagnostics"; hibr_list(s, "RET", keys, 1, &rows, 1);
	for (i = 0; i < rows.n; i++) {
		keys[1] = rows.p[i]; keys[2] = "message"; message = hibr_getp(s, "RET", keys, 3);
		keys[2] = "span"; keys[3] = "line"; line = hibr_getp(s, "RET", keys, 4);
		printf("  %s: %s\n", line ? line : "?", message ? message : "diagnostic");
	}
	v_free(&rows); free(keys);
}

/* Render the preliminary JSON records without exposing parser nodes. */
static int ix_builtin(sh *s, int ac, char **av)
{
	str out, options;
	int status, i, json = 0;

	if (ac < 3) return 2;
	s_init(&out); s_init(&options); s_ch(&options, '{');
	for (i = 3; i < ac; i++) {
		if (!strcmp(av[i], "-j")) { json = 1; continue; }
		if (options.n > 1) s_ch(&options, ',');
		if (!strcmp(av[i], "--rules")) {
			if (++i >= ac) { s_free(&out); s_free(&options); return 2; }
			s_cat(&options, "\"rules\":"); ix_quote(&options, av[i]);
		} else { s_cat(&options, "\"function\":"); ix_quote(&options, av[i]); }
	}
	s_ch(&options, '}');
	status = ix_query(s, av[1], av[2], options.p, &out);
	s_free(&options);
	{
		char **args = xm(4 * sizeof *args);
		args[0] = "json"; args[1] = "parse"; args[2] = "RET"; args[3] = out.p;
		b_json(s, 4, args);
		free(args);
	}
	if (!s->bind) {
		if (json) printf("%s\n", out.p);
		else ix_plain(s, av[1], av[2]);
	}
	s_free(&out);
	return status;
}

static const ix_api ix_table = { ix_query };
static const hibr_bi ix_builtins[] = {
	{ "inspect", ix_builtin, "experimental inspection: inspect functions FILE -j" },
	HIBR_BI_END
};

/* Register the inspection producer. */
static int ix_init(sh *s)
{
	ix_lint = hibr_require(s, "lint", LI_API_VER);
	if (!ix_lint) return 1;
	return hibr_provide(s, "inspect", IX_API_VER, (void *)&ix_table);
}

/* Withdraw the inspection producer. */
static void ix_fini(sh *s)
{
	hibr_unprovide(s, "inspect");
}
HIBR_MODULE_P("inspect", "0.0.1", "experimental source inspection records", ix_builtins, ix_init, ix_fini, "inspect");
