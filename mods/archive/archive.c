/* archive -- an archive as a folder (Gitea #102, #475, #488).
 *
 * What archivemount does with FUSE, without a kernel mount: the shell
 * already has the one mechanism this needs, since a module can register a
 * protocol, so `/dev/archive/<name>/path/in/archive` works anywhere a filename
 * does -- `read`, a redirection, any builtin that takes a path.
 *
 *   archive open NAME FILE      read the archive's index and keep it
 *   archive close NAME          forget it
 *   archive list                the archives open, name TAB file TAB members
 *   archive ls NAME [PATH]      what a folder holds, a line each, as dav ls does
 *   archive stat NAME PATH      one member's own line
 *   archive cat NAME PATH       its bytes
 *   archive extract NAME DEST [PATH]   write it into a folder, every name checked first
 *   archive create [-z] OUT [-C DIR] PATH...   write a new tar
 *
 * .tar and .tar.gz, zip, and ISO 9660 disc images directly, as raw sectors
 * or through a CUE sheet, each known by its bytes. A compressed tar is
 * expanded once, at open, into a file of its own that nothing else can see;
 * a zip member is inflated when it is read; everything else is read where it
 * lies. Inflating is mods/inflate.c, shared with the prompt module.
 */
#include "hibr.h"
#include "../inflate.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* How much of an archive is read at a time: a chunk on the heap, or what a
   growing string takes in one go, never a buffer on the stack. */
#ifndef ARC_CH
#define ARC_CH 65536
#endif

/* The most an archive may inflate to, so a crafted .tar.gz cannot ask for
   the machine's memory. Raise it with ARCHIVE_MAX. */
#ifndef ARC_MAXGZ
#define ARC_MAXGZ (512ull * 1024 * 1024)
#endif

/* What an ISO 9660 volume descriptor says after its type byte. */
#ifndef ARC_CD
#define ARC_CD "CD001"
#endif

/* One member: where its bytes are in the (expanded) archive, and what the
   header said about it. kind is f a file, d a folder, l a link; hard a hard
   link ('1'), which extract does not make. A zip member is held compressed:
   meth is its method (0 stored, 8 deflate), csize what it takes in the
   archive, crc what its bytes must add up to. why, when set, is the reason
   its bytes cannot be read, said whenever they are asked for. */
typedef struct arcm arcm;
struct arcm {
	char *name;
	long long size, off, mtime, csize;
	unsigned mode, crc;
	char kind;
	int hard, meth;
	const char *why;
	char *link;
};

/* An archive that is open: the expanded bytes in a file, and the index. fd is
   the archive or the expanded copy of it, tmp whether it is ours to forget.
   A disc image starts base bytes into fd and, when secsz is set, is raw
   sectors of that size whose 2048 bytes of data start sechd into each. len
   is how long the archive is as it reads, max how far its members may add
   up past that (ARCHIVE_MAX), and cm the one member held inflated in cb. */
typedef struct arcf arcf;
struct arcf {
	char *nm, *path;
	int fd, tmp, secsz, sechd;
	long long base, len, max;
	arcm *cm;
	str cb;
	vec mem;
};

static vec arc_list;

/* An octal field, which tar writes space or NUL padded, and GNU sometimes
   writes as base 256 with the top bit set. */
long long arc_num(const char *p, size_t n)
{
	long long v = 0;
	size_t i = 0;

	if (n && (p[0] & 0x80)) {
		unsigned long long u = p[0] & 0x40 ? ~0ull : 0;

		for (i = 0; i < n; i++)
			u = (u << 8) | (unsigned char)p[i];
		return (long long)u;
	}
	while (i < n && (p[i] == ' ' || p[i] == '0'))
		i++;
	for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
		v = v * 8 + (p[i] - '0');
	return v;
}

/* Whether a block is all zeroes, which is how an archive ends. It cannot be
   told from its checksum: the sum reads the checksum field as spaces, so a
   zero block comes to 8 * 32 rather than 0 -- which the first version took
   for a corrupt header and refused every archive on. */
int arc_zero(const unsigned char *h)
{
	int i;

	for (i = 0; i < 512; i++)
		if (h[i])
			return 0;
	return 1;
}

/* A header's own checksum: the sum of its bytes with the checksum field
   itself read as spaces. */
unsigned arc_sum(const unsigned char *h)
{
	unsigned s = 0;
	int i;

	for (i = 0; i < 512; i++)
		s += i >= 148 && i < 156 ? ' ' : h[i];
	return s;
}

/* Free one archive. */
void arc_free(arcf *a)
{
	size_t i;

	for (i = 0; i < a->mem.n; i++) {
		arcm *m = a->mem.p[i];

		free(m->name);
		free(m->link);
		free(m);
	}
	v_free(&a->mem);
	s_free(&a->cb);
	if (a->fd >= 0)
		close(a->fd);
	free(a->nm);
	free(a->path);
	free(a);
}

/* The archive a name stands for. */
arcf *arc_find(const char *nm)
{
	size_t i;

	for (i = 0; i < arc_list.n; i++) {
		arcf *a = arc_list.p[i];

		if (!strcmp(a->nm, nm))
			return a;
	}
	return 0;
}

/* Read the whole of a file. */
int arc_slurp(const char *path, str *o)
{
	return hibr_slurp(path, o) == 0;
}

/* A path with the leading ./ and any trailing / taken off, so what a person
   types and what the archive holds meet in the middle. */
void arc_tidy(str *o, const char *p)
{
	size_t n;

	while (p[0] == '.' && p[1] == '/')
		p += 2;
	while (*p == '/')
		p++;
	s_cat(o, p);
	n = o->n;
	while (n > 1 && o->p[n - 1] == '/')
		n--;
	o->n = n;
	s_grow(o, 1);
	o->p[o->n] = 0;
}

/* Whether a member's name, as extract would write it, stays inside the folder: not empty, not absolute, and no empty, "." or ".." part. */
int arc_safe(const char *p)
{
	const char *q = p;

	if (!*p || *p == '/')
		return 0;
	while (*q) {
		const char *e = strchr(q, '/');
		size_t n = e ? (size_t)(e - q) : strlen(q);

		if (!n || (n == 1 && q[0] == '.') || (n == 2 && q[0] == '.' && q[1] == '.'))
			return 0;
		q += n;
		if (*q == '/')
			q++;
	}
	return 1;
}

/* Up to n bytes of the archive as it reads from off: where they lie, or for a raw disc image the 2048 bytes of data in each sector, skipping the sync and header around them. */
ssize_t arc_pread(arcf *a, void *buf, size_t n, long long off)
{
	size_t got = 0;

	if (off < 0)
		return -1;
	if (!a->secsz)
		return pread(a->fd, buf, n, (off_t)(a->base + off));
	while (got < n) {
		long long at = off + (long long)got;
		size_t in = (size_t)(at % 2048), k = 2048 - in;
		ssize_t r;

		if (k > n - got)
			k = n - got;
		r = pread(a->fd, (char *)buf + got, k,
			  (off_t)(a->base + at / 2048 * a->secsz + a->sechd + (long long)in));
		if (r <= 0)
			break;
		got += (size_t)r;
		if ((size_t)r < k)
			break;
	}
	return (ssize_t)got;
}

/* Whether a member's bytes can be read at all, saying why not after verb when they cannot. */
int arc_canread(arcm *m, const char *verb)
{
	if (!m->why)
		return 1;
	lg(HIBR_LERR, "%s: %s %s", verb, m->name, m->why);
	return 0;
}

/* Inflate a compressed member whole into the archive's one held member, checking its length and its CRC before a byte of it is handed out. */
int arc_inflate(arcf *a, arcm *m)
{
	str in, out;
	ssize_t k;
	int ok;

	s_free(&a->cb);
	a->cm = 0;
	if (m->csize < 0 || m->off < 0 || m->off > a->len || m->csize > a->len - m->off ||
	    m->size < 0 || m->size > a->len + a->max) {
		lg(HIBR_LERR, "archive: %s ends early in %s", m->name, a->path);
		return 0;
	}
	s_init(&in);
	s_grow(&in, (size_t)m->csize);
	k = arc_pread(a, in.p, (size_t)m->csize, m->off);
	if (k != (ssize_t)m->csize) {
		lg(HIBR_LERR, "archive: %s ends early in %s", m->name, a->path);
		s_free(&in);
		return 0;
	}
	s_init(&out);
	ok = inf_raw((const unsigned char *)in.p, (size_t)m->csize, (size_t)m->size, &out, 0);
	s_free(&in);
	if (!ok || (long long)out.n != m->size ||
	    df_crc32((const unsigned char *)(out.p ? out.p : ""), out.n) != m->crc) {
		lg(HIBR_LERR, "archive: %s in %s is damaged: it does not inflate to what its header says",
		   m->name, a->path);
		s_free(&out);
		return 0;
	}
	a->cb = out;
	a->cm = m;
	return 1;
}

/* Up to want bytes of a member from pos within it: read where they lie, or out of the member inflated whole. -1 when they cannot be had. */
ssize_t arc_mread(arcf *a, arcm *m, char *buf, size_t want, long long pos)
{
	if (m->why)
		return -1;
	if (pos < 0 || pos >= m->size)
		return 0;
	if ((long long)want > m->size - pos)
		want = (size_t)(m->size - pos);
	if (!m->meth)
		return arc_pread(a, buf, want, m->off + pos);
	if (a->cm != m && !arc_inflate(a, m))
		return -1;
	memcpy(buf, a->cb.p + pos, want);
	return (ssize_t)want;
}

/* Walk the headers, filling the index. The archive is already expanded, so
   this is seeks and 512 byte reads. GNU's long name ('L') and pax's
   extended header ('x') both carry a name too long for the header's own
   hundred bytes, and both are followed by the member they describe. */
