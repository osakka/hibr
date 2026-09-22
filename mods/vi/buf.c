#define _GNU_SOURCE

#include "vi.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Start an empty buffer. */
void vi_binit(vi_buf *b)
{
	memset(b, 0, sizeof *b);
	b->d = xm(VI_GAP);
	b->cap = VI_GAP;
	b->gp = 0;
	b->gn = VI_GAP;
	b->dirty = 0;
}

/* Release a buffer. */
void vi_bfree(vi_buf *b)
{
	free(b->d);
	v_free(&b->ls);
	memset(b, 0, sizeof *b);
}

/* How much text there is, the gap not counted. */
size_t vi_len(const vi_buf *b)
{
	return b->cap - b->gn;
}

/* The byte at a logical position, or -1 past the end. */
int vi_at(const vi_buf *b, size_t pos)
{
	if (pos >= vi_len(b))
		return -1;
	return (unsigned char)b->d[pos < b->gp ? pos : pos + b->gn];
}

/* Copy a run of text out, which is two pieces when it spans the gap. */
void vi_get(const vi_buf *b, size_t pos, size_t n, str *out)
{
	size_t len = vi_len(b), k;

	if (pos >= len)
		return;
	if (pos + n > len)
		n = len - pos;
	if (pos + n <= b->gp) {
		s_add(out, b->d + pos, n);
		return;
	}
	if (pos >= b->gp) {
		s_add(out, b->d + pos + b->gn, n);
		return;
	}
	k = b->gp - pos;
	s_add(out, b->d + pos, k);
	s_add(out, b->d + b->gp + b->gn, n - k);
}

/* Put the gap where the next edit is, which is the only copying that happens. */
void vi_move(vi_buf *b, size_t pos)
{
	if (pos == b->gp)
		return;
	if (pos < b->gp)
		memmove(b->d + pos + b->gn, b->d + pos, b->gp - pos);
	else
		memmove(b->d + b->gp, b->d + b->gp + b->gn, pos - b->gp);
	b->gp = pos;
}

/* Make sure the gap can take n more bytes. */
void vi_room(vi_buf *b, size_t n)
{
	size_t len, want;
	char *nd;

	if (b->gn >= n)
		return;
	len = vi_len(b);
	want = b->cap * 2;
	while (want < len + n + VI_GAP)
		want = want * 2;
	nd = xm(want);
	memcpy(nd, b->d, b->gp);
	memcpy(nd + want - (len - b->gp), b->d + b->gp + b->gn, len - b->gp);
	free(b->d);
	b->d = nd;
	b->gn = want - len;
	b->cap = want;
}

/* Note that the line index is wrong from this position onwards. */
void vi_soil(vi_buf *b, size_t pos)
{
	size_t i, n = b->ls.n;

	b->mod = 1;
	if (!n) {
		b->dirty = 0;
		return;
	}
	for (i = n; i-- > 0;)
		if ((size_t)(uintptr_t)b->ls.p[i] <= pos) {
			if (i < b->dirty)
				b->dirty = i;
			return;
		}
	b->dirty = 0;
}

/* Insert text at a logical position. */
void vi_ins(vi_buf *b, size_t pos, const char *t, size_t n)
{
	if (!n)
		return;
	if (pos > vi_len(b))
		pos = vi_len(b);
	vi_soil(b, pos);
	vi_move(b, pos);
	vi_room(b, n);
	memcpy(b->d + b->gp, t, n);
	b->gp += n;
	b->gn -= n;
}

/* Remove n bytes at a logical position. */
void vi_del(vi_buf *b, size_t pos, size_t n)
{
	size_t len = vi_len(b);

	if (pos >= len || !n)
		return;
	if (pos + n > len)
		n = len - pos;
	vi_soil(b, pos);
	vi_move(b, pos);
	b->gn += n;
}

/* Rebuild the line index from the first entry that an edit invalidated. */
void vi_index(vi_buf *b)
{
	size_t i, len = vi_len(b), from;

	if (b->ls.n && b->dirty >= b->ls.n)
		return;
	if (!b->ls.n) {
		b->ls.n = 0;
		v_add(&b->ls, (void *)(uintptr_t)0);
		from = 0;
		b->dirty = 1;
	} else {
		if (b->dirty == 0)
			b->dirty = 1;
		b->ls.n = b->dirty;
		from = (size_t)(uintptr_t)b->ls.p[b->dirty - 1];
	}
	for (i = from; i < len; i++)
		if (vi_at(b, i) == '\n')
			v_add(&b->ls, (void *)(uintptr_t)(i + 1));
	b->dirty = b->ls.n;
}

/* How many lines there are; an empty buffer still has one. */
size_t vi_nlines(vi_buf *b)
{
	size_t len = vi_len(b);

	vi_index(b);
	if (len && vi_at(b, len - 1) == '\n')
		return b->ls.n - 1 ? b->ls.n - 1 : 1;
	return b->ls.n;
}

