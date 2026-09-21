#include "pr.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Rotate a word left. */
unsigned sh_rol(unsigned v, int n)
{
	return (v << n) | (v >> (32 - n));
}

/* Mix one sixty four byte block into the running state. */
void sh_block(unsigned *h, const unsigned char *p)
{
	unsigned *w = xm(80 * sizeof *w);
	unsigned a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], t;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((unsigned)p[i * 4] << 24) |
		       ((unsigned)p[i * 4 + 1] << 16) |
		       ((unsigned)p[i * 4 + 2] << 8) | (unsigned)p[i * 4 + 3];
	for (; i < 80; i++)
		w[i] = sh_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (i = 0; i < 80; i++) {
		if (i < 20)
			t = ((b & c) | (~b & d)) + 0x5a827999u;
		else if (i < 40)
			t = (b ^ c ^ d) + 0x6ed9eba1u;
		else if (i < 60)
			t = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdcu;
		else
			t = (b ^ c ^ d) + 0xca62c1d6u;
		t += sh_rol(a, 5) + e + w[i];
		e = d;
		d = c;
		c = sh_rol(b, 30);
		b = a;
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	free(w);
}

/* Start a hash. */
void sh_init(sha1 *s)
{
	s->h[0] = 0x67452301u;
	s->h[1] = 0xefcdab89u;
	s->h[2] = 0x98badcfeu;
	s->h[3] = 0x10325476u;
	s->h[4] = 0xc3d2e1f0u;
	s->n = 0;
	s->tot = 0;
}

/* Add bytes to a hash. */
void sh_add(sha1 *s, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;

	s->tot += n;
	while (n) {
		size_t k = SH_BLK - s->n;
		if (k > n)
			k = n;
		memcpy(s->buf + s->n, b, k);
		s->n += k;
		b += k;
		n -= k;
		if (s->n == SH_BLK) {
			sh_block(s->h, s->buf);
			s->n = 0;
		}
	}
}

/* Finish a hash, producing twenty bytes. */
void sh_done(sha1 *s, unsigned char *out)
{
	unsigned long long bits = (unsigned long long)s->tot * 8;
	unsigned char pad = 0x80;
	unsigned char z = 0;
	int i;

	sh_add(s, &pad, 1);
	s->tot--;
	while (s->n != 56) {
		sh_add(s, &z, 1);
		s->tot--;
	}
	for (i = 7; i >= 0; i--) {
		unsigned char c = (unsigned char)(bits >> (i * 8));
		sh_add(s, &c, 1);
		s->tot--;
	}
	for (i = 0; i < 5; i++) {
		out[i * 4] = (unsigned char)(s->h[i] >> 24);
		out[i * 4 + 1] = (unsigned char)(s->h[i] >> 16);
		out[i * 4 + 2] = (unsigned char)(s->h[i] >> 8);
		out[i * 4 + 3] = (unsigned char)s->h[i];
	}
}

/* Hash a path the way git names a blob, following the index's rules. */
int sh_blob(const char *path, int symlink, unsigned char *out)
{
	sha1 s;
	str hdr;
	unsigned char *buf;
	int fd;
	ssize_t got;
	size_t total = 0;
	struct stat st;

	if (lstat(path, &st) != 0)
		return 0;
	sh_init(&s);
	s_init(&hdr);
	s_cat(&hdr, "blob ");
	s_num(&hdr, (long)st.st_size);
	s_ch(&hdr, 0);
	if (symlink) {
		char *lnk = xm((size_t)st.st_size + 1);
		ssize_t k = readlink(path, lnk, (size_t)st.st_size);
		if (k < 0) {
			free(lnk);
			s_free(&hdr);
			return 0;
		}
		hdr.n = 0;
		s_cat(&hdr, "blob ");
		s_num(&hdr, (long)k);
		s_ch(&hdr, 0);
		sh_add(&s, hdr.p, hdr.n);
		sh_add(&s, lnk, (size_t)k);
		free(lnk);
		s_free(&hdr);
		sh_done(&s, out);
		return 1;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		s_free(&hdr);
		return 0;
	}
	sh_add(&s, hdr.p, hdr.n);
	s_free(&hdr);
	buf = xm(HIBR_IOCH);
	while ((got = read(fd, buf, HIBR_IOCH)) > 0) {
		sh_add(&s, buf, (size_t)got);
		total += (size_t)got;
	}
	free(buf);
	close(fd);
	if (got < 0 || total != (size_t)st.st_size)
		return 0;
	sh_done(&s, out);
	return 1;
}
