#define _GNU_SOURCE

#include "im.h"
#include <dlfcn.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* libpng dlopen'd on first use, the same shape src/net.c already loads
   libssl in -- present is enough, and a shell that never opens an image
   never pays for it. Reached through png_read_png's own "transforms" mask
   rather than the row-by-row API, which cuts the symbol list roughly in
   half: one call normalises 16-bit, palette, low-bit-depth grey and grey
   itself all down to 8-bit RGB(A), instead of one dlsym per transform. */

#define PNG_TRANSFORM_STRIP_16 0x0001
#define PNG_TRANSFORM_PACKING 0x0004
#define PNG_TRANSFORM_EXPAND 0x0010
#define PNG_TRANSFORM_GRAY_TO_RGB 0x2000

typedef void png_struct;
typedef void png_info;
typedef void (*png_longjmp_ptr)(jmp_buf, int);

struct pngapi {
	const char *(*ver)(const void *);
	png_struct *(*create_read)(const char *, void *, void *, void *);
	png_info *(*create_info)(png_struct *);
	void (*destroy_read)(png_struct **, png_info **, png_info **);
	jmp_buf *(*set_longjmp)(png_struct *, png_longjmp_ptr, size_t);
	void (*init_io)(png_struct *, FILE *);
	void (*read_png)(png_struct *, png_info *, int, void *);
	unsigned (*get_width)(const png_struct *, const png_info *);
	unsigned (*get_height)(const png_struct *, const png_info *);
	unsigned char (*get_channels)(const png_struct *, const png_info *);
	unsigned char **(*get_rows)(const png_struct *, const png_info *);
};

static struct pngapi png;
static void *png_lib;
static int png_tried;

/* Bind one symbol from libpng. */
static void *png_sym(void *h, const char *nm, int *bad)
{
	void *p = dlsym(h, nm);

	if (!p) {
		lg(HIBR_LERR, "libpng has no %s", nm);
		*bad = 1;
	}
	return p;
}

/* Load libpng on first use. Tried once: a broken or missing install does
   not get rediscovered on the next call, and dt_wall calls this once per
   frame once a wallpaper is set -- without the latch, a missing libpng
   means three failed dlopens and a log line every single frame, forever,
   rather than the one line this was meant to be. */
static int png_load(void)
{
	int bad = 0;
#ifdef __APPLE__
	const char *names[] = {
		"/opt/homebrew/opt/libpng/lib/libpng16.dylib",
		"/usr/local/opt/libpng/lib/libpng16.dylib",
		"libpng16.dylib", "libpng.dylib", 0
	};
#else
	const char *names[] = { "libpng16.so.16", "libpng.so.16",
				 "libpng.so", 0 };
#endif
	int i;

	if (png_lib)
		return HIBR_OK;
	if (png_tried)
		return HIBR_FAIL;
	png_tried = 1;
	for (i = 0; names[i]; i++) {
		png_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (png_lib)
			break;
	}
	if (!png_lib) {
		lg(HIBR_LERR, "cannot load libpng: %s", dlerror());
		return HIBR_FAIL;
	}
	png.ver = png_sym(png_lib, "png_get_libpng_ver", &bad);
	png.create_read = png_sym(png_lib, "png_create_read_struct", &bad);
	png.create_info = png_sym(png_lib, "png_create_info_struct", &bad);
	png.destroy_read = png_sym(png_lib, "png_destroy_read_struct", &bad);
	png.set_longjmp = png_sym(png_lib, "png_set_longjmp_fn", &bad);
	png.init_io = png_sym(png_lib, "png_init_io", &bad);
	png.read_png = png_sym(png_lib, "png_read_png", &bad);
	png.get_width = png_sym(png_lib, "png_get_image_width", &bad);
	png.get_height = png_sym(png_lib, "png_get_image_height", &bad);
	png.get_channels = png_sym(png_lib, "png_get_channels", &bad);
	png.get_rows = png_sym(png_lib, "png_get_rows", &bad);
	if (bad)
		return HIBR_FAIL;
	lg(HIBR_LDBG, "libpng loaded on demand");
	return HIBR_OK;
}

/* Decode a PNG from an open stream into 8-bit RGB, dropping any alpha;
   name is what an error calls it. user_png_ver is asked of the library
   itself rather than assumed, since there is no compile-time png.h here to
   have baked one in -- passing back what it just told us always
   "matches". */