/* Where a line starts. */
size_t vi_lstart(vi_buf *b, size_t i)
{
	vi_index(b);
	if (i >= b->ls.n)
		return vi_len(b);
	return (size_t)(uintptr_t)b->ls.p[i];
}

/* Where a line ends, not counting its newline. */
size_t vi_lend(vi_buf *b, size_t i)
{
	size_t len = vi_len(b), e;

	vi_index(b);
	if (i + 1 < b->ls.n) {
		e = (size_t)(uintptr_t)b->ls.p[i + 1];
		return e ? e - 1 : 0;
	}
	return len;
}

/* Which line a position falls on. */
size_t vi_lineof(vi_buf *b, size_t pos)
{
	size_t lo = 0, hi, mid;

	vi_index(b);
	hi = b->ls.n;
	while (lo + 1 < hi) {
		mid = (lo + hi) / 2;
		if ((size_t)(uintptr_t)b->ls.p[mid] <= pos)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* The next position, a whole character along. */
size_t vi_next(const vi_buf *b, size_t pos)
{
	size_t len = vi_len(b);
	int c;

	if (pos >= len)
		return len;
	c = vi_at(b, pos);
	pos++;
	while (pos < len && (vi_at(b, pos) & 0xC0) == 0x80)
		pos++;
	(void)c;
	return pos;
}

/* The previous position, a whole character back. */
size_t vi_prev(const vi_buf *b, size_t pos)
{
	if (!pos)
		return 0;
	pos--;
	while (pos && (vi_at(b, pos) & 0xC0) == 0x80)
		pos--;
	return pos;
}

/* Read a file in, replacing whatever the buffer held. */
int vi_load(vi_buf *b, const char *path)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	char *t;
	ssize_t k;
	size_t got = 0;

	vi_bfree(b);
	vi_binit(b);
	if (fd < 0)
		return errno == ENOENT ? HIBR_OK : HIBR_FAIL;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		vi_room(b, (size_t)st.st_size + 1);
		t = xm((size_t)st.st_size);
		while (got < (size_t)st.st_size) {
			k = read(fd, t + got, (size_t)st.st_size - got);
			if (k <= 0)
				break;
			got += (size_t)k;
		}
		vi_ins(b, 0, t, got);
		free(t);
	}
	close(fd);
	b->mod = 0;
	b->dirty = 0;
	b->ls.n = 0;
	return HIBR_OK;
}

/* Remember what the file looked like when it was read. */
int vi_stamp(vi_ed *e)
{
	struct stat st;

	if (!e->path || stat(e->path, &st) != 0) {
		e->ondisk = 0;
		return HIBR_OK;
	}
	e->ondisk = 1;
	e->mtim = (long)st.st_mtime;
	e->mtin = (long)HIBR_MTIM(st).tv_nsec;
	e->fsize = (size_t)st.st_size;
	return HIBR_OK;
}

/* True when the file moved underneath us since it was read. */
int vi_changed(vi_ed *e)
{
	struct stat st;

	if (!e->path)
		return 0;
	if (stat(e->path, &st) != 0)
		return e->ondisk;
	if (!e->ondisk)
		return 1;
	return (long)st.st_mtime != e->mtim ||
	       (long)HIBR_MTIM(st).tv_nsec != e->mtin ||
	       (size_t)st.st_size != e->fsize;
}

/* Write the buffer out through a temporary file, so a crash cannot truncate. */
int vi_save(const vi_buf *b, const char *path)
{
	str tmp, out;
	int fd, ok = HIBR_OK;
	size_t n;
	ssize_t k;
	struct stat st;

	s_init(&tmp);
	s_cat(&tmp, path);
	s_cat(&tmp, ".hibr-vi-tmp");
	fd = open(tmp.p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		lg(HIBR_LERR, "vi: %s: %s", tmp.p, strerror(errno));
		s_free(&tmp);
		return HIBR_FAIL;
	}
	s_init(&out);
	vi_get(b, 0, vi_len(b), &out);
	n = 0;
	while (n < out.n) {
		k = write(fd, out.p + n, out.n - n);
		if (k <= 0) {
			ok = HIBR_FAIL;
			break;
		}
		n += (size_t)k;
	}
	if (stat(path, &st) == 0)
		fchmod(fd, st.st_mode & 07777);
	if (close(fd) != 0)
		ok = HIBR_FAIL;
	if (ok == HIBR_OK && rename(tmp.p, path) != 0) {
		lg(HIBR_LERR, "vi: %s: %s", path, strerror(errno));
		ok = HIBR_FAIL;
	}
	if (ok != HIBR_OK)
		unlink(tmp.p);
	s_free(&out);
	s_free(&tmp);
	return ok;
}
