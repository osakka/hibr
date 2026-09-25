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

/* Load libpng on first use. */
static int png_load(void)
{
	int bad = 0;
	const char *names[] = { "libpng16.so.16", "libpng.so.16",
				 "libpng.so", 0 };
	int i;

	if (png_lib)
		return HIBR_OK;
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

/* Decode a PNG file into 8-bit RGB, dropping any alpha. user_png_ver is
   asked of the library itself rather than assumed, since there is no
   compile-time png.h here to have baked one in -- passing back what it
   just told us always "matches". */
int im_pngload(const char *path, image *out, str *err)
{
	FILE *fp;
	png_struct *p;
	png_info *info;
	jmp_buf *jb;
	unsigned w, h;
	unsigned char ch;
	unsigned char **rows;
	unsigned r, c;

	if (png_load() != HIBR_OK) {
		s_cat(err, "libpng is not available");
		return HIBR_FAIL;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		s_cat(err, "cannot open ");
		s_cat(err, path);
		return HIBR_FAIL;
	}
	p = png.create_read(png.ver(0), 0, 0, 0);
	if (!p) {
		fclose(fp);
		s_cat(err, "png_create_read_struct failed");
		return HIBR_FAIL;
	}
	info = png.create_info(p);
	if (!info) {
		png.destroy_read(&p, 0, 0);
		fclose(fp);
		s_cat(err, "png_create_info_struct failed");
		return HIBR_FAIL;
	}
	jb = png.set_longjmp(p, longjmp, sizeof(jmp_buf));
	if (setjmp(*jb)) {
		png.destroy_read(&p, &info, 0);
		fclose(fp);
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
		fclose(fp);
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
	fclose(fp);
	return HIBR_OK;
}
