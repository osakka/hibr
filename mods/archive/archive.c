/* archive -- an archive as a folder (Gitea #102).
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
 *
 * Reading only. A tar is append-only in practice, and rewriting one to
 * change a member is the thing archivemount does badly; writing waits until
 * it can be honest about rewriting the whole file.
 *
 * .tar, and .tar.gz/.tgz through the inflate the prompt module already
 * had (mods/inflate.c, shared now). A compressed archive is expanded once,
 * at open, into a file of its own that nothing else can see -- unlinked
 * the moment it is made -- so every later read is a seek rather than another
 * pass over the whole stream. An uncompressed one is read where it lies.
 */
#include "hibr.h"
#include "../inflate.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* One member: where its bytes are in the (expanded) archive, and what the
   header said about it. */
typedef struct arcm arcm;
struct arcm {
	char *name;
	long long size, off, mtime;
	unsigned mode;
	char kind;		/* f a file, d a folder, l a link, else as given */
	int hard;		/* a hard link ('1'), which extract does not make */
	char *link;
};

/* An archive that is open: the expanded bytes in a file, and the index. */
typedef struct arcf arcf;
struct arcf {
	char *nm, *path;
	int fd;			/* the archive, or the expanded copy of it */
	int tmp;		/* whether fd is ours to close and forget */
	vec mem;
};

static vec arc_list;

/* An octal field, which tar writes space or NUL padded, and GNU sometimes
   writes as base 256 with the top bit set. */
long long arc_num(const char *p, size_t n)
{
	long long v = 0;
	size_t i = 0;

	if (n && (p[0] & 0x80)) {	/* GNU base 256 */
		v = p[0] & 0x40 ? -1 : 0;
		for (i = 0; i < n; i++)
			v = (v << 8) | (unsigned char)p[i];
		return v;
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

/* Open an archive under a name: gzip is expanded once, into a file of its
   own; anything else is read where it lies. */
int arc_openarch(sh *s, const char *nm, const char *path)
{
	arcf *a;
	unsigned char magic[2];
	int fd;
	size_t max = ARC_MAXGZ;
	const char *v;

	if (arc_find(nm)) {
		lg(HIBR_LERR, "archive open: %s is open already", nm);
		return HIBR_FAIL;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		lg(HIBR_LERR, "archive open: %s: cannot be read", path);
		return HIBR_FAIL;
	}
	a = xm(sizeof *a);
	memset(a, 0, sizeof *a);
	a->nm = xs(nm);
	a->path = xs(path);
	a->fd = fd;
	a->tmp = 0;
	if (read(fd, magic, 2) == 2 && magic[0] == 0x1f && magic[1] == 0x8b) {
		str raw, out;
		int ok;

		if ((v = hibr_get(s, "ARCHIVE_MAX")) && *v)
			max = (size_t)strtoull(v, 0, 10);
		s_init(&raw);
		s_init(&out);
		if (!arc_slurp(path, &raw)) {
			lg(HIBR_LERR, "archive open: %s: cannot be read", path);
			s_free(&raw);
			s_free(&out);
			arc_free(a);
			return HIBR_FAIL;
		}
		ok = inf_gzip((const unsigned char *)raw.p, raw.n, max, &out, 0);
		s_free(&raw);
		if (!ok) {
			lg(HIBR_LERR, "archive open: %s: the gzip stream does not "
			   "expand (or is larger than ARCHIVE_MAX)", path);
			s_free(&out);
			arc_free(a);
			return HIBR_FAIL;
		}
		close(a->fd);
		a->fd = arc_tmpfd(out.p ? out.p : "", out.n);
		a->tmp = 1;
		lg(HIBR_LDBG, "archive: %s expanded to %zu bytes", path, out.n);
		s_free(&out);
		if (a->fd < 0) {
			lg(HIBR_LERR, "archive open: nowhere to expand %s to", path);
			arc_free(a);
			return HIBR_FAIL;
		}
	}
	if (!arc_index(a)) {
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
	s_init(&o);
	buf = xm(ARC_CH);
	left = m->size;
	while (left > 0) {
		size_t want = left > (long long)ARC_CH ? ARC_CH : (size_t)left;
		ssize_t k = pread(a->fd, buf, want, (off_t)(m->off + m->size - left));

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
	s_init(&data);
	left = m->size;
	while (left > 0) {
		size_t want = left > (long long)ARC_CH ? ARC_CH : (size_t)left;
		ssize_t k;

		s_grow(&data, want);
		k = pread(a->fd, data.p + data.n, want,
			  (off_t)(m->off + m->size - left));
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
		ssize_t k = pread(a->fd, buf, want, (off_t)(m->off + m->size - left));

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

		if (!r)
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

			if (!r || (pass == 0) == (m->kind == 'l'))
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
	{ "archive", arc_bi, "an archive as a folder: archive open|close|list|ls|stat|cat|extract|create, "
			 "and /dev/archive/NAME/path anywhere a filename goes" },
	HIBR_BI_END
};

HIBR_MODULE("archive", "1.1", "tar and tar.gz read as folders, written and extracted",
	    arc_bis, arc_init, arc_fini);
