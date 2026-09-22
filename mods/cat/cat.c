#define _GNU_SOURCE

#include "ct.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CT_CH
#define CT_CH 65536
#endif

/* Write a whole buffer, retrying a short or interrupted write. */
int ct_wr(int fd, const char *p, size_t n)
{
	ssize_t k;

	while (n) {
		k = write(fd, p, n);
		if (k > 0) {
			p += (size_t)k;
			n -= (size_t)k;
			continue;
		}
		if (k < 0 && errno == EINTR)
			continue;
		return HIBR_FAIL;
	}
	return HIBR_OK;
}

/* Copy a descriptor to standard output without looking at what is in it. */
int ct_raw(int fd, const char *nm)
{
	char *b = xm(CT_CH);
	ssize_t k;
	int r = HIBR_OK;

	for (;;) {
		k = read(fd, b, CT_CH);
		if (k == 0)
			break;
		if (k < 0) {
			if (errno == EINTR)
				continue;
			lg(HIBR_LERR, "cat: %s: %s", nm, strerror(errno));
			r = HIBR_FAIL;
			break;
		}
		if (ct_wr(1, b, (size_t)k) != HIBR_OK) {
			r = HIBR_FAIL;
			break;
		}
	}
	free(b);
	return r;
}

/* Append one byte the way GNU cat's -v spells it. */
void ct_vis(str *o, unsigned char c, unsigned f)
{
	if (c == '\t') {
		if (f & CT_TABS)
			s_cat(o, "^I");
		else
			s_ch(o, '\t');
		return;
	}
	if (c == '\n' || !(f & CT_NONPRINT)) {
		s_ch(o, (char)c);
		return;
	}
	if (c >= 128) {
		s_cat(o, "M-");
		c = (unsigned char)(c - 128);
	}
	if (c < 32) {
		s_ch(o, '^');
		s_ch(o, (char)(c + 64));
		return;
	}
	if (c == 127) {
		s_cat(o, "^?");
		return;
	}
	s_ch(o, (char)c);
}

/* True when a block looks like something that should not reach a terminal. */
int ct_binary(const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (!b[i])
			return 1;
	return 0;
}

/* Number a line the way cat has always numbered it. */
void ct_number(str *o, long n, unsigned f)
{
	str t;
	size_t i;

	if (f & CT_COLOUR)
		s_cat(o, "\033[38;5;240m");
	s_init(&t);
	s_num(&t, n);
	for (i = t.n; i < 6; i++)
		s_ch(o, ' ');
	s_cat(o, t.p ? t.p : "");
	if (f & CT_COLOUR)
		s_cat(o, "\033[0m \342\224\202 ");
	else
		s_ch(o, '\t');
	s_free(&t);
}

/* Copy a descriptor, applying the options that need the content looked at. */
int ct_cook(int fd, ct_opt *o)
{
	str in, out;
	char *b = xm(CT_CH);
	ssize_t k;
	size_t st = 0, i;
	int r = HIBR_OK, first = 1, bin = 0;
	const char *lang = o->tty ? ct_lang(o->name) : 0;

	o->blk = CT_BLK_NONE;

	s_init(&in);
	s_init(&out);
	for (;;) {
		k = read(fd, b, CT_CH);
		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0) {
			lg(HIBR_LERR, "cat: %s: %s", o->name, strerror(errno));
			r = HIBR_FAIL;
			break;
		}
		if (k > 0) {
			if (first && o->tty && !(o->f & CT_FORCE) &&
			    ct_binary(b, (size_t)k)) {
				bin = 1;
				break;
			}
			first = 0;
			s_add(&in, b, (size_t)k);
		}
		st = 0;
		for (i = 0; i < in.n; i++) {
			if (in.p[i] != '\n')
				continue;
			ct_line(&out, in.p + st, i - st, o, lang);
			st = i + 1;
		}
		if (st) {
			memmove(in.p, in.p + st, in.n - st);
			in.n -= st;
		}
		if (out.n) {
			if (ct_wr(1, out.p, out.n) != HIBR_OK) {
				r = HIBR_FAIL;
				break;
			}
			out.n = 0;
		}
		if (k == 0)
			break;
	}
	if (bin) {
		struct stat sb;
		str m;
		s_init(&m);
		s_cat(&m, "cat: ");
		s_cat(&m, o->name);
		s_cat(&m, ": binary file");
		if (fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode)) {
			s_cat(&m, ", ");
			s_num(&m, (long)sb.st_size);
			s_cat(&m, " bytes");
		}
		s_cat(&m, " (use -f to show it)\n");
		ct_wr(2, m.p, m.n);
		s_free(&m);
		r = HIBR_FAIL;
	} else if (in.n) {
		ct_line(&out, in.p, in.n, o, lang);
		if (out.n >= 1 && out.p[out.n - 1] == '\n')
			out.n--;
		if (out.n && ct_wr(1, out.p, out.n) != HIBR_OK)
			r = HIBR_FAIL;
	}
	free(b);
	s_free(&in);
	s_free(&out);
	return r;
}

