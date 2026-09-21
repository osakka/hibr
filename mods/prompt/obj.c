#include "pr.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct pack {
	char *path;
	unsigned char *idx, *dat;
	size_t idxn, datn;
	unsigned nobj, ver;
	const unsigned char *sha, *off4, *off8;
};

/* Name an object type the way git spells it. */
const char *ob_tname(int type)
{
	if (type == OB_COMMIT)
		return "commit";
	if (type == OB_TREE)
		return "tree";
	if (type == OB_BLOB)
		return "blob";
	if (type == OB_TAG)
		return "tag";
	return "?";
}

/* Read a hexadecimal object name into twenty bytes. */
int ob_hex(const char *hex, unsigned char *sha)
{
	int i, hi, lo;

	for (i = 0; i < 20; i++) {
		hi = pr_hex(hex[i * 2]);
		lo = pr_hex(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return 0;
		sha[i] = (unsigned char)(hi * 16 + lo);
	}
	return 1;
}

/* Write twenty bytes as a hexadecimal object name. */
void ob_unhex(const unsigned char *sha, char *hex)
{
	const char *d = "0123456789abcdef";
	int i;

	for (i = 0; i < 20; i++) {
		hex[i * 2] = d[sha[i] >> 4];
		hex[i * 2 + 1] = d[sha[i] & 15];
	}
	hex[40] = 0;
}

/* Map a whole file read only, or give nothing. */
unsigned char *ob_map(const char *path, size_t *n)
{
	struct stat st;
	int fd = open(path, O_RDONLY);
	void *p;

	if (fd < 0)
		return 0;
	if (fstat(fd, &st) != 0 || st.st_size <= 0) {
		close(fd);
		return 0;
	}
	p = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return 0;
	*n = (size_t)st.st_size;
	return (unsigned char *)p;
}

/* Read a big endian thirty two bit field. */
unsigned ob_be32(const unsigned char *p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
	       ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/* Collect the object directories, following any alternates. */
void ob_dirs(grepo *g)
{
	char *base = gt_join(g->common, "objects");
	char *alt;
	const char *p, *e;

	v_add(&g->odirs, base);
	alt = gt_join(base, "info/alternates");
	p = pr_slurp(alt, 65536);
	free(alt);
	if (!p)
		return;
	alt = (char *)p;
	while (*p) {
		const char *b = p;
		char *d;
		while (*p && *p != '\n')
			p++;
		e = p;
		while (e > b && (e[-1] == '\r' || e[-1] == ' '))
			e--;
		if (e > b && *b != '#') {
			d = pr_span(b, e);
			v_add(&g->odirs, *d == '/' ? d : gt_join(base, d));
			if (*d != '/')
				free(d);
		}
		if (*p)
			p++;
	}
	free(alt);
}

/* Unmap and release one pack. */
void ob_drop(struct pack *k)
{
	if (k->idx)
		munmap(k->idx, k->idxn);
	if (k->dat)
		munmap(k->dat, k->datn);
	free(k->path);
	free(k);
}

/* Open one pack index and its data file. */
struct pack *ob_pack(const char *idxpath)
{
	struct pack *k = xm(sizeof *k);
	size_t n = strlen(idxpath);
	size_t need;
	char *dp;

	memset(k, 0, sizeof *k);
	k->idx = ob_map(idxpath, &k->idxn);
	if (!k->idx) {
		free(k);
		return 0;
	}
	dp = xm(n + 2);
	memcpy(dp, idxpath, n - 3);
	strcpy(dp + n - 3, "pack");
	k->dat = ob_map(dp, &k->datn);
	k->path = dp;
	if (!k->dat || k->datn < 32) {
		ob_drop(k);
		return 0;
	}
	if (k->idxn > 8 && !memcmp(k->idx, "\377tOc", 4)) {
		k->ver = ob_be32(k->idx + 4);
		if (k->ver != 2) {
			lg(HIBR_LDBG, "pack index version %u unsupported",
			   k->ver);
			ob_drop(k);
			return 0;
		}
		k->nobj = ob_be32(k->idx + 8 + 255 * 4);
		k->sha = k->idx + 8 + 256 * 4;
		k->off4 = k->sha + (size_t)k->nobj * 24;
		k->off8 = k->off4 + (size_t)k->nobj * 4;
		need = 8 + 256 * 4 + (size_t)k->nobj * 28 + 40;
	} else {
		if (k->idxn < 256 * 4) {
			ob_drop(k);
			return 0;
		}
		k->ver = 1;
		k->nobj = ob_be32(k->idx + 255 * 4);
		k->sha = k->idx + 256 * 4;
		k->off4 = 0;
		k->off8 = 0;
		need = 256 * 4 + (size_t)k->nobj * 24 + 40;
	}
	if (need > k->idxn) {
		lg(HIBR_LWRN, "pack index %s is short, ignoring", idxpath);
		ob_drop(k);
		return 0;
	}
	lg(HIBR_LDBG, "pack %s holds %u objects, index v%u", dp, k->nobj,
	   k->ver);
	return k;
}

/* Find every pack under the object directories. */
void ob_packs(grepo *g)
{
	size_t i;

	if (g->packsdone)
		return;
	g->packsdone = 1;
	if (!g->odirs.n)
		ob_dirs(g);
	for (i = 0; i < g->odirs.n; i++) {
		char *pd = gt_join((char *)g->odirs.p[i], "pack");
		DIR *d = opendir(pd);
		struct dirent *e;
		while (d && (e = readdir(d)) != 0) {
			size_t l = strlen(e->d_name);
			char *fp;
			struct pack *k;
			if (l < 5 || strcmp(e->d_name + l - 4, ".idx"))
				continue;
			fp = gt_join(pd, e->d_name);
			k = ob_pack(fp);
			free(fp);
			if (k)
				v_add(&g->packs, k);
		}
		if (d)
			closedir(d);
		free(pd);
	}
}

/* Read the object name stored at one index slot. */
const unsigned char *ob_idxsha(struct pack *k, unsigned i)
{
	if (k->ver == 2)
		return k->sha + (size_t)i * 20;
	return k->sha + (size_t)i * 24 + 4;
}

/* Look one object name up in a pack index. */
int ob_idxfind(struct pack *k, const unsigned char *sha, size_t *off)
{
	unsigned lo, hi, mid;
	const unsigned char *fan = k->ver == 2 ? k->idx + 8 : k->idx;

	if (!k->nobj)
		return 0;
	lo = sha[0] ? ob_be32(fan + (sha[0] - 1) * 4) : 0;
	hi = ob_be32(fan + sha[0] * 4);
	if (hi > k->nobj)
		return 0;
	while (lo < hi) {
		int cmp;
		mid = lo + (hi - lo) / 2;
		cmp = memcmp(sha, ob_idxsha(k, mid), 20);
		if (cmp == 0) {
			if (k->ver == 1) {
				*off = ob_be32(k->sha + (size_t)mid * 24);
				return 1;
			}
			{
				unsigned o = ob_be32(k->off4 +
						     (size_t)mid * 4);
				if (!(o & 0x80000000u)) {
					*off = o;
					return 1;
				}
				o &= 0x7fffffffu;
				if ((size_t)(k->off8 - k->idx) +
					    (size_t)(o + 1) * 8 >
				    k->idxn)
					return 0;
				*off = ((size_t)ob_be32(k->off8 +
							(size_t)o * 8) << 32) |
				       ob_be32(k->off8 + (size_t)o * 8 + 4);
				return 1;
			}
		}
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return 0;
}

/* Read a little endian seven bit varint as deltas encode sizes. */
size_t ob_varint(const unsigned char *p, size_t n, size_t *pos)
{
	size_t v = 0;
	int shift = 0;
	unsigned char c;

	do {
		if (*pos >= n)
			return v;
		c = p[(*pos)++];
		v |= (size_t)(c & 0x7f) << shift;
		shift += 7;
	} while (c & 0x80 && shift < 64);
	return v;
}

/* Rebuild an object from its base and a delta stream. */
int ob_delta(const str *base, const unsigned char *d, size_t dn, str *out)
{
	size_t pos = 0, want;

	if (ob_varint(d, dn, &pos) != base->n) {
		lg(HIBR_LDBG, "delta: base size does not match");
		return 0;
	}
	want = ob_varint(d, dn, &pos);
	if (want > OB_MAX)
		return 0;
	s_init(out);
	s_grow(out, want + 1);
	out->p[0] = 0;
	while (pos < dn) {
		unsigned char c = d[pos++];
		if (c & 0x80) {
			size_t off = 0, len = 0;
			int i;
			for (i = 0; i < 4; i++)
				if (c & (1 << i)) {
					if (pos >= dn)
						goto bad;
					off |= (size_t)d[pos++] << (i * 8);
				}
			for (i = 0; i < 3; i++)
				if (c & (0x10 << i)) {
					if (pos >= dn)
						goto bad;
					len |= (size_t)d[pos++] << (i * 8);
				}
			if (!len)
				len = 0x10000;
			if (off + len > base->n || out->n + len > want)
				goto bad;
			s_add(out, base->p + off, len);
			continue;
		}
		if (!c)
			goto bad;
		if (pos + c > dn || out->n + c > want)
			goto bad;
		s_add(out, (const char *)d + pos, c);
		pos += c;
	}
	if (out->n != want)
		goto bad;
	return 1;
bad:
	lg(HIBR_LDBG, "delta: instruction stream damaged");
	s_free(out);
	return 0;
}

/* Read one packed object, following any delta chain. */
int ob_entry(grepo *g, struct pack *k, size_t off, int depth, int *type,
	     str *out)
{
	const unsigned char *p = k->dat;
	size_t pos = off, base = 0;
	unsigned char c;
	int t;
	size_t sz;
	int shift;
	str bo;
	int bt;

	if (depth > OB_MAXDELTA) {
		lg(HIBR_LERR, "pack: delta chain deeper than %d", OB_MAXDELTA);
		return 0;
	}
	if (pos >= k->datn)
		return 0;
	c = p[pos++];
	t = (c >> 4) & 7;
	sz = c & 15;
	shift = 4;
	while (c & 0x80) {
		if (pos >= k->datn)
			return 0;
		c = p[pos++];
		sz |= (size_t)(c & 0x7f) << shift;
		shift += 7;
	}
	if (sz > g->omax)
		return 0;
	if (t == OB_OFS) {
		size_t d;
		if (pos >= k->datn)
			return 0;
		c = p[pos++];
		d = c & 0x7f;
		while (c & 0x80) {
			if (pos >= k->datn)
				return 0;
			c = p[pos++];
			d = ((d + 1) << 7) | (c & 0x7f);
		}
		if (d > off)
			return 0;
		base = off - d;
	} else if (t == OB_REF) {
		if (pos + 20 > k->datn)
			return 0;
		pos += 20;
	} else if (t != OB_COMMIT && t != OB_TREE && t != OB_BLOB &&
		   t != OB_TAG) {
		lg(HIBR_LDBG, "pack: unknown entry type %d", t);
		return 0;
	}
	if (t != OB_OFS && t != OB_REF) {
		if (!inf_zlib(p + pos, k->datn - pos, sz + 1, out, 0))
			return 0;
		if (out->n != sz) {
			s_free(out);
			return 0;
		}
		*type = t;
		return 1;
	}
	if (t == OB_OFS) {
		if (!ob_entry(g, k, base, depth + 1, &bt, &bo))
			return 0;
	} else {
		if (!ob_get(g, p + pos - 20, &bt, &bo))
			return 0;
	}
	{
		str ds;
		int ok;
		if (!inf_zlib(p + pos, k->datn - pos, sz + 1, &ds, 0)) {
			s_free(&bo);
			return 0;
		}
		ok = ob_delta(&bo, (const unsigned char *)ds.p, ds.n, out);
		s_free(&ds);
		s_free(&bo);
		if (!ok)
			return 0;
		*type = bt;
		return 1;
	}
}

/* Read a loose object from one object directory. */
int ob_loose(const char *dir, size_t max, const unsigned char *sha,
	     int *type, str *out)
{
	char hex[41];
	str path;
	unsigned char *raw;
	size_t n, i;
	str body;
	char *sp;

	ob_unhex(sha, hex);
	s_init(&path);
	s_cat(&path, dir);
	s_cat(&path, "/");
	s_add(&path, hex, 2);
	s_ch(&path, '/');
	s_cat(&path, hex + 2);
	raw = ob_map(path.p, &n);
	s_free(&path);
	if (!raw)
		return 0;
	if (!inf_zlib(raw, n, max, &body, 0)) {
		munmap(raw, n);
		return 0;
	}
	munmap(raw, n);
	sp = memchr(body.p, 0, body.n);
	if (!sp) {
		s_free(&body);
		return 0;
	}
	*type = 0;
	if (!strncmp(body.p, "commit ", 7))
		*type = OB_COMMIT;
	else if (!strncmp(body.p, "tree ", 5))
		*type = OB_TREE;
	else if (!strncmp(body.p, "blob ", 5))
		*type = OB_BLOB;
	else if (!strncmp(body.p, "tag ", 4))
		*type = OB_TAG;
	if (!*type) {
		s_free(&body);
		return 0;
	}
	i = (size_t)(sp - body.p) + 1;
	s_init(out);
	s_add(out, body.p + i, body.n - i);
	s_free(&body);
	return 1;
}

/* Read any object by name, loose or packed. */
int ob_get(grepo *g, const unsigned char *sha, int *type, str *out)
{
	size_t i, off;

	if (!g->odirs.n)
		ob_dirs(g);
	for (i = 0; i < g->odirs.n; i++)
		if (ob_loose((char *)g->odirs.p[i], g->omax, sha, type, out))
			return 1;
	ob_packs(g);
	for (i = 0; i < g->packs.n; i++) {
		struct pack *k = (struct pack *)g->packs.p[i];
		if (ob_idxfind(k, sha, &off))
			return ob_entry(g, k, off, 0, type, out);
	}
	return 0;
}

/* Release the object store. */
void ob_free(grepo *g)
{
	size_t i;

	for (i = 0; i < g->packs.n; i++)
		ob_drop((struct pack *)g->packs.p[i]);
	v_free(&g->packs);
	for (i = 0; i < g->odirs.n; i++)
		free(g->odirs.p[i]);
	v_free(&g->odirs);
}
