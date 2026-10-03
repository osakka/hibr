#define _GNU_SOURCE

#include "im.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libturbojpeg dlopen'd on first use, the way png.c loads libpng and
   src/net.c loads libssl. Its TurboJPEG API is plain functions on an
   opaque handle -- nothing of libjpeg's own jpeg_decompress_struct to lay
   out by hand -- and has kept these five since TurboJPEG 1.4, through 3.x,
   where they remain as the API every caller of 2.x used. */

#ifndef JP_RGB
#define JP_RGB 0
#endif

/* A JPEG larger than this many pixels is refused rather than decoded: a
   crafted header can declare dimensions a terminal will never show. */
#ifndef JP_MAXPX
#define JP_MAXPX (64L * 1024 * 1024)
#endif

/* A file larger than this is refused before it is read. */
#ifndef JP_MAXFILE
#define JP_MAXFILE (256L * 1024 * 1024)
#endif

/* The few TurboJPEG calls used. */
typedef struct jp_api jp_api;
struct jp_api {
	void *(*init)(void);
	int (*header)(void *, const unsigned char *, unsigned long, int *, int *,
		      int *, int *);
	int (*decode)(void *, const unsigned char *, unsigned long,
		      unsigned char *, int, int, int, int, int);
	int (*destroy)(void *);
	char *(*error)(void *);
};

jp_api jp;
void *jp_lib;
int jp_tried;

/* Load libturbojpeg once; a missing one is said once, not on every frame
   a JPEG wallpaper is drawn. */
int jp_load(str *err)
{
	static const char *names[] = {
		"libturbojpeg.so.0", "libturbojpeg.so", "libturbojpeg.0.dylib",
		"libturbojpeg.dylib", "/opt/homebrew/opt/jpeg-turbo/lib/libturbojpeg.dylib",
		"/usr/local/opt/jpeg-turbo/lib/libturbojpeg.dylib",
		"/opt/homebrew/lib/libturbojpeg.dylib", "/usr/local/lib/libturbojpeg.dylib",
		"/opt/local/lib/libturbojpeg.dylib", 0
	};
	int i;

	if (jp_lib)
		return HIBR_OK;
	if (jp_tried) {
		s_cat(err, "no libturbojpeg to decode JPEG with");
		return HIBR_FAIL;
	}
	jp_tried = 1;
	for (i = 0; names[i] && !jp_lib; i++)
		jp_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!jp_lib) {
		s_cat(err, "no libturbojpeg to decode JPEG with (libjpeg-turbo)");
		return HIBR_FAIL;
	}
	jp.init = dlsym(jp_lib, "tjInitDecompress");
	jp.header = dlsym(jp_lib, "tjDecompressHeader3");
	jp.decode = dlsym(jp_lib, "tjDecompress2");
	jp.destroy = dlsym(jp_lib, "tjDestroy");
	jp.error = dlsym(jp_lib, "tjGetErrorStr2");
	if (!jp.init || !jp.header || !jp.decode || !jp.destroy) {
		dlclose(jp_lib);
		jp_lib = 0;
		s_cat(err, "libturbojpeg lacks the TurboJPEG calls needed");
		return HIBR_FAIL;
	}
	lg(HIBR_LDBG, "img: libturbojpeg loaded");
	return HIBR_OK;
}

/* A big- or little-endian number of n bytes. */
unsigned long jp_num(const unsigned char *p, int n, int be)
{
	unsigned long v = 0;
	int i;

	for (i = 0; i < n; i++)
		v |= (unsigned long)p[be ? i : n - 1 - i] << (8 * (n - 1 - i));
	return v;
}

/* The EXIF orientation a JPEG declares, 1 to 8, or 1 when it says none:
   the first APP1 segment holding "Exif", its TIFF header, the first IFD's
   tag 0x0112. Every offset is checked against the bytes there are. */
int jp_orient(const unsigned char *b, size_t n)
{
	size_t i = 2, len, t, ifd, k, cnt;
	int be;
	unsigned long v;

	while (i + 4 <= n && b[i] == 0xFF) {
		len = (size_t)jp_num(b + i + 2, 2, 1);
		if (b[i + 1] == 0xDA || len < 2 || i + 2 + len > n)
			break;
		if (b[i + 1] == 0xE1 && len >= 16 && !memcmp(b + i + 4, "Exif\0\0", 6)) {
			t = i + 10;
			if (!memcmp(b + t, "MM\0*", 4))
				be = 1;
			else if (!memcmp(b + t, "II*\0", 4))
				be = 0;
			else
				return 1;
			ifd = t + jp_num(b + t + 4, 4, be);
			if (ifd + 2 > i + 2 + len)
				return 1;
			cnt = jp_num(b + ifd, 2, be);
			for (k = 0; k < cnt; k++) {
				size_t e = ifd + 2 + k * 12;

				if (e + 12 > i + 2 + len)
					return 1;
				if (jp_num(b + e, 2, be) == 0x0112) {
					v = jp_num(b + e + 8, 2, be);
					return v >= 1 && v <= 8 ? (int)v : 1;
				}
			}
			return 1;
		}
		i += 2 + len;
	}
	return 1;
}