/* Render one line, with whatever of the options apply to it. */
void ct_line(str *out, const char *p, size_t n, ct_opt *o, const char *lang)
{
	size_t i;
	int blank = n == 0;

	if ((o->f & CT_SQUEEZE) && blank && o->blank) {
		o->blank++;
		return;
	}
	o->blank = blank ? o->blank + 1 : 0;
	if (o->f & CT_NUMNB) {
		if (!blank)
			ct_number(out, ++o->n, o->f);
	} else if (o->f & CT_NUM) {
		ct_number(out, ++o->n, o->f);
	}
	o->col = 0;
	if (o->f & CT_COLOUR)
		ct_hl(out, p, n, lang, o);
	else if (o->f & (CT_NONPRINT | CT_TABS))
		for (i = 0; i < n; i++)
			ct_vis(out, (unsigned char)p[i], o->f);
	else
		s_add(out, p, n);
	if (o->f & CT_ENDS)
		s_ch(out, '$');
	s_ch(out, '\n');
}

/* Decide whether anything about this run needs the content inspected. */
int ct_plainish(const ct_opt *o)
{
	return !(o->f & (CT_NUM | CT_NUMNB | CT_SQUEEZE | CT_ENDS | CT_TABS |
			 CT_NONPRINT | CT_COLOUR));
}

/* Show files, or standard input, on standard output. */
int m_cat(sh *s, int ac, char **av)
{
	ct_opt o;
	int i, fd, r = HIBR_OK, files = 0, endopt = 0;
	struct stat sb;

	(void)s;
	memset(&o, 0, sizeof o);
	o.tty = isatty(1);
	o.tabw = 8;
	for (i = 1; i < ac; i++) {
		const char *a = av[i];
		if (endopt || a[0] != '-' || !a[1]) {
			files++;
			continue;
		}
		if (!strcmp(a, "--")) {
			endopt = 1;
			continue;
		}
		if (!strcmp(a, "--help")) {
			printf("usage: cat [-benstuvAETfp] [file...]\n");
			return HIBR_OK;
		}
		for (a++; *a; a++)
			switch (*a) {
			case 'n': o.f |= CT_NUM; break;
			case 'b': o.f |= CT_NUMNB; break;
			case 's': o.f |= CT_SQUEEZE; break;
			case 'E': o.f |= CT_ENDS; break;
			case 'T': o.f |= CT_TABS; break;
			case 'v': o.f |= CT_NONPRINT; break;
			case 'e': o.f |= CT_NONPRINT | CT_ENDS; break;
			case 't': o.f |= CT_NONPRINT | CT_TABS; break;
			case 'A': o.f |= CT_NONPRINT | CT_ENDS | CT_TABS; break;
			case 'u': break;
			case 'f': o.f |= CT_FORCE; break;
			case 'p': o.f |= CT_PLAIN; break;
			default:
				lg(HIBR_LERR, "cat: -%c: unknown option", *a);
				return 2;
			}
	}
	if (o.f & CT_NUMNB)
		o.f &= ~(unsigned)CT_NUM;
	if (o.tty && !(o.f & CT_PLAIN)) {
		o.f |= CT_COLOUR;
		if (!(o.f & CT_NUMNB))
			o.f |= CT_NUM;
	}
	endopt = 0;
	for (i = 1; i < ac; i++) {
		const char *a = av[i];
		if (!endopt && !strcmp(a, "--")) {
			endopt = 1;
			continue;
		}
		if (!endopt && a[0] == '-' && a[1])
			continue;
		o.name = !strcmp(a, "-") ? "standard input" : a;
		if (!strcmp(a, "-")) {
			fd = 0;
		} else {
			fd = open(a, O_RDONLY);
			if (fd < 0) {
				lg(HIBR_LERR, "cat: %s: %s", a, strerror(errno));
				r = HIBR_FAIL;
				continue;
			}
			if (fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
				lg(HIBR_LERR, "cat: %s: Is a directory", a);
				close(fd);
				r = HIBR_FAIL;
				continue;
			}
		}
		if (ct_plainish(&o)) {
			if (ct_raw(fd, o.name) != HIBR_OK)
				r = HIBR_FAIL;
		} else if (ct_cook(fd, &o) != HIBR_OK) {
			r = HIBR_FAIL;
		}
		if (fd)
			close(fd);
	}
	if (!files) {
		o.name = "standard input";
		if (ct_plainish(&o)) {
			if (ct_raw(0, o.name) != HIBR_OK)
				r = HIBR_FAIL;
		} else if (ct_cook(0, &o) != HIBR_OK) {
			r = HIBR_FAIL;
		}
	}
	return r;
}

const hibr_bi cat_bi[] = {
	{ "cat", m_cat, "show files, with a gutter and colour on a terminal" },
	HIBR_BI_END
};

HIBR_MODULE_P("cat", "0.21", "cat that is plain in a pipe and useful on a tty",
	      cat_bi, ct_ini, ct_fini, "highlight");