int im_pngfile(FILE *fp, const char *name, image *out, str *err)
{
	png_struct *p;
	png_info *info;
	jmp_buf *jb;
	unsigned w, h;
	unsigned char ch;
	unsigned char **rows;
	unsigned r, c;
	const char *path = name;

	if (png_load() != HIBR_OK) {
		s_cat(err, "libpng is not available");
		return HIBR_FAIL;
	}
	p = png.create_read(png.ver(0), 0, 0, 0);
	if (!p) {
		s_cat(err, "png_create_read_struct failed");
		return HIBR_FAIL;
	}
	info = png.create_info(p);
	if (!info) {
		png.destroy_read(&p, 0, 0);
		s_cat(err, "png_create_info_struct failed");
		return HIBR_FAIL;
	}
	jb = png.set_longjmp(p, longjmp, sizeof(jmp_buf));
	if (setjmp(*jb)) {
		png.destroy_read(&p, &info, 0);
		s_cat(err, path);
		s_cat(err, " is not a valid PNG");
		return HIBR_FAIL;
	}
	png.init_io(p, fp);
	png.read_png(p, info, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING |
			      PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_GRAY_TO_RGB,
		     0);
	w = png.get_width(p, info);
	h = png.get_height(p, info);
	ch = png.get_channels(p, info);
	rows = png.get_rows(p, info);
	if (w < 1 || h < 1 || (ch != 3 && ch != 4)) {
		png.destroy_read(&p, &info, 0);
		s_cat(err, path);
		s_cat(err, " decoded to an unsupported shape");
		return HIBR_FAIL;
	}
	out->w = (int)w;
	out->h = (int)h;
	out->px = xm((size_t)w * (size_t)h * 3);
	for (r = 0; r < h; r++) {
		unsigned char *src = rows[r];
		unsigned char *dst = out->px + (size_t)r * w * 3;

		for (c = 0; c < w; c++) {
			dst[c * 3 + 0] = src[c * ch + 0];
			dst[c * 3 + 1] = src[c * ch + 1];
			dst[c * 3 + 2] = src[c * ch + 2];
		}
	}
	png.destroy_read(&p, &info, 0);
	return HIBR_OK;
}

/* Decode a PNG file. */
int im_pngload(const char *path, image *out, str *err)
{
	FILE *fp = fopen(path, "rb");
	int r;

	if (!fp) {
		s_cat(err, "cannot open ");
		s_cat(err, path);
		return HIBR_FAIL;
	}
	r = im_pngfile(fp, path, out, err);
	fclose(fp);
	return r;
}

/* Decode a PNG held in memory. */
int im_pngmem(const unsigned char *b, size_t n, image *out, str *err)
{
	FILE *fp = fmemopen((void *)b, n, "rb");
	int r;

	if (!fp) {
		s_cat(err, "cannot read the image from memory");
		return HIBR_FAIL;
	}
	r = im_pngfile(fp, "the image", out, err);
	fclose(fp);
	return r;
}

/* Resample into exactly w by h pixels, averaging every source pixel a target
   one covers -- the same box filter im_resample uses for cells, which is why
   a photograph scaled down looks blurred rather than speckled. */
void im_scale(const image *im, unsigned char *out, int w, int h)
{
	int x, y, c;

	if (!im || !im->px || w < 1 || h < 1)
		return;
	for (y = 0; y < h; y++) {
		int y0 = (int)((long long)y * im->h / h);
		int y1 = (int)((long long)(y + 1) * im->h / h);

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > im->h)
			y1 = im->h;
		for (x = 0; x < w; x++) {
			int x0 = (int)((long long)x * im->w / w);
			int x1 = (int)((long long)(x + 1) * im->w / w);
			unsigned long sum[3] = { 0, 0, 0 };
			unsigned long n = 0;
			int sx, sy;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > im->w)
				x1 = im->w;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++) {
					const unsigned char *p =
						im->px + ((size_t)sy * im->w + sx) * 3;

					for (c = 0; c < 3; c++)
						sum[c] += p[c];
					n++;
				}
			for (c = 0; c < 3; c++)
				out[((size_t)y * w + x) * 3 + c] =
					(unsigned char)(n ? sum[c] / n : 0);
		}
	}
}

/* A mode's own name, as -m takes it. */
int im_mode(const char *t)
{
	if (!t)
		return IM_AUTO;
	if (!strcmp(t, "half") || !strcmp(t, "halfblock"))
		return IM_HALF;
	if (!strcmp(t, "mono") || !strcmp(t, "grey") || !strcmp(t, "gray"))
		return IM_MONO;
	if (!strcmp(t, "ascii") || !strcmp(t, "text"))
		return IM_ASCII;
	if (!strcmp(t, "sixel") || !strcmp(t, "pixels"))
		return IM_SIXEL;
	return IM_AUTO;
}