int arc_index(arcf *a)
{
	/* Sized by the format: a tar header is one 512-byte block. */
	unsigned char h[512];
	long long off = 0;
	str pend;
	int zeroes = 0, ended = 0;

	s_init(&pend);
	for (;;) {
		long long size;
		unsigned sum;
		arcm *m;
		char kind;
		str nm;

		if (pread(a->fd, h, 512, (off_t)off) != 512)
			break;
		off += 512;
		if (arc_zero(h)) {
			/* two zero blocks end an archive; one may be padding */
			if (++zeroes >= 2) {
				ended = 1;
				break;
			}
			continue;
		}
		zeroes = 0;
		sum = arc_sum(h);
		if (sum != (unsigned)arc_num((const char *)h + 148, 8)) {
			lg(HIBR_LERR, "archive: %s: a header does not add up at %lld",
			   a->path, off - 512);
			s_free(&pend);
			return 0;
		}
		size = arc_num((const char *)h + 124, 12);
		kind = (char)h[156];
		if (kind == 'L' || kind == 'K' || kind == 'x' || kind == 'g') {
			str t;

			/* the name of the member that follows, or a pax header
			   whose "path=" field is the same thing */
			s_init(&t);
			if (size > 0 && size < (1 << 20)) {
				char *b = xm((size_t)size + 1);

				if (pread(a->fd, b, (size_t)size, (off_t)off) == size) {
					b[size] = 0;
					if (kind == 'x' || kind == 'g') {
						char *q = strstr(b, " path=");

						if (q) {
							char *e = strchr(q + 6, '\n');

							if (e)
								*e = 0;
							s_cat(&t, q + 6);
						}
					} else if (kind == 'L') {
						s_cat(&t, b);
					}
				}
				free(b);
			}
			if (t.n && kind != 'g') {
				pend.n = 0;
				s_add(&pend, t.p, t.n);
			}
			s_free(&t);
			off += (size + 511) / 512 * 512;
			continue;
		}
		s_init(&nm);
		if (pend.n) {
			arc_tidy(&nm, pend.p);
			pend.n = 0;
		} else {
			str raw;

			s_init(&raw);
			if (h[345]) {	/* a prefix, which ustar splits a long name over */
				s_add(&raw, (const char *)h + 345,
				      strnlen((const char *)h + 345, 155));
				s_ch(&raw, '/');
			}
			s_add(&raw, (const char *)h, strnlen((const char *)h, 100));
			arc_tidy(&nm, raw.p ? raw.p : "");
			s_free(&raw);
		}
		m = xm(sizeof *m);
		memset(m, 0, sizeof *m);
		m->name = xs(nm.p ? nm.p : "");
		s_free(&nm);
		m->size = kind == '5' ? 0 : size;
		m->off = off;
		m->mode = (unsigned)arc_num((const char *)h + 100, 8);
		m->mtime = arc_num((const char *)h + 136, 12);
		m->kind = kind == '5' ? 'd' : kind == '2' || kind == '1' ? 'l' : 'f';
		m->hard = kind == '1';
		if (m->kind == 'l') {
			size_t ln = strnlen((const char *)h + 157, 100);

			m->link = xm(ln + 1);
			memcpy(m->link, h + 157, ln);
			m->link[ln] = 0;
		}
		v_add(&a->mem, m);
		off += (size + 511) / 512 * 512;
	}
	s_free(&pend);
	/* Nothing read and no proper end: this is not an archive at all. A tar
	   with no members is 1024 bytes of zeroes and does end properly, so the
	   two are told apart rather than both passing -- a 40 byte text file
	   used to "open" with no members and no complaint. */
	if (!a->mem.n && !ended) {
		lg(HIBR_LERR, "archive: %s: not a tar archive", a->path);
		return 0;
	}
	if (!ended)
		lg(HIBR_LDBG, "archive: %s: ends early, %zu members read",
		   a->path, a->mem.n);
	lg(HIBR_LDBG, "archive: %s: %zu members", a->path, a->mem.n);
	return 1;
}

/* An anonymous file holding what the archive expands to: made, written,
   unlinked at once, so nothing else can see it and it goes when the archive
   is closed. */
int arc_tmpfd(const char *data, size_t n)
{
	char tmpl[] = "/tmp/hibr-arc-XXXXXX";
	const char *d = getenv("TMPDIR");
	str p;
	int fd;

	s_init(&p);
	s_cat(&p, d && *d ? d : "/tmp");
	s_cat(&p, "/hibr-arc-XXXXXX");
	(void)tmpl;
	fd = mkstemp(p.p);
	if (fd < 0) {
		s_free(&p);
		return -1;
	}
	unlink(p.p);
	s_free(&p);
	while (n) {
		ssize_t k = write(fd, data, n);

		if (k <= 0) {
			close(fd);
			return -1;
		}
		data += k;
		n -= (size_t)k;
	}
	return fd;
}