/* Turn a decoded picture the way its orientation says, so a photo a phone
   stored on its side is shown the right way up. */
void jp_turn(image *im, int o)
{
	int sw = im->w, sh = im->h, w = sw, h = sh, x, y, sx, sy;
	unsigned char *d, *s = im->px;

	if (o <= 1 || o > 8)
		return;
	if (o >= 5) {
		w = sh;
		h = sw;
	}
	d = xm((size_t)w * h * 3);
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			switch (o) {
			case 2: sx = sw - 1 - x; sy = y; break;
			case 3: sx = sw - 1 - x; sy = sh - 1 - y; break;
			case 4: sx = x; sy = sh - 1 - y; break;
			case 5: sx = y; sy = x; break;
			case 6: sx = y; sy = sh - 1 - x; break;
			case 7: sx = sw - 1 - y; sy = sh - 1 - x; break;
			default: sx = sw - 1 - y; sy = x; break;
			}
			memcpy(d + ((size_t)y * w + x) * 3, s + ((size_t)sy * sw + sx) * 3, 3);
		}
	}
	free(s);
	im->px = d;
	im->w = w;
	im->h = h;
}

/* Decode a JPEG held in memory to 8-bit RGB, turned by its orientation. */
int im_jpegmem(const unsigned char *b, size_t n, image *out, str *err)
{
	void *h;
	int w = 0, ht = 0, sub = 0, cs = 0;

	memset(out, 0, sizeof *out);
	if (jp_load(err) != HIBR_OK)
		return HIBR_FAIL;
	h = jp.init();
	if (!h) {
		s_cat(err, "libturbojpeg would not start");
		return HIBR_FAIL;
	}
	if (jp.header(h, b, (unsigned long)n, &w, &ht, &sub, &cs) < 0 || w < 1 ||
	    ht < 1) {
		s_cat(err, "not a JPEG it can read");
		if (jp.error) {
			s_cat(err, ": ");
			s_cat(err, jp.error(h));
		}
		jp.destroy(h);
		return HIBR_FAIL;
	}
	if ((long)w * ht > JP_MAXPX) {
		s_cat(err, "larger than an image is let be");
		jp.destroy(h);
		return HIBR_FAIL;
	}
	out->px = xm((size_t)w * ht * 3);
	if (jp.decode(h, b, (unsigned long)n, out->px, w, 0, ht, JP_RGB, 0) < 0) {
		s_cat(err, "could not decode it");
		if (jp.error) {
			s_cat(err, ": ");
			s_cat(err, jp.error(h));
		}
		free(out->px);
		out->px = 0;
		jp.destroy(h);
		return HIBR_FAIL;
	}
	jp.destroy(h);
	out->w = w;
	out->h = ht;
	jp_turn(out, jp_orient(b, n));
	return HIBR_OK;
}

/* Decode a JPEG file. */
int im_jpegload(const char *path, image *out, str *err)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	long n;
	int rc;

	memset(out, 0, sizeof *out);
	if (!f) {
		s_cat(err, "cannot open ");
		s_cat(err, path);
		return HIBR_FAIL;
	}
	if (fseek(f, 0, SEEK_END) < 0 || (n = ftell(f)) <= 0 || n > JP_MAXFILE ||
	    fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		s_cat(err, path);
		s_cat(err, ": empty, unreadable, or too large");
		return HIBR_FAIL;
	}
	b = xm((size_t)n);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(b);
		s_cat(err, path);
		s_cat(err, ": could not be read");
		return HIBR_FAIL;
	}
	fclose(f);
	rc = im_jpegmem(b, (size_t)n, out, err);
	if (rc != HIBR_OK) {
		str e;

		s_init(&e);
		s_cat(&e, path);
		s_cat(&e, ": ");
		s_cat(&e, err->p ? err->p : "");
		err->n = 0;
		s_cat(err, e.p);
		s_free(&e);
	}
	free(b);
	return rc;
}
