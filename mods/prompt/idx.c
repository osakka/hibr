#include "pr.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Read a big endian thirty two bit field. */
unsigned ix_be32(const unsigned char *p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
	       ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/* Read the offset style varint git uses for index and pack offsets. */
size_t ix_varint(const unsigned char *p, size_t n, size_t *pos)
{
	size_t v;
	unsigned char c;

	if (*pos >= n)
		return 0;
	c = p[(*pos)++];
	v = c & 127;
	while (c & 128) {
		if (*pos >= n)
			return v;
		v += 1;
		c = p[(*pos)++];
		v = (v << 7) + (c & 127);
	}
	return v;
}

/* Release one cached tree record. */
void ix_ctfree(void *p)
{
	struct ct *t = (struct ct *)p;

	free(t->path);
	free(t);
}

/* Read the cached tree extension, which records per directory names. */
void ix_tree(gidx *x, const unsigned char *p, size_t n, const char *prefix)
{
	size_t pos = 0;

	while (pos < n) {
		const unsigned char *nul = memchr(p + pos, 0, n - pos);
		struct ct *t;
		str path;
		long cnt, sub;
		char *e;
		size_t nl;
		if (!nul)
			return;
		nl = (size_t)(nul - (p + pos));
		s_init(&path);
		s_cat(&path, prefix);
		if (nl) {
			s_add(&path, (const char *)p + pos, nl);
			s_ch(&path, '/');
		}
		pos += nl + 1;
		if (pos >= n) {
			s_free(&path);
			return;
		}
		cnt = strtol((const char *)p + pos, &e, 10);
		pos += (size_t)(e - (const char *)(p + pos));
		while (pos < n && p[pos] == ' ')
			pos++;
		sub = strtol((const char *)p + pos, &e, 10);
		pos += (size_t)(e - (const char *)(p + pos));
		while (pos < n && (p[pos] == '\n' || p[pos] == '\r'))
			pos++;
		t = xm(sizeof *t);
		memset(t, 0, sizeof *t);
		t->path = path.p ? path.p : xs("");
		t->cnt = cnt;
		if (cnt >= 0) {
			if (pos + 20 > n) {
				free(t->path);
				free(t);
				return;
			}
			memcpy(t->sha, p + pos, 20);
			pos += 20;
		}
		v_add(&x->ctrees, t);
		if (sub > 0) {
			size_t used = pos;
			ix_tree(x, p + used, n - used, t->path);
			return;
		}
	}
}

/* Read one index entry, returning its length or zero. */
size_t ix_entry(gidx *x, const unsigned char *p, size_t n, size_t pos,
		const char **prev)
{
	struct ie *e;
	size_t base, nl, total, start = pos;
	unsigned flags;
	int ext;
	str path;

	if (pos + 62 > n)
		return 0;
	flags = ((unsigned)p[pos + 60] << 8) | p[pos + 61];
	ext = (flags & 0x4000) && x->ver >= 3;
	base = ext ? 64 : 62;
	if (pos + base > n)
		return 0;
	e = xm(sizeof *e);
	memset(e, 0, sizeof *e);
	e->ctim = ix_be32(p + pos);
	e->ctin = ix_be32(p + pos + 4);
	e->mtim = ix_be32(p + pos + 8);
	e->mtin = ix_be32(p + pos + 12);
	e->dev = ix_be32(p + pos + 16);
	e->ino = ix_be32(p + pos + 20);
	e->mode = ix_be32(p + pos + 24);
	e->uid = ix_be32(p + pos + 28);
	e->gid = ix_be32(p + pos + 32);
	e->size = ix_be32(p + pos + 36);
	memcpy(e->sha, p + pos + 40, 20);
	e->stage = (int)((flags >> 12) & 3);
	pos += base;
	s_init(&path);
	if (x->ver >= 4) {
		size_t strip = ix_varint(p, n, &pos);
		const unsigned char *nul;
		size_t plen = *prev ? strlen(*prev) : 0;
		if (strip > plen) {
			free(e);
			s_free(&path);
			return 0;
		}
		s_add(&path, *prev ? *prev : "", plen - strip);
		nul = memchr(p + pos, 0, n - pos);
		if (!nul) {
			free(e);
			s_free(&path);
			return 0;
		}
		s_add(&path, (const char *)p + pos, (size_t)(nul - (p + pos)));
		pos = (size_t)(nul - p) + 1;
		total = pos - start;
	} else {
		const unsigned char *nul = memchr(p + pos, 0, n - pos);
		if (!nul) {
			free(e);
			s_free(&path);
			return 0;
		}
		nl = (size_t)(nul - (p + pos));
		s_add(&path, (const char *)p + pos, nl);
		total = (base + nl + 8) & ~(size_t)7;
	}
	e->path = path.p ? path.p : xs("");
	*prev = e->path;
	v_add(&x->ents, e);
	return total;
}

/* Read the whole index file. */
gidx *ix_read(grepo *g)
{
	char *path = gt_join(g->dir, "index");
	unsigned char *p;
	size_t n, pos = 12;
	gidx *x;
	unsigned cnt, i;
	const char *prev = 0;
	struct stat st;

	if (stat(path, &st) != 0) {
		free(path);
		return 0;
	}
	p = ob_map(path, &n);
	free(path);
	if (!p)
		return 0;
	if (n < 32 || memcmp(p, "DIRC", 4)) {
		lg(HIBR_LDBG, "index: bad signature");
		munmap(p, n);
		return 0;
	}
	x = xm(sizeof *x);
	memset(x, 0, sizeof *x);
	x->ver = ix_be32(p + 4);
	cnt = ix_be32(p + 8);
	x->mtim = (unsigned)st.st_mtime;
	x->mtimns = (unsigned)HIBR_MTIM(st).tv_nsec;
	if (x->ver < 2 || x->ver > 4) {
		lg(HIBR_LWRN, "index version %u unsupported", x->ver);
		munmap(p, n);
		free(x);
		return 0;
	}
	for (i = 0; i < cnt; i++) {
		size_t used = ix_entry(x, p, n - 20, pos, &prev);
		if (!used) {
			lg(HIBR_LDBG, "index: truncated at entry %u", i);
			break;
		}
		pos += used;
	}
	while (pos + 8 <= n - 20) {
		unsigned len = ix_be32(p + pos + 4);
		if (pos + 8 + len > n - 20)
			break;
		if (!memcmp(p + pos, "TREE", 4))
			ix_tree(x, p + pos + 8, len, "");
		pos += 8 + len;
	}
	munmap(p, n);
	lg(HIBR_LDBG, "index v%u, %lu entries, %lu cached trees", x->ver,
	   (unsigned long)x->ents.n, (unsigned long)x->ctrees.n);
	return x;
}

/* Release the index. */
void ix_free(gidx *x)
{
	size_t i;

	if (!x)
		return;
	for (i = 0; i < x->ents.n; i++) {
		struct ie *e = (struct ie *)x->ents.p[i];
		free(e->path);
		free(e);
	}
	v_free(&x->ents);
	for (i = 0; i < x->ctrees.n; i++)
		ix_ctfree(x->ctrees.p[i]);
	v_free(&x->ctrees);
	free(x);
}

/* Find the cached tree record for a directory prefix. */
struct ct *ix_ct(gidx *x, const char *prefix)
{
	size_t i;

	for (i = 0; i < x->ctrees.n; i++) {
		struct ct *t = (struct ct *)x->ctrees.p[i];
		if (!strcmp(t->path, prefix))
			return t;
	}
	return 0;
}