/* The characters of DOS code page 437 from 0x80 up, which a zip name not marked UTF-8 is written in. Sized by the encoding: 128 code points. */
static const unsigned short arc_cp437[128] = {
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
	0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
	0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
	0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
	0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
	0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
	0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
	0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

/* A little-endian number of two bytes. */
unsigned arc_u16(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* A little-endian number of four bytes. */
unsigned arc_u32(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* A little-endian number of eight bytes. */
unsigned long long arc_u64(const unsigned char *p)
{
	return (unsigned long long)arc_u32(p) | (unsigned long long)arc_u32(p + 4) << 32;
}

/* Count a member's size toward what the whole archive may expand to, its own length and ARCHIVE_MAX past it; 0 once that is passed. */
int arc_budget(arcf *a, long long *total, unsigned long long size)
{
	unsigned long long cap = (unsigned long long)a->len + (unsigned long long)a->max;

	if (size > cap || (unsigned long long)*total + size > cap) {
		lg(HIBR_LERR, "archive open: %s: its members add up to more than ARCHIVE_MAX past its own size", a->path);
		return 0;
	}
	*total += (long long)size;
	return 1;
}

/* A zip member's name as UTF-8: as written when it says it is UTF-8 or already reads as UTF-8, else from code page 437; a name made on DOS or Windows has its backslashes taken for folders. */
void arc_zipname(str *o, const unsigned char *p, size_t n, int utf8, int dos)
{
	size_t i;
	unsigned cp;

	for (i = 0; i < n && !utf8; i++) {
		if (p[i] < 0x80)
			continue;
		if (u8dec((const char *)p + i, n - i, &cp) == 1)
			break;
		i += (size_t)u8len(p[i]) - 1;
	}
	if (i >= n)
		utf8 = 1;
	for (i = 0; i < n && p[i]; i++) {
		if (p[i] >= 0x80 && !utf8)
			u8put(o, arc_cp437[p[i] - 0x80]);
		else
			s_ch(o, p[i] == '\\' && dos ? '/' : p[i]);
	}
}

/* A DOS date and time, which a zip keeps in local time with no zone, as seconds since the epoch. */
long long arc_dostime(unsigned d, unsigned t)
{
	struct tm tm;

	memset(&tm, 0, sizeof tm);
	tm.tm_year = (int)(d >> 9) + 80;
	tm.tm_mon = (int)((d >> 5) & 15) - 1;
	tm.tm_mday = (int)(d & 31);
	tm.tm_hour = (int)(t >> 11);
	tm.tm_min = (int)((t >> 5) & 63);
	tm.tm_sec = (int)(t & 31) * 2;
	tm.tm_isdst = -1;
	if (tm.tm_mon < 0 || tm.tm_mon > 11 || !tm.tm_mday)
		return 0;
	return (long long)mktime(&tm);
}

/* One central directory entry of n bytes from p: a member with its name, sizes, method, time and kind, and where its local header says its bytes start. 0 when the entry or its local header is damaged. */
int arc_zipent(arcf *a, const unsigned char *p, long long *total)
{
	unsigned host = arc_u16(p + 4) >> 8, fl = arc_u16(p + 8), meth = arc_u16(p + 10);
	unsigned nl = arc_u16(p + 28), xl = arc_u16(p + 30), ea = arc_u32(p + 38);
	unsigned long long cs = arc_u32(p + 20), us = arc_u32(p + 24), lho = arc_u32(p + 42);
	const unsigned char *x = p + 46 + nl, *uname = 0;
	size_t un = 0, i;
	long long ut = -1;
	/* Sized by the format: a zip's local header is 30 bytes before its name. */
	unsigned char lh[30];
	int dos = host != 3 && host != 19, dir;
	arcm *m;
	str nm, raw;

	for (i = 0; i + 4 <= xl; i += 4 + arc_u16(x + i + 2)) {
		unsigned id = arc_u16(x + i), sz = arc_u16(x + i + 2);
		const unsigned char *d = x + i + 4, *e = d + sz;

		if (i + 4 + sz > xl)
			break;
		if (id == 0x0001) {
			if (us == 0xffffffffu && d + 8 <= e) {
				us = arc_u64(d);
				d += 8;
			}
			if (cs == 0xffffffffu && d + 8 <= e) {
				cs = arc_u64(d);
				d += 8;
			}
			if (lho == 0xffffffffu && d + 8 <= e)
				lho = arc_u64(d);
		} else if (id == 0x5455 && sz >= 5 && (d[0] & 1)) {
			ut = (long long)(int)arc_u32(d + 1);
		} else if (id == 0x7075 && sz >= 5 && d[0] == 1 &&
			   arc_u32(d + 1) == df_crc32(p + 46, nl)) {
			uname = d + 5;
			un = sz - 5;
		}
	}
	s_init(&raw);
	if (uname)
		arc_zipname(&raw, uname, un, 1, 0);
	else
		arc_zipname(&raw, p + 46, nl, (fl & 0x800) != 0, dos);
	dir = (raw.n && raw.p[raw.n - 1] == '/') ||
	      (!dos && ((ea >> 16) & 0170000) == 0040000) || (dos && (ea & 0x10));
	if (a->len < 30 || lho > (unsigned long long)(a->len - 30) ||
	    pread(a->fd, lh, 30, (off_t)lho) != 30 || arc_u32(lh) != 0x04034b50u ||
	    (meth == 0 && cs != us && !dir && !(fl & 1))) {
		lg(HIBR_LERR, "archive open: %s: the zip's entry for %s is damaged", a->path,
		   raw.p ? raw.p : "");
		s_free(&raw);
		return 0;
	}
	if (!dir && !arc_budget(a, total, us)) {
		s_free(&raw);
		return 0;
	}
	s_init(&nm);
	arc_tidy(&nm, raw.p ? raw.p : "");
	s_free(&raw);
	m = xm(sizeof *m);
	memset(m, 0, sizeof *m);
	m->name = xs(nm.p ? nm.p : "");
	s_free(&nm);
	m->off = (long long)lho + 30 + arc_u16(lh + 26) + arc_u16(lh + 28);
	m->size = dir ? 0 : (long long)us;
	m->csize = dir ? 0 : (long long)cs;
	m->meth = dir ? 0 : (int)meth;
	m->crc = arc_u32(p + 16);
	m->mtime = ut >= 0 ? ut : arc_dostime(arc_u16(p + 14), arc_u16(p + 12));
	m->mode = !dos && (ea >> 16) ? (ea >> 16) & 07777 : dir ? 0755 : (ea & 1) ? 0444 : 0644;
	m->kind = dir ? 'd' : !dos && ((ea >> 16) & 0170000) == 0120000 ? 'l' : 'f';
	if (!dir && (fl & 1))
		m->why = "is encrypted, which this does not read";
	else if (!dir && meth != 0 && meth != 8) {
		lg(HIBR_LDBG, "archive: %s in %s is compressed with method %u", m->name, a->path, meth);
		m->why = "is compressed in a way this does not read";
	}
	v_add(&a->mem, m);
	if (m->kind == 'l' && !m->why && m->size <= 4096) {
		char *t = xm((size_t)m->size + 1);

		if (arc_mread(a, m, t, (size_t)m->size, 0) == (ssize_t)m->size) {
			t[m->size] = 0;
			m->link = t;
		} else {
			free(t);
		}
	}
	return 1;
}

/* Read a zip's central directory, which is its index, from the end of central directory record (or its zip64 form) at the archive's end. */
int arc_zip(arcf *a)
{
	long long tail = a->len < 65557 ? a->len : 65557, i, eo = -1, ep, total = 0;
	unsigned long long n, cdsz, cdoff, k, p;
	unsigned char *t, *cd;
	int ok = 1;

	if (tail < 22) {
		lg(HIBR_LERR, "archive open: %s: a zip too short to hold its own index", a->path);
		return 0;
	}
	t = xm((size_t)tail);
	if (pread(a->fd, t, (size_t)tail, (off_t)(a->len - tail)) != tail) {
		free(t);
		lg(HIBR_LERR, "archive open: %s: cannot be read", a->path);
		return 0;
	}
	for (i = tail - 22; i >= 0 && eo < 0; i--)
		if (arc_u32(t + i) == 0x06054b50u && i + 22 + arc_u16(t + i + 20) <= tail)
			eo = i;
	if (eo < 0 || arc_u16(t + eo + 4) || arc_u16(t + eo + 6)) {
		lg(HIBR_LERR, "archive open: %s: %s", a->path, eo < 0 ?
		   "a zip with no end to its central directory" : "a zip spread over several files, which this does not read");
		free(t);
		return 0;
	}
	ep = a->len - tail + eo;
	n = arc_u16(t + eo + 10);
	cdsz = arc_u32(t + eo + 12);
	cdoff = arc_u32(t + eo + 16);
	if (eo >= 20 && arc_u32(t + eo - 20) == 0x07064b50u) {
		unsigned long long zo = arc_u64(t + eo - 12);
		unsigned char *z = xm(56);

		if (zo > (unsigned long long)ep || (unsigned long long)ep - zo < 56 ||
		    pread(a->fd, z, 56, (off_t)zo) != 56 || arc_u32(z) != 0x06064b50u) {
			ok = 0;
		} else {
			n = arc_u64(z + 32);
			cdsz = arc_u64(z + 40);
			cdoff = arc_u64(z + 48);
			ep = (long long)zo;
		}
		free(z);
	}
	free(t);
	if (!ok || cdoff > (unsigned long long)ep || cdsz > (unsigned long long)ep - cdoff) {
		lg(HIBR_LERR, "archive open: %s: the zip's central directory is not where it says", a->path);
		return 0;
	}
	cd = xm((size_t)cdsz + 1);
	if (pread(a->fd, cd, (size_t)cdsz, (off_t)cdoff) != (ssize_t)cdsz)
		ok = 0;
	for (k = 0, p = 0; ok && k < n; k++) {
		unsigned long long l;

		if (cdsz - p < 46 || arc_u32(cd + p) != 0x02014b50u) {
			ok = 0;
			break;
		}
		l = 46ull + arc_u16(cd + p + 28) + arc_u16(cd + p + 30) + arc_u16(cd + p + 32);
		if (cdsz - p < l) {
			ok = 0;
			break;
		}
		if (!arc_zipent(a, cd + p, &total)) {
			free(cd);
			return 0;
		}
		p += l;
	}
	free(cd);
	if (!ok) {
		lg(HIBR_LERR, "archive open: %s: the zip's central directory is damaged", a->path);
		return 0;
	}
	lg(HIBR_LDBG, "archive: %s: a zip of %zu members", a->path, a->mem.n);
	return 1;
}

/* A run of n decimal digits as a number, or -1 when one of them is not a digit. */
int arc_dig(const unsigned char *p, int n)
{
	int v = 0, i;

	for (i = 0; i < n; i++) {
		if (p[i] < '0' || p[i] > '9')
			return -1;
		v = v * 10 + (p[i] - '0');
	}
	return v;
}

/* A broken-down time and an offset from UTC in quarter hours, as seconds since the epoch; 0 for a time that cannot be one. */
long long arc_utc(int y, int mo, int d, int h, int mi, int s, int q)
{
	struct tm tm;

	if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
	    mi < 0 || mi > 59 || s < 0 || s > 60)
		return 0;
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	return (long long)timegm(&tm) - (long long)q * 900;
}

/* A disc image's seven-byte time, years since 1900 to seconds and its own offset from UTC, as seconds since the epoch. */
long long arc_isotime(const unsigned char *b)
{
	return arc_utc(b[0] + 1900, b[1], b[2], b[3], b[4], b[5], (signed char)b[6]);
}

/* The seventeen-byte form of that time: digits, hundredths, then the same offset. */
long long arc_isotime17(const unsigned char *b)
{
	return arc_utc(arc_dig(b, 4), arc_dig(b + 4, 2), arc_dig(b + 6, 2), arc_dig(b + 8, 2),
		       arc_dig(b + 10, 2), arc_dig(b + 12, 2), (signed char)b[16]);
}

/* What a walk of a disc image's folders carries: the archive and its block size, whether Rock Ridge or Joliet names are read and how much of each system use area to skip, the folders walked so far, how many directory bytes are left to read, the file whose pieces are still arriving, and what the members add up to. */
typedef struct arci arci;
struct arci {
	arcf *a;
	long long bs, left, total;
	int rr, jol, skp;
	str seen;
	arcm *part;
};

/* What one record's Rock Ridge entries said: its name, a link's target and whether the next component needs a slash, its mode, its time, whether it is "." or "..", a folder relocated elsewhere (RE), or one standing for a folder elsewhere (CL). */
typedef struct arcr arcr;
struct arcr {
	str nm, sl;
	int slsep, px, nmgot, dot, re;
	unsigned mode;
	long long mtime, cl;
};

/* Add an SL entry's components to the link target being built: a root, ".", ".." or a name, with a slash between them unless the one before said it continues. */
void arc_slpart(arcr *r, const unsigned char *p, size_t n)
{
	size_t i = 0;

	while (i + 2 <= n && i + 2 + p[i + 1] <= n) {
		unsigned cf = p[i], cl = p[i + 1];

		if (cf & 8) {
			if (!r->sl.n)
				s_ch(&r->sl, '/');
			r->slsep = 0;
		} else {
			if (r->slsep)
				s_ch(&r->sl, '/');
			if (cf & 2)
				s_ch(&r->sl, '.');
			else if (cf & 4)
				s_cat(&r->sl, "..");
			else
				s_add(&r->sl, (const char *)p + i + 2, cl);
			r->slsep = !(cf & 1);
		}
		i += 2 + cl;
	}
}

/* Read one system use area's entries into r, following a continuation area (CE) at most hops more times. */
void arc_susp(arci *w, const unsigned char *p, size_t n, arcr *r, int hops)
{
	size_t i = 0;
	unsigned long long cb = 0, co = 0, cn = 0;

	while (i + 4 <= n && p[i + 2] >= 4 && i + p[i + 2] <= n) {
		const unsigned char *e = p + i;
		size_t l = e[2];

		if (e[0] == 'S' && e[1] == 'T')
			break;
		if (e[0] == 'N' && e[1] == 'M' && l >= 5) {
			if (e[4] & 6)
				r->dot = 1;
			s_add(&r->nm, (const char *)e + 5, l - 5);
			r->nmgot = 1;
		} else if (e[0] == 'P' && e[1] == 'X' && l >= 12) {
			r->mode = arc_u32(e + 4);
			r->px = 1;
		} else if (e[0] == 'S' && e[1] == 'L' && l >= 5) {
			arc_slpart(r, e + 5, l - 5);
		} else if (e[0] == 'T' && e[1] == 'F' && l >= 5) {
			size_t sz = e[4] & 0x80 ? 17 : 7, at = 5 + (e[4] & 1 ? sz : 0);

			if ((e[4] & 2) && at + sz <= l)
				r->mtime = sz == 17 ? arc_isotime17(e + at) : arc_isotime(e + at);
		} else if (e[0] == 'R' && e[1] == 'E') {
			r->re = 1;
		} else if (e[0] == 'C' && e[1] == 'L' && l >= 12) {
			r->cl = arc_u32(e + 4);
		} else if (e[0] == 'C' && e[1] == 'E' && l >= 28) {
			cb = arc_u32(e + 4);
			co = arc_u32(e + 12);
			cn = arc_u32(e + 20);
		}
		i += l;
	}
	if (cn && hops > 0 && cn <= 65536) {
		unsigned char *c = xm((size_t)cn);

		if (arc_pread(w->a, c, (size_t)cn, (long long)(cb * (unsigned long long)w->bs + co)) == (ssize_t)cn)
			arc_susp(w, c, (size_t)cn, r, hops - 1);
		free(c);
	}
}

/* Take a disc image name's version (";1") off, and for a plain ISO 9660 name the dot it keeps when it has no extension. */
void arc_isover(str *o, int plain)
{
	size_t i = o->n;

	while (i && o->p[i - 1] >= '0' && o->p[i - 1] <= '9')
		i--;
	if (i && o->p[i - 1] == ';')
		o->n = i - 1;
	if (plain && o->n > 1 && o->p[o->n - 1] == '.')
		o->n--;
	if (o->p)
		o->p[o->n] = 0;
}

/* A Joliet name, UCS-2 big-endian with UTF-16's pairs, as UTF-8. */
void arc_ucs2(str *o, const unsigned char *p, size_t n)
{
	size_t i;

	for (i = 0; i + 1 < n; i += 2) {
		unsigned c = (unsigned)p[i] << 8 | p[i + 1];

		if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
			unsigned d = (unsigned)p[i + 2] << 8 | p[i + 3];

			if (d >= 0xDC00 && d < 0xE000) {
				c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
				i += 2;
			}
		}
		if (!c)
			break;
		u8put(o, c);
	}
}

/* How long the folder at block ext says it is, from its own "." record. */
long long arc_isodot(arci *w, long long ext)
{
	unsigned char *b = xm(2048);
	long long n = 0;

	if (arc_pread(w->a, b, 2048, ext * w->bs) == 2048 && b[0] >= 34)
		n = arc_u32(b + 10);
	free(b);
	return n;
}

int arc_isodir(arci *w, long long ext, long long size, const char *prefix, int depth);

/* One directory record of len bytes: a member named by the walk's own reading of it, and for a folder the walk below it. 0 when what is below is damaged. */
int arc_isorec(arci *w, const unsigned char *r, unsigned len, const char *prefix, int depth)
{
	unsigned lfi = r[32], fl = r[25];
	long long ext = (long long)arc_u32(r + 2) + r[1], dl = arc_u32(r + 10);
	size_t su = 33 + lfi + !(lfi & 1);
	int dir = (fl & 2) != 0, ok = 1, kind;
	arcr rr;
	str nm, nm2, full;
	arcm *m;

	if ((lfi == 1 && r[33] <= 1) || (fl & 4))
		return 1;
	memset(&rr, 0, sizeof rr);
	rr.cl = -1;
	if (w->rr && su + (size_t)w->skp < len)
		arc_susp(w, r + su + w->skp, len - su - (size_t)w->skp, &rr, 16);
	s_init(&nm);
	s_init(&nm2);
	s_init(&full);
	if (rr.re || rr.dot)
		goto out;
	if (rr.nmgot)
		s_add(&nm, rr.nm.p ? rr.nm.p : "", rr.nm.n);
	else if (w->jol) {
		arc_ucs2(&nm, r + 33, lfi);
		arc_isover(&nm, 0);
	} else {
		s_add(&nm, (const char *)r + 33, lfi);
		arc_isover(&nm, 1);
	}
	if (!nm.p || !nm.p[0])
		goto out;
	if (*prefix) {
		s_cat(&nm2, prefix);
		s_ch(&nm2, '/');
	}
	s_cat(&nm2, nm.p);
	arc_tidy(&full, nm2.p);
	if (w->part && !strcmp(w->part->name, full.p ? full.p : "")) {
		if (ext * w->bs != w->part->off + w->part->size)
			w->part->why = "is in pieces that are not together, which this does not read";
		w->part->size += dl;
		ok = arc_budget(w->a, &w->total, (unsigned long long)dl);
		if (!(fl & 0x80))
			w->part = 0;
		goto out;
	}
	w->part = 0;
	if (rr.cl >= 0) {
		dir = 1;
		ext = rr.cl;
		dl = arc_isodot(w, ext);
	}
	kind = dir ? 'd' : !rr.px || !(rr.mode & 0170000) || (rr.mode & 0170000) == 0100000 ? 'f' :
	       (rr.mode & 0170000) == 0120000 ? 'l' : 0;
	if (!kind) {
		lg(HIBR_LDBG, "archive: %s in %s is neither a file, a folder nor a link, and is left out",
		   full.p ? full.p : "", w->a->path);
		goto out;
	}
	if (kind == 'f' && !arc_budget(w->a, &w->total, (unsigned long long)dl)) {
		ok = 0;
		goto out;
	}
	m = xm(sizeof *m);
	memset(m, 0, sizeof *m);
	m->name = xs(full.p ? full.p : "");
	m->kind = (char)kind;
	m->off = ext * w->bs;
	m->size = kind == 'f' ? dl : 0;
	m->mtime = rr.mtime ? rr.mtime : arc_isotime(r + 18);
	m->mode = rr.px ? rr.mode & 07777 : dir ? 0755 : 0644;
	if (kind == 'l')
		m->link = xs(rr.sl.p ? rr.sl.p : "");
	if (kind == 'f' && (r[26] || r[27]))
		m->why = "is interleaved, which this does not read";
	v_add(&w->a->mem, m);
	if (kind == 'f' && (fl & 0x80))
		w->part = m;
	if (dir) {
		ok = arc_isodir(w, ext, dl, m->name, depth + 1);
		w->part = 0;
	}
out:
	s_free(&rr.nm);
	s_free(&rr.sl);
	s_free(&nm);
	s_free(&nm2);
	s_free(&full);
	return ok;
}

/* Walk one folder of a disc image, at block ext and size bytes long, adding what it holds under prefix and walking each folder in it. A folder seen before is not walked again, and every folder together may not read more than the image holds. 0 when a record is damaged. */
int arc_isodir(arci *w, long long ext, long long size, const char *prefix, int depth)
{
	unsigned char *b;
	long long pos, at = ext * w->bs, v;
	size_t k;
	int ok = 1;

	if (depth > 256) {
		lg(HIBR_LERR, "archive open: %s: its folders nest deeper than 256", w->a->path);
		return 0;
	}
	for (k = 0; k + sizeof v <= w->seen.n; k += sizeof v) {
		memcpy(&v, w->seen.p + k, sizeof v);
		if (v == ext) {
			lg(HIBR_LDBG, "archive: %s: the folder at block %lld is named twice, walked once",
			   w->a->path, ext);
			return 1;
		}
	}
	s_add(&w->seen, (const char *)&ext, sizeof ext);
	b = xm(2048);
	for (pos = 0; ok && pos < size; pos += 2048) {
		size_t i = 0;

		if ((w->left -= 2048) < 0) {
			lg(HIBR_LERR, "archive open: %s: its folders overlap", w->a->path);
			ok = 0;
			break;
		}
		if (arc_pread(w->a, b, 2048, at + pos) != 2048 || !b[0])
			break;
		while (ok && i < 2048 && b[i]) {
			unsigned len = b[i];

			if (len < 34 || i + len > 2048 || 33u + b[i + 32] > len) {
				lg(HIBR_LERR, "archive open: %s: a directory record is damaged at block %lld",
				   w->a->path, ext);
				ok = 0;
				break;
			}
			ok = arc_isorec(w, b + i, len, prefix, depth);
			i += len;
		}
	}
	free(b);
	return ok;
}

/* Whether the archive as it reads has an ISO 9660 volume descriptor at sector 16. */
int arc_cd001(arcf *a)
{
	/* Sized by the format: a volume descriptor's type and its "CD001". */
	unsigned char b[6];

	return arc_pread(a, b, sizeof b, 32768) == (ssize_t)sizeof b && !memcmp(b + 1, ARC_CD, sizeof ARC_CD - 1);
}

/* Read a disc image's ISO 9660 file system: the volume descriptors from sector 16 to the terminator, then the folders from the root -- with Rock Ridge names where the primary tree has them, else Joliet's, else the plain ones. */
int arc_iso(arcf *a)
{
	unsigned char *b = xm(2048);
	long long i, pe = -1, ps = 0, je = -1, js = 0, bs = 0;
	arci w;
	int ok;

	for (i = 16; i < 16 + 64; i++) {
		if (arc_pread(a, b, 2048, i * 2048) != 2048 || memcmp(b + 1, ARC_CD, sizeof ARC_CD - 1) || b[0] == 255)
			break;
		if (b[0] == 1 && pe < 0) {
			pe = (long long)arc_u32(b + 156 + 2) + b[156 + 1];
			ps = arc_u32(b + 156 + 10);
			bs = arc_u16(b + 128);
		} else if (b[0] == 2 && je < 0 && b[88] == '%' && b[89] == '/' &&
			   (b[90] == '@' || b[90] == 'C' || b[90] == 'E')) {
			je = (long long)arc_u32(b + 156 + 2) + b[156 + 1];
			js = arc_u32(b + 156 + 10);
		}
	}
	if (pe < 0 || (bs != 512 && bs != 1024 && bs != 2048)) {
		lg(HIBR_LERR, "archive open: %s: %s", a->path, pe < 0 ?
		   "holds no ISO 9660 file system" : "its ISO 9660 block size is not one there can be");
		free(b);
		return 0;
	}
	memset(&w, 0, sizeof w);
	w.a = a;
	w.bs = bs;
	w.left = a->len + 65536;
	if (arc_pread(a, b, 2048, pe * bs) == 2048 && b[0] >= 34) {
		size_t su = 33 + b[32] + !(b[32] & 1);

		if (su + 7 <= b[0] && b[su] == 'S' && b[su + 1] == 'P' && b[su + 4] == 0xBE && b[su + 5] == 0xEF) {
			w.rr = 1;
			w.skp = b[su + 6];
		}
	}
	free(b);
	if (!w.rr && je >= 0) {
		w.jol = 1;
		pe = je;
		ps = js;
	}
	lg(HIBR_LDBG, "archive: %s: ISO 9660, %s names", a->path, w.rr ? "Rock Ridge" : w.jol ? "Joliet" : "plain");
	s_init(&w.seen);
	ok = arc_isodir(&w, pe, ps, "", 0);
	s_free(&w.seen);
	return ok;
}

/* Point the archive at a raw disc image: sectors of secsz bytes whose data starts hd bytes in, from base on. */
void arc_sectors(arcf *a, int secsz, int hd, long long base, long long size)
{
	a->base = base;
	a->secsz = secsz == 2048 ? 0 : secsz;
	a->sechd = hd;
	a->len = size > base ? (secsz == 2048 ? size - base : (size - base) / secsz * 2048) : 0;
}

/* Whether a block begins with a raw CD sector's sync pattern, and so which header its data follows: 16 bytes for mode 1, 24 for mode 2 form 1, 0 for neither. */
int arc_sync(const unsigned char *h, ssize_t n)
{
	int i;

	if (n < 16 || h[0] || h[11])
		return 0;
	for (i = 1; i < 11; i++)
		if (h[i] != 0xff)
			return 0;
	return h[15] == 1 ? 16 : h[15] == 2 ? 24 : 0;
}

/* Where the word starting at j ends: the first blank before end. */
size_t arc_wend(const char *t, size_t j, size_t end)
{
	while (j < end && t[j] != ' ' && t[j] != '\t')
		j++;
	return j;
}

/* Whether the n bytes at t are the word w, in any case. */
int arc_word(const char *t, size_t n, const char *w)
{
	return n == strlen(w) && !strncasecmp(t, w, n);
}

/* Whether a file's text is a CUE sheet: no NUL in it, and a FILE line and a TRACK line among its lines. */
int arc_iscue(const char *t, size_t n)
{
	size_t i = 0, w;
	int f = 0, tr = 0;

	if (memchr(t, 0, n))
		return 0;
	while (i < n) {
		const char *e = memchr(t + i, '\n', n - i);
		size_t j = i, end = e ? (size_t)(e - t) : n;

		while (j < end && (t[j] == ' ' || t[j] == '\t' || (unsigned char)t[j] >= 0x80))
			j++;
		w = arc_wend(t, j, end);
		if (w < end && arc_word(t + j, w - j, "FILE"))
			f = 1;
		if (w < end && arc_word(t + j, w - j, "TRACK"))
			tr = 1;
		i = end + 1;
	}
	return f && tr;
}

/* Open a file named in a CUE sheet, in the sheet's own folder, matching its case when no file has it exactly, as a sheet written on Windows often needs. -1 when it is not there. */
int arc_cuefile(const char *cue, const char *name)
{
	const char *sl = strrchr(cue, '/');
	str p;
	int fd;
	DIR *d;
	struct dirent *e;

	s_init(&p);
	if (sl)
		s_add(&p, cue, (size_t)(sl - cue) + 1);
	else
		s_cat(&p, "./");
	s_cat(&p, name);
	fd = open(p.p, O_RDONLY | O_CLOEXEC);
	if (fd < 0 && !strchr(name, '/')) {
		p.n -= strlen(name);
		p.p[p.n] = 0;
		d = opendir(p.p);
		while (d && fd < 0 && (e = readdir(d)))
			if (!strcasecmp(e->d_name, name)) {
				s_cat(&p, e->d_name);
				fd = open(p.p, O_RDONLY | O_CLOEXEC);
			}
		if (d)
			closedir(d);
	}
	s_free(&p);
	return fd;
}

/* Open the data track a CUE sheet names: the first track of MODE1/2048, MODE1/2352 or MODE2/2352, in the file the sheet names beside itself, from that track's INDEX 01. */
int arc_cue(arcf *a, const char *t, size_t n)
{
	str cur, data, bad;
	size_t i = 0;
	int in = 0, secsz = 0, hd = 0, fd;
	long long idx = -1;
	struct stat st;

	s_init(&cur);
	s_init(&data);
	s_init(&bad);
	while (i < n) {
		const char *e = memchr(t + i, '\n', n - i);
		size_t j = i, end = e ? (size_t)(e - t) : n, w;

		while (end > j && (t[end - 1] == '\r' || t[end - 1] == ' ' || t[end - 1] == '\t'))
			end--;
		while (j < end && (t[j] == ' ' || t[j] == '\t' || (unsigned char)t[j] >= 0x80))
			j++;
		w = arc_wend(t, j, end);
		if (arc_word(t + j, w - j, "FILE")) {
			size_t q = w, qe;

			while (q < end && (t[q] == ' ' || t[q] == '\t'))
				q++;
			if (q < end && t[q] == '"') {
				const char *c = memchr(t + q + 1, '"', end - q - 1);

				q++;
				qe = c ? (size_t)(c - t) : end;
			} else {
				for (qe = end; qe > q && t[qe - 1] != ' ' && t[qe - 1] != '\t'; qe--)
					;
				while (qe > q && (t[qe - 1] == ' ' || t[qe - 1] == '\t'))
					qe--;
				if (qe == q)
					qe = end;
			}
			cur.n = 0;
			s_add(&cur, t + q, qe - q);
		} else if (arc_word(t + j, w - j, "TRACK")) {
			size_t q = w, m;

			while (q < end && (t[q] == ' ' || t[q] == '\t' || (t[q] >= '0' && t[q] <= '9')))
				q++;
			m = end - q;
			if (data.p) {
				in = 0;
			} else if (arc_word(t + q, m, "MODE1/2048")) {
				secsz = 2048;
			} else if (arc_word(t + q, m, "MODE1/2352")) {
				secsz = 2352;
				hd = 16;
			} else if (arc_word(t + q, m, "MODE2/2352")) {
				secsz = 2352;
				hd = 24;
			} else if (!bad.n && !arc_word(t + q, m, "AUDIO")) {
				s_add(&bad, t + q, m);
			}
			if (!data.p && secsz) {
				s_add(&data, cur.p ? cur.p : "", cur.n);
				in = 1;
			}
		} else if (arc_word(t + j, w - j, "INDEX") && in && idx < 0) {
			size_t q = w;
			const unsigned char *u;

			while (q < end && (t[q] == ' ' || t[q] == '\t'))
				q++;
			if (end - q >= 11 && t[q] == '0' && t[q + 1] == '1' && t[q + 2] == ' ') {
				u = (const unsigned char *)t + end - 8;
				if (arc_dig(u, 2) >= 0 && u[2] == ':' && arc_dig(u + 3, 2) >= 0 && u[5] == ':' &&
				    arc_dig(u + 6, 2) >= 0)
					idx = ((long long)arc_dig(u, 2) * 60 + arc_dig(u + 3, 2)) * 75 + arc_dig(u + 6, 2);
			}
		}
		i = (e ? (size_t)(e - t) : n) + 1;
	}
	s_free(&cur);
	if (!data.p || !data.n || !arc_safe(data.p)) {
		if (!data.p)
			lg(HIBR_LERR, "archive open: %s: a CUE sheet with no data track this reads%s%s", a->path,
			   bad.n ? ": " : "", bad.n ? bad.p : "");
		else
			lg(HIBR_LERR, "archive open: %s: the CUE sheet names %s, which is not beside it", a->path, data.p);
		s_free(&data);
		s_free(&bad);
		return 0;
	}
	s_free(&bad);
	fd = arc_cuefile(a->path, data.p);
	if (fd < 0 || fstat(fd, &st) != 0) {
		lg(HIBR_LERR, "archive open: %s: the CUE sheet names %s, which cannot be read", a->path, data.p);
		if (fd >= 0)
			close(fd);
		s_free(&data);
		return 0;
	}
	s_free(&data);
	close(a->fd);
	a->fd = fd;
	arc_sectors(a, secsz, hd, (idx < 0 ? 0 : idx) * secsz, (long long)st.st_size);
	if (!arc_cd001(a)) {
		lg(HIBR_LERR, "archive open: %s: its data track holds no ISO 9660 file system", a->path);
		return 0;
	}
	return arc_iso(a);
}

/* Expand a gzip archive once, into a file of its own, bounded by ARCHIVE_MAX. */
int arc_gunzip(arcf *a)
{
	str raw, out;
	int ok;

	s_init(&raw);
	s_init(&out);
	if (!arc_slurp(a->path, &raw)) {
		lg(HIBR_LERR, "archive open: %s: cannot be read", a->path);
		s_free(&raw);
		return 0;
	}
	ok = inf_gzip((const unsigned char *)raw.p, raw.n, (size_t)a->max, &out, 0);
	s_free(&raw);
	if (!ok) {
		lg(HIBR_LERR, "archive open: %s: the gzip stream does not "
		   "expand (or is larger than ARCHIVE_MAX)", a->path);
		s_free(&out);
		return 0;
	}
	close(a->fd);
	a->fd = arc_tmpfd(out.p ? out.p : "", out.n);
	a->tmp = 1;
	a->len = (long long)out.n;
	lg(HIBR_LDBG, "archive: %s expanded to %zu bytes", a->path, out.n);
	s_free(&out);
	if (a->fd < 0) {
		lg(HIBR_LERR, "archive open: nowhere to expand %s to", a->path);
		return 0;
	}
	return 1;
}

/* Decide what an archive is from its bytes, never its name, and read its index: gzip (a tar inside), a zip, a tar, a raw disc image, an ISO 9660 image, a CUE sheet, and anything else as a tar, which says when it is not one. */
int arc_sniff(arcf *a)
{
	/* Sized by the format: a tar header is one 512-byte block. */
	unsigned char h[512];
	ssize_t k = pread(a->fd, h, sizeof h, 0);
	int hd;

	if (k >= 2 && h[0] == 0x1f && h[1] == 0x8b)
		return arc_gunzip(a) && arc_index(a);
	if (k >= 4 && h[0] == 'P' && h[1] == 'K' && ((h[2] == 3 && h[3] == 4) || (h[2] == 5 && h[3] == 6)))
		return arc_zip(a);
	if (k == 512 && !arc_zero(h) && arc_sum(h) == (unsigned)arc_num((const char *)h + 148, 8))
		return arc_index(a);
	if ((hd = arc_sync(h, k))) {
		arc_sectors(a, 2352, hd, 0, a->len);
		if (arc_cd001(a))
			return arc_iso(a);
		lg(HIBR_LERR, "archive open: %s: a raw disc image whose data holds no ISO 9660 file system", a->path);
		return 0;
	}
	if (arc_cd001(a))
		return arc_iso(a);
	if (a->len > 0 && a->len <= 65536) {
		str t;
		int ok = -1;

		s_init(&t);
		s_grow(&t, (size_t)a->len);
		if (pread(a->fd, t.p, (size_t)a->len, 0) == (ssize_t)a->len &&
		    arc_iscue(t.p, (size_t)a->len))
			ok = arc_cue(a, t.p, (size_t)a->len);
		s_free(&t);
		if (ok >= 0)
			return ok;
	}
	return arc_index(a);
}

/* Open an archive under a name. */
int arc_openarch(sh *s, const char *nm, const char *path)
{
	arcf *a;
	int fd;
	const char *v;
	struct stat st;

	if (arc_find(nm)) {
		lg(HIBR_LERR, "archive open: %s is open already", nm);
		return HIBR_FAIL;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) != 0) {
		lg(HIBR_LERR, "archive open: %s: cannot be read", path);
		if (fd >= 0)
			close(fd);
		return HIBR_FAIL;
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->nm = xs(nm);
	a->path = xs(path);
	a->fd = fd;
	a->len = (long long)st.st_size;
	a->max = (long long)ARC_MAXGZ;
	if ((v = hibr_get(s, "ARCHIVE_MAX")) && *v)
		a->max = (long long)strtoull(v, 0, 10);
	if (a->max < 0 || a->max > (1ll << 60))
		a->max = 1ll << 60;
	if (!arc_sniff(a)) {
		arc_free(a);
		return HIBR_FAIL;
	}
	v_add(&arc_list, a);
	return HIBR_OK;
}

/* One member by name. */
arcm *arc_member(arcf *a, const char *path)
{
	str want;
	size_t i;
	arcm *hit = 0;

	s_init(&want);
	arc_tidy(&want, path);
	for (i = 0; i < a->mem.n && !hit; i++) {
		arcm *m = a->mem.p[i];

		if (!strcmp(m->name, want.p ? want.p : ""))
			hit = m;
	}
	s_free(&want);
	return hit;
}

/* A member's own line: kind, size, mtime, name -- the shape dav ls has. */
void arc_line(str *o, const arcm *m, const char *nm)
{
	s_ch(o, m->kind);
	s_ch(o, '\t');
	s_num(o, (long)m->size);
	s_ch(o, '\t');
	s_num(o, (long)m->mtime);
	s_ch(o, '\t');
	s_cat(o, nm ? nm : m->name);
}

/* What a member is called inside a folder, at any depth: "a/b/c" is under
   "a" as "b/c" and under "a/b" as "c". 0 when it is not under it at all. */
const char *arc_under(const char *name, const char *dir, size_t dn)
{
	if (!dn)
		return *name ? name : 0;
	if (strncmp(name, dir, dn) || name[dn] != '/')
		return 0;
	return name[dn + 1] ? name + dn + 1 : 0;
}

/* Whether a path is a folder of the archive. Many archives -- anything
   Python's tarfile writes, and plenty of real ones -- hold no entry for a
   folder at all, only the files inside it, so a folder is one when anything
   is under it. The first version asked for a 'd' member and refused to list
   dir3 in an archive that plainly had dir3/file0003.txt in it. */
int arc_isdir(arcf *a, const char *path, size_t pn)
{
	size_t i;

	if (!pn)
		return 1;
	for (i = 0; i < a->mem.n; i++) {
		arcm *m = a->mem.p[i];

		if (m->kind == 'd' && !strcmp(m->name, path))
			return 1;
		if (arc_under(m->name, path, pn))
			return 1;
	}
	return 0;
}

/* archive ls NAME [PATH] */
int arc_ls(sh *s, arcf *a, const char *path)
{
	str dir, o;
	vec seen;
	size_t i, n = 0;

	s_init(&dir);
	s_init(&o);
	if (path)
		arc_tidy(&dir, path);
	if (dir.n && !arc_isdir(a, dir.p, dir.n)) {
		lg(HIBR_LERR, "archive ls: %s: no such folder in %s", dir.p, a->nm);
		s_free(&dir);
		s_free(&o);
		return HIBR_FAIL;
	}
	memset(&seen, 0, sizeof seen);
	for (i = 0; i < a->mem.n; i++) {
		arcm *m = a->mem.p[i], imp;
		const char *rest = arc_under(m->name, dir.p ? dir.p : "", dir.n);
		const char *slash, *leaf;
		str leafs;
		int dup = 0;
		size_t j;

		if (!rest)
			continue;
		slash = strchr(rest, '/');
		s_init(&leafs);
		if (slash) {
			/* a folder nothing in the archive declared: it is one
			   because something is inside it */
			s_add(&leafs, rest, (size_t)(slash - rest));
			memset(&imp, 0, sizeof imp);
			imp.kind = 'd';
			imp.mtime = m->mtime;
			imp.name = leafs.p;
			m = &imp;
		} else {
			s_cat(&leafs, rest);
		}
		leaf = leafs.p ? leafs.p : "";
		for (j = 0; j < seen.n; j++)
			if (!strcmp(seen.p[j], leaf)) {
				dup = 1;
				break;
			}
		if (dup) {
			s_free(&leafs);
			continue;
		}
		v_add(&seen, xs(leaf));
		if (s->bind) {
			char ks[2][32];
			char *kp[2];
			str v;

			snprintf(ks[0], sizeof ks[0], "%zu", n);
			kp[0] = ks[0];
			kp[1] = ks[1];
			s_init(&v);
			s_ch(&v, m->kind);
			snprintf(ks[1], sizeof ks[1], "kind");
			hibr_setp(s, "RET", kp, 2, v.p ? v.p : "");
			v.n = 0;
			s_num(&v, (long)m->size);
			snprintf(ks[1], sizeof ks[1], "size");
			hibr_setp(s, "RET", kp, 2, v.p ? v.p : "0");
			v.n = 0;
			s_num(&v, (long)m->mtime);
			snprintf(ks[1], sizeof ks[1], "mtime");
			hibr_setp(s, "RET", kp, 2, v.p ? v.p : "0");
			snprintf(ks[1], sizeof ks[1], "name");
			hibr_setp(s, "RET", kp, 2, leaf);
			snprintf(ks[1], sizeof ks[1], "path");
			hibr_setp(s, "RET", kp, 2, m->name);
			s_free(&v);
		} else {
			arc_line(&o, m, leaf);
			s_ch(&o, '\n');
		}
		s_free(&leafs);
		n++;
	}
	for (i = 0; i < seen.n; i++)
		free(seen.p[i]);
	v_free(&seen);
	if (!s->bind && o.n)
		fwrite(o.p, 1, o.n, stdout);
	if (s->bind && !n)
		hibr_retn(s, 0, 0);	/* nothing here is an empty list, not one entry */
	s_free(&dir);
	s_free(&o);
	return HIBR_OK;
}

/* The bytes of one member, to stdout or into the slot. */
int arc_cat(sh *s, arcf *a, const char *path)
{
	arcm *m = arc_member(a, path);
	long long left;
	char *buf;
	str o;

	if (!m) {
		str t;

		s_init(&t);
		arc_tidy(&t, path);
		if (arc_isdir(a, t.p ? t.p : "", t.n))
			lg(HIBR_LERR, "archive cat: %s is a folder", path);
		else
			lg(HIBR_LERR, "archive cat: %s: not in %s", path, a->nm);
		s_free(&t);
		return HIBR_FAIL;
	}
	if (m->kind == 'd') {
		lg(HIBR_LERR, "archive cat: %s is a folder", path);
		return HIBR_FAIL;
	}
	if (!arc_canread(m, "archive cat"))
		return HIBR_FAIL;
	s_init(&o);
	buf = xm(ARC_CH);
	left = m->size;
	while (left > 0) {
		size_t want = left > (long long)ARC_CH ? ARC_CH : (size_t)left;
		ssize_t k = arc_mread(a, m, buf, want, m->size - left);

		if (k <= 0)
			break;
		if (s->bind)
			s_add(&o, buf, (size_t)k);
		else
			fwrite(buf, 1, (size_t)k, stdout);
		left -= k;
	}
	free(buf);
	if (s->bind) {
		s_grow(&o, 1);
		o.p[o.n] = 0;
		hibr_ret(s, o.p ? o.p : "");
	}
	s_free(&o);
	return left == 0 ? HIBR_OK : HIBR_FAIL;
}

/* /dev/archive/NAME/path: a descriptor holding that member's bytes.

   The member is copied into a file of its own, which is unlinked at once, so
   it closes away when the caller is done. A descriptor on the archive itself
   would read straight past the member's end into the next header, and a pipe
   would need something to fill it: a copy is the honest answer, and the
   member's own size is what it costs. */
int arc_scheme(sh *s, const char *rest)
{
	const char *slash = strchr(rest, '/');
	str nm, data;
	arcf *a;
	arcm *m;
	long long left;
	int fd;

	(void)s;
	if (!slash) {
		lg(HIBR_LERR, "/dev/archive/ wants a name and a path inside it");
		return -1;
	}
	s_init(&nm);
	s_add(&nm, rest, (size_t)(slash - rest));
	a = arc_find(nm.p ? nm.p : "");
	if (!a) {
		lg(HIBR_LERR, "/dev/archive/%s: no archive of that name is open", nm.p);
		s_free(&nm);
		return -1;
	}
	s_free(&nm);
	m = arc_member(a, slash + 1);
	if (!m || m->kind == 'd') {
		lg(HIBR_LERR, "/dev/archive/: %s is not a file in the archive", slash + 1);
		return -1;
	}
	if (!arc_canread(m, "/dev/archive/"))
		return -1;
	s_init(&data);
	left = m->size;
	while (left > 0) {
		size_t want = left > (long long)ARC_CH ? ARC_CH : (size_t)left;
		ssize_t k;

		s_grow(&data, want);
		k = arc_mread(a, m, data.p + data.n, want, m->size - left);
		if (k <= 0)
			break;
		data.n += (size_t)k;
		left -= k;
	}
	if (left) {
		lg(HIBR_LERR, "/dev/archive/: %s ends early in the archive", m->name);
		s_free(&data);
		return -1;
	}
	fd = arc_tmpfd(data.p ? data.p : "", data.n);
	s_free(&data);
	if (fd < 0) {
		lg(HIBR_LERR, "/dev/archive/: nowhere to put %s", m->name);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	return fd;
}


/* Make every folder above a path that is relative to the descriptor dfd, as mkdir -p would; an existing folder is fine, anything else there is not. */
int arc_mkparents(int dfd, const char *rel)
{
	str t;
	size_t i;
	int ok = 1;

	s_init(&t);
	s_cat(&t, rel);
	for (i = 1; ok && t.p && i < t.n; i++) {
		struct stat st;

		if (t.p[i] != '/')
			continue;
		t.p[i] = 0;
		if (fstatat(dfd, t.p, &st, AT_SYMLINK_NOFOLLOW) == 0) {
			if (!S_ISDIR(st.st_mode))
				ok = 0;
		} else if (mkdirat(dfd, t.p, 0755) != 0 && errno != EEXIST) {
			ok = 0;
		}
		t.p[i] = '/';
	}
	s_free(&t);
	return ok;
}

/* The name a member lands under when PATH (n bytes, tidied) was asked for: PATH's own last part and what is below it, or the whole name when nothing was. 0 when the member is not PATH or under it. */
const char *arc_landing(const char *name, const char *path, size_t pn)
{
	const char *base;

	if (!pn)
		return name;
	if (strncmp(name, path, pn) || (name[pn] && name[pn] != '/'))
		return 0;
	base = strrchr(path, '/');
	return name + (base ? (size_t)(base - path) + 1 : 0);
}

/* Write one member's bytes to a new file at rel under dfd, with its permission bits (never set-id or sticky) and its time. */
int arc_putfile(arcf *a, arcm *m, int dfd, const char *rel)
{
	int fd = openat(dfd, rel, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
	long long left = m->size;
	char *buf;
	struct timespec ts[2];

	if (fd < 0)
		return 0;
	buf = xm(ARC_CH);
	while (left > 0) {
		size_t want = left > (long long)ARC_CH ? ARC_CH : (size_t)left;
		ssize_t k = arc_mread(a, m, buf, want, m->size - left);

		if (k <= 0 || hibr_wrall(fd, buf, (size_t)k) != k)
			break;
		left -= k;
	}
	free(buf);
	fchmod(fd, (m->mode & 0777) | 0400);
	ts[0].tv_sec = ts[1].tv_sec = (time_t)m->mtime;
	ts[0].tv_nsec = ts[1].tv_nsec = 0;
	futimens(fd, ts);
	close(fd);
	return left == 0;
}

/* archive extract NAME DEST [PATH]: every member, or PATH and what is under it, written into the folder DEST. Every name is checked on the path it would be written to before anything is written, and the whole extraction is refused on the first that would land outside DEST, overwrite something there, or link out of it. */
int arc_extract(sh *s, arcf *a, const char *dest, const char *path)
{
	str want, o;
	size_t i, n = 0;
	int dfd, pass, ok = 1;
	struct stat st;

	if (stat(dest, &st) != 0 || !S_ISDIR(st.st_mode)) {
		lg(HIBR_LERR, "archive extract: %s is not a folder", dest);
		return HIBR_FAIL;
	}
	s_init(&want);
	arc_tidy(&want, path ? path : "");
	if (want.n && !arc_isdir(a, want.p, want.n) && !arc_member(a, want.p)) {
		lg(HIBR_LERR, "archive extract: %s: not in %s", want.p, a->nm);
		s_free(&want);
		return HIBR_FAIL;
	}
	for (i = 0; i < a->mem.n; i++) {
		arcm *m = a->mem.p[i];
		const char *r = arc_landing(m->name, want.p ? want.p : "", want.n);
		struct stat t;

		/* A folder whose name is nothing is the archive's own root --
		   `./` as `tar c .` writes it, tidied when indexed -- which is
		   DEST itself and has nothing to write (Gitea #487). */
		if (!r || (!*r && m->kind == 'd'))
			continue;
		if (!arc_safe(r)) {
			lg(HIBR_LERR, "archive extract: %s would land outside %s, so nothing was written", m->name, dest);
			ok = 0;
			break;
		}
		if (m->kind == 'l' && !m->hard &&
		    (!m->link || !arc_safe(m->link))) {
			lg(HIBR_LERR, "archive extract: %s links out of the folder (to %s), so nothing was written",
			   m->name, m->link ? m->link : "");
			ok = 0;
			break;
		}
		if (m->kind == 'f' && m->why) {
			lg(HIBR_LERR, "archive extract: %s %s, so nothing was written", m->name, m->why);
			ok = 0;
			break;
		}
		s_init(&o);
		s_cat(&o, dest);
		s_ch(&o, '/');
		s_cat(&o, r);
		if (lstat(o.p, &t) == 0 && !(m->kind == 'd' && S_ISDIR(t.st_mode))) {
			lg(HIBR_LERR, "archive extract: %s is there already, so nothing was written", o.p);
			ok = 0;
		}
		s_free(&o);
		if (!ok)
			break;
	}
	if (!ok) {
		s_free(&want);
		return HIBR_FAIL;
	}
	dfd = open(dest, O_RDONLY | O_DIRECTORY);
	if (dfd < 0) {
		lg(HIBR_LERR, "archive extract: %s cannot be opened", dest);
		s_free(&want);
		return HIBR_FAIL;
	}
	for (pass = 0; pass < 2 && ok; pass++) {
		for (i = 0; i < a->mem.n && ok; i++) {
			arcm *m = a->mem.p[i];
			const char *r = arc_landing(m->name, want.p ? want.p : "", want.n);

			if (!r || (!*r && m->kind == 'd') ||
			    (pass == 0) == (m->kind == 'l'))
				continue;
			if (!arc_mkparents(dfd, r)) {
				lg(HIBR_LERR, "archive extract: cannot make the folders above %s", r);
				ok = 0;
				break;
			}
			if (m->kind == 'd') {
				if (mkdirat(dfd, r, (m->mode & 0777) | 0700) != 0 && errno != EEXIST) {
					lg(HIBR_LERR, "archive extract: cannot make %s", r);
					ok = 0;
				}
			} else if (m->kind == 'l') {
				if (m->hard) {
					lg(HIBR_LDBG, "archive extract: %s is a hard link, left out", r);
					continue;
				}
				if (symlinkat(m->link, dfd, r) != 0) {
					lg(HIBR_LERR, "archive extract: cannot make the link %s", r);
					ok = 0;
				}
			} else if (!arc_putfile(a, m, dfd, r)) {
				lg(HIBR_LERR, "archive extract: cannot write %s", r);
				ok = 0;
			}
			if (ok)
				n++;
		}
	}
	close(dfd);
	s_free(&want);
	if (s->bind) {
		s_init(&o);
		s_num(&o, (long)n);
		hibr_ret(s, o.p ? o.p : "0");
		s_free(&o);
	}
	return ok ? HIBR_OK : HIBR_FAIL;
}

/* An octal field of width w, NUL-terminated as ustar wants; a size too large for it is written base-256, as GNU tar does. */
void arc_oct(unsigned char *f, size_t w, unsigned long long v)
{
	size_t i;

	if (v >> (3 * (w - 1))) {
		memset(f, 0, w);
		f[0] = 0x80;
		for (i = w - 1; i > 0 && v; i--, v >>= 8)
			f[i] = (unsigned char)(v & 0xff);
		return;
	}
	f[w - 1] = 0;
	for (i = w - 1; i > 0; i--, v >>= 3)
		f[i - 1] = (unsigned char)('0' + (v & 7));
}

/* One ustar header block appended to o: a GNU long-name block ahead of it when the name fits neither the name field nor a prefix split, and another for a link target past a hundred bytes. */
void arc_hdr(str *o, const char *name, char kind, unsigned mode,
	     unsigned long long size, long long mtime, const char *link)
{
	/* Sized by the format: a tar header is one 512-byte block. */
	unsigned char h[512];
	size_t nl = strlen(name), ll = link ? strlen(link) : 0, i, cut = 0;
	unsigned sum = 0;

	if (ll > 100)
		arc_hdr(o, "././@LongLink", 'K', 0644, ll + 1, 0, 0);
	if (ll > 100) {
		s_add(o, link, ll + 1);
		while (o->n % 512)
			s_ch(o, 0);
	}
	if (nl > 100) {
		for (i = nl - 1; i > 0; i--)
			if (name[i] == '/' && i <= 155 && nl - i - 1 <= 100 && nl - i - 1 > 0) {
				cut = i;
				break;
			}
		if (!cut) {
			arc_hdr(o, "././@LongLink", 'L', 0644, nl + 1, 0, 0);
			s_add(o, name, nl + 1);
			while (o->n % 512)
				s_ch(o, 0);
		}
	}
	memset(h, 0, sizeof h);
	if (nl <= 100)
		memcpy(h, name, nl);
	else if (cut) {
		memcpy(h, name + cut + 1, nl - cut - 1);
		memcpy(h + 345, name, cut);
	} else
		memcpy(h, name, 100);
	arc_oct(h + 100, 8, mode & 07777);
	arc_oct(h + 108, 8, (unsigned long long)getuid());
	arc_oct(h + 116, 8, (unsigned long long)getgid());
	arc_oct(h + 124, 12, size);
	arc_oct(h + 136, 12, (unsigned long long)(mtime < 0 ? 0 : mtime));
	memset(h + 148, ' ', 8);
	h[156] = (unsigned char)kind;
	if (link)
		memcpy(h + 157, link, ll > 100 ? 100 : ll);
	memcpy(h + 257, "ustar", 6);
	memcpy(h + 263, "00", 2);
	for (i = 0; i < 512; i++)
		sum += h[i];
	arc_oct(h + 148, 7, sum);
	h[155] = ' ';
	s_add(o, (const char *)h, 512);
}

/* Everything a create needs as it walks: where to write, whether it is all kept for gzip, how far it may grow, and the file being written so it is never put inside itself. */
typedef struct arcw arcw;
struct arcw {
	str o;
	int fd, gz, bad;
	size_t max;
	dev_t dev;
	ino_t ino;
};

/* Write what is held to the file, unless it is being kept whole for gzip. */
int arc_wflush(arcw *w)
{
	if (w->gz) {
		if (w->o.n > w->max) {
			lg(HIBR_LERR, "archive create: larger than ARCHIVE_MAX before compressing");
			w->bad = 1;
		}
		return !w->bad;
	}
	if (w->o.n && hibr_wrall(w->fd, w->o.p, w->o.n) != (ssize_t)w->o.n) {
		lg(HIBR_LERR, "archive create: cannot write the archive");
		w->bad = 1;
	}
	w->o.n = 0;
	return !w->bad;
}

/* Sort names for a folder's listing, so an archive of the same tree is the same archive. */
int arc_namecmp(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

/* Add the path rel (under base) and, for a folder, everything in it, depth first, to the archive; depth bounds how far a tree may nest. */
void arc_add(arcw *w, const char *base, const char *rel, int depth)
{
	str full, nm;
	struct stat st;

	if (w->bad)
		return;
	if (depth > 256) {
		lg(HIBR_LERR, "archive create: %s nests deeper than 256 folders", rel);
		w->bad = 1;
		return;
	}
	s_init(&full);
	s_cat(&full, base);
	s_ch(&full, '/');
	s_cat(&full, rel);
	if (lstat(full.p, &st) != 0) {
		lg(HIBR_LERR, "archive create: %s cannot be read", full.p);
		w->bad = 1;
		s_free(&full);
		return;
	}
	if (st.st_dev == w->dev && st.st_ino == w->ino) {
		s_free(&full);
		return;
	}
	s_init(&nm);
	s_cat(&nm, rel);
	if (S_ISDIR(st.st_mode)) {
		DIR *d;
		struct dirent *e;
		vec names;
		size_t i;

		s_ch(&nm, '/');
		arc_hdr(&w->o, nm.p, '5', st.st_mode, 0, (long long)st.st_mtime, 0);
		arc_wflush(w);
		d = opendir(full.p);
		if (!d) {
			lg(HIBR_LERR, "archive create: %s cannot be listed", full.p);
			w->bad = 1;
		}
		memset(&names, 0, sizeof names);
		while (d && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			v_add(&names, xs(e->d_name));
		}
		if (d)
			closedir(d);
		if (names.n)
			qsort(names.p, names.n, sizeof *names.p, arc_namecmp);
		for (i = 0; i < names.n; i++) {
			str sub;

			s_init(&sub);
			s_cat(&sub, rel);
			s_ch(&sub, '/');
			s_cat(&sub, names.p[i]);
			arc_add(w, base, sub.p, depth + 1);
			s_free(&sub);
			free(names.p[i]);
		}
		v_free(&names);
	} else if (S_ISLNK(st.st_mode)) {
		str t;
		ssize_t k;

		s_init(&t);
		s_grow(&t, (size_t)st.st_size + 256);
		k = readlink(full.p, t.p, (size_t)st.st_size + 255);
		if (k < 0) {
			lg(HIBR_LERR, "archive create: %s cannot be read", full.p);
			w->bad = 1;
		} else {
			t.p[k] = 0;
			arc_hdr(&w->o, nm.p, '2', 0777, 0, (long long)st.st_mtime, t.p);
			arc_wflush(w);
		}
		s_free(&t);
	} else if (S_ISREG(st.st_mode)) {
		int fd = open(full.p, O_RDONLY);
		unsigned long long left = (unsigned long long)st.st_size;

		if (fd < 0) {
			lg(HIBR_LERR, "archive create: %s cannot be read", full.p);
			w->bad = 1;
		} else {
			arc_hdr(&w->o, nm.p, '0', st.st_mode, left, (long long)st.st_mtime, 0);
			while (left > 0 && !w->bad) {
				size_t want = left > ARC_CH ? ARC_CH : (size_t)left;
				ssize_t k;

				s_grow(&w->o, want);
				k = read(fd, w->o.p + w->o.n, want);
				if (k <= 0) {
					memset(w->o.p + w->o.n, 0, want);
					k = (ssize_t)want;
					lg(HIBR_LERR, "archive create: %s shrank while it was read", full.p);
				}
				w->o.n += (size_t)k;
				left -= (unsigned long long)k;
				arc_wflush(w);
			}
			for (left = (512 - (unsigned long long)st.st_size % 512) % 512; left; left--)
				s_ch(&w->o, 0);
			close(fd);
			arc_wflush(w);
		}
	} else {
		lg(HIBR_LERR, "archive create: %s is not a file, a folder or a link, and is left out", full.p);
	}
	s_free(&nm);
	s_free(&full);
}

/* Add everything in the folder base at the top of the archive, sorted, which is what "." asks for. */
void arc_addall(arcw *w, const char *base)
{
	DIR *d = opendir(base);
	struct dirent *e;
	vec names;
	size_t i;

	if (!d) {
		lg(HIBR_LERR, "archive create: %s cannot be listed", base);
		w->bad = 1;
		return;
	}
	memset(&names, 0, sizeof names);
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			v_add(&names, xs(e->d_name));
	closedir(d);
	if (names.n)
		qsort(names.p, names.n, sizeof *names.p, arc_namecmp);
	for (i = 0; i < names.n; i++) {
		if (!w->bad)
			arc_add(w, base, names.p[i], 0);
		free(names.p[i]);
	}
	v_free(&names);
}

/* Put an archive in place where the folder has no hard links, as FAT has none: nothing is there, so a rename replaces nothing. */
int arc_nolink(const char *tmp, const char *out)
{
	struct stat st;

	if (errno != EPERM && errno != ENOTSUP && errno != ENOSYS)
		return 0;
	if (lstat(out, &st) == 0) {
		errno = EEXIST;
		return 0;
	}
	return rename(tmp, out) == 0;
}

/* archive create [-z] OUT [-C DIR] PATH...: a new tar of the paths named, relative to DIR (the current folder by default), gzipped with -z or when OUT ends .gz or .tgz. Written beside OUT and moved into place only when whole, and never over a file that is there. */
int arc_create(sh *s, int ac, char **av)
{
	const char *out = 0, *base = ".";
	int i = 2, gz = 0, first;
	arcw w;
	str tmp, z;
	struct stat st;
	const char *v;
	size_t ol;

	if (i < ac && !strcmp(av[i], "-z")) {
		gz = 1;
		i++;
	}
	if (i >= ac) {
		lg(HIBR_LERR, "usage: archive create [-z] OUT [-C DIR] PATH...");
		return 2;
	}
	out = av[i++];
	if (i + 1 < ac && !strcmp(av[i], "-C")) {
		base = av[i + 1];
		i += 2;
	}
	if (i >= ac) {
		lg(HIBR_LERR, "usage: archive create [-z] OUT [-C DIR] PATH...");
		return 2;
	}
	first = i;
	for (; i < ac; i++)
		if (strcmp(av[i], ".") && !arc_safe(av[i])) {
			lg(HIBR_LERR, "archive create: %s: name the paths relative to the folder, with no ..", av[i]);
			return HIBR_FAIL;
		}
	ol = strlen(out);
	if ((ol > 3 && !strcmp(out + ol - 3, ".gz")) || (ol > 4 && !strcmp(out + ol - 4, ".tgz")))
		gz = 1;
	if (lstat(out, &st) == 0) {
		lg(HIBR_LERR, "archive create: %s is there already", out);
		return HIBR_FAIL;
	}
	memset(&w, 0, sizeof w);
	s_init(&w.o);
	w.gz = gz;
	w.max = ARC_MAXGZ;
	if ((v = hibr_get(s, "ARCHIVE_MAX")) && *v)
		w.max = (size_t)strtoull(v, 0, 10);
	s_init(&tmp);
	s_cat(&tmp, out);
	s_cat(&tmp, ".hibr-part");
	w.fd = open(tmp.p, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (w.fd < 0) {
		lg(HIBR_LERR, "archive create: cannot write %s", tmp.p);
		s_free(&tmp);
		return HIBR_FAIL;
	}
	fstat(w.fd, &st);
	w.dev = st.st_dev;
	w.ino = st.st_ino;
	for (i = first; i < ac && !w.bad; i++)
		if (!strcmp(av[i], "."))
			arc_addall(&w, base);
		else
			arc_add(&w, base, av[i], 0);
	if (!w.bad) {
		s_grow(&w.o, 1024);
		memset(w.o.p + w.o.n, 0, 1024);
		w.o.n += 1024;
		if (gz) {
			s_init(&z);
			if (!def_gzip((const unsigned char *)(w.o.p ? w.o.p : ""), w.o.n,
				      (long)time(0), &z) ||
			    hibr_wrall(w.fd, z.p, z.n) != (ssize_t)z.n) {
				lg(HIBR_LERR, "archive create: cannot write the compressed archive");
				w.bad = 1;
			}
			s_free(&z);
			w.o.n = 0;
		} else {
			arc_wflush(&w);
		}
	}
	s_free(&w.o);
	if (close(w.fd) != 0)
		w.bad = 1;
	if (!w.bad && link(tmp.p, out) != 0 && !arc_nolink(tmp.p, out)) {
		lg(HIBR_LERR, "archive create: %s", errno == EEXIST ?
		   "something took the name while it was written" : "cannot put the archive in place");
		w.bad = 1;
	}
	unlink(tmp.p);
	s_free(&tmp);
	return w.bad ? HIBR_FAIL : HIBR_OK;
}

/* archive open|close|list|ls|stat|cat|extract|create ... */
int arc_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	arcf *a;
	str o;
	size_t i;

	if (!strcmp(sub, "open") && ac == 4)
		return arc_openarch(s, av[2], av[3]);
	if (!strcmp(sub, "create"))
		return arc_create(s, ac, av);
	if (!strcmp(sub, "extract") && (ac == 4 || ac == 5)) {
		a = arc_find(av[2]);
		if (!a) {
			lg(HIBR_LERR, "archive extract: %s is not open", av[2]);
			return HIBR_FAIL;
		}
		return arc_extract(s, a, av[3], ac == 5 ? av[4] : 0);
	}
	if (!strcmp(sub, "close") && ac == 3) {
		for (i = 0; i < arc_list.n; i++) {
			a = arc_list.p[i];
			if (strcmp(a->nm, av[2]))
				continue;
			arc_list.p[i] = arc_list.p[arc_list.n - 1];
			arc_list.n--;
			arc_free(a);
			return HIBR_OK;
		}
		lg(HIBR_LERR, "archive close: %s is not open", av[2]);
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "list") && ac == 2) {
		s_init(&o);
		for (i = 0; i < arc_list.n; i++) {
			a = arc_list.p[i];
			s_cat(&o, a->nm);
			s_ch(&o, '\t');
			s_cat(&o, a->path);
			s_ch(&o, '\t');
			s_num(&o, (long)a->mem.n);
			s_ch(&o, '\n');
		}
		if (o.n)
			fwrite(o.p, 1, o.n, stdout);
		s_free(&o);
		return HIBR_OK;
	}
	if ((!strcmp(sub, "ls") || !strcmp(sub, "stat") || !strcmp(sub, "cat")) && ac >= 3) {
		a = arc_find(av[2]);
		if (!a) {
			lg(HIBR_LERR, "archive %s: %s is not open", sub, av[2]);
			return HIBR_FAIL;
		}
		if (!strcmp(sub, "ls"))
			return arc_ls(s, a, ac > 3 ? av[3] : 0);
		if (!strcmp(sub, "cat")) {
			if (ac != 4) {
				lg(HIBR_LERR, "usage: archive cat NAME PATH");
				return 2;
			}
			return arc_cat(s, a, av[3]);
		}
		if (ac != 4) {
			lg(HIBR_LERR, "usage: archive stat NAME PATH");
			return 2;
		}
		{
			arcm *m = arc_member(a, av[3]);

			if (!m) {
				lg(HIBR_LERR, "archive stat: %s: not in %s", av[3], av[2]);
				return HIBR_FAIL;
			}
			s_init(&o);
			arc_line(&o, m, 0);
			hibr_ret(s, o.p ? o.p : "");
			if (!s->bind) {
				fwrite(o.p, 1, o.n, stdout);
				fputc('\n', stdout);
			}
			s_free(&o);
			return HIBR_OK;
		}
	}
	lg(HIBR_LERR, "usage: archive open NAME FILE | close NAME | list | "
		      "ls NAME [PATH] | stat NAME PATH | cat NAME PATH | "
		      "extract NAME DEST [PATH] | create [-z] OUT [-C DIR] PATH...");
	return 2;
}

/* Register the scheme. */
int arc_init(sh *s)
{
	memset(&arc_list, 0, sizeof arc_list);
	return hibr_schemef(s, "archive", arc_scheme, HIBR_SCH_LOCAL);
}

/* Close every archive and withdraw the scheme. */
void arc_fini(sh *s)
{
	while (arc_list.n)
		arc_free(arc_list.p[--arc_list.n]);
	v_free(&arc_list);
	hibr_unscheme(s, "archive");
}

const hibr_bi arc_bis[] = {
	{ "archive", arc_bi, "a tar, zip or disc image as a folder: archive open|close|list|ls|stat|cat|extract|create, "
			 "and /dev/archive/NAME/path anywhere a filename goes" },
	HIBR_BI_END
};

HIBR_MODULE("archive", "1.2", "tar and tar.gz read as folders, written and extracted; zip and disc images read",
	    arc_bis, arc_init, arc_fini);
