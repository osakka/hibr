#include "pri.h"
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

extern const hibr_bi bitab[];
extern volatile sig_atomic_t g_int;

struct termios ed_saved;
int ed_raw;

typedef struct est est;
struct est { const char *ps; int orows, orow; };

struct rng { unsigned lo, hi; };

const struct rng u8zero[] = {
	{ 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD },
	{ 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 }, { 0x05C4, 0x05C5 },
	{ 0x05C7, 0x05C7 }, { 0x0610, 0x061A }, { 0x064B, 0x065F },
	{ 0x0670, 0x0670 }, { 0x06D6, 0x06DC }, { 0x06DF, 0x06E4 },
	{ 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED }, { 0x0711, 0x0711 },
	{ 0x0730, 0x074A }, { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 },
	{ 0x0816, 0x0819 }, { 0x081B, 0x0823 }, { 0x0825, 0x0827 },
	{ 0x0829, 0x082D }, { 0x0859, 0x085B }, { 0x08D3, 0x08FF },
	{ 0x093A, 0x093A }, { 0x093C, 0x093C }, { 0x0941, 0x0948 },
	{ 0x094D, 0x094D }, { 0x0951, 0x0957 }, { 0x1AB0, 0x1AFF },
	{ 0x1DC0, 0x1DFF }, { 0x200B, 0x200F }, { 0x20D0, 0x20F0 },
	{ 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F }, { 0xFEFF, 0xFEFF }
};

const struct rng u8wide[] = {
	{ 0x1100, 0x115F }, { 0x2E80, 0x303E }, { 0x3041, 0x33FF },
	{ 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA4CF },
	{ 0xA960, 0xA97F }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
	{ 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 },
	{ 0xFFE0, 0xFFE6 }, { 0x1F300, 0x1F64F }, { 0x1F900, 0x1F9FF },
	{ 0x20000, 0x3FFFD }
};

/* Report the byte length announced by a UTF-8 lead byte. */
int u8len(unsigned char c)
{
	if (c < 0x80)
		return 1;
	if ((c & 0xE0) == 0xC0)
		return 2;
	if ((c & 0xF0) == 0xE0)
		return 3;
	if ((c & 0xF8) == 0xF0)
		return 4;
	return 1;
}

/* Decode one UTF-8 character, returning the bytes consumed. */
int u8dec(const char *p, size_t n, unsigned *cp)
{
	unsigned char c = (unsigned char)p[0];
	int l = u8len(c), i;

	if (l == 1 || (size_t)l > n) {
		*cp = c;
		return 1;
	}
	*cp = c & (unsigned)(0xFF >> (l + 1));
	for (i = 1; i < l; i++) {
		if (((unsigned char)p[i] & 0xC0) != 0x80) {
			*cp = c;
			return 1;
		}
		*cp = (*cp << 6) | ((unsigned char)p[i] & 0x3F);
	}
	return l;
}

/* True if a codepoint falls inside a sorted range table. */
int u8in(const struct rng *t, size_t n, unsigned c)
{
	size_t lo = 0, hi = n, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (c < t[mid].lo)
			hi = mid;
		else if (c > t[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

/* Report how many terminal columns a codepoint occupies. */
int u8w(unsigned c)
{
	if (c < 32 || (c >= 0x7F && c < 0xA0))
		return 0;
	if (c >= 0x0300 &&
	    u8in(u8zero, sizeof u8zero / sizeof u8zero[0], c))
		return 0;
	if (c >= 0x1100 &&
	    u8in(u8wide, sizeof u8wide / sizeof u8wide[0], c))
		return 2;
	return 1;
}

/* Step back one character, taking any combining marks with it. */
size_t u8prev(str *b, size_t pos)
{
	unsigned cp;
	size_t q;

	while (pos) {
		q = pos - 1;
		while (q && ((unsigned char)b->p[q] & 0xC0) == 0x80)
			q--;
		u8dec(b->p + q, pos - q, &cp);
		pos = q;
		if (u8w(cp))
			break;
	}
	return pos;
}

/* Step forward one character, taking any combining marks with it. */
size_t u8next(str *b, size_t pos)
{
	unsigned cp;
	int l;

	if (pos >= b->n)
		return b->n;
	pos += (size_t)u8dec(b->p + pos, b->n - pos, &cp);
	while (pos < b->n) {
		l = u8dec(b->p + pos, b->n - pos, &cp);
		if (u8w(cp))
			break;
		pos += (size_t)l;
	}
	return pos;
}

/* Put the terminal into character at a time mode. */
int ed_on(void)
{
	struct termios t;

	if (tcgetattr(0, &ed_saved) < 0)
		return HIBR_FAIL;
	t = ed_saved;
	t.c_lflag &= ~(unsigned)(ICANON | ECHO | IEXTEN);
	t.c_iflag &= ~(unsigned)(IXON | ICRNL);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSADRAIN, &t) < 0)
		return HIBR_FAIL;
	ed_raw = 1;
	return HIBR_OK;
}

/* Restore the terminal settings. */
void ed_off(void)
{
	if (ed_raw)
		tcsetattr(0, TCSADRAIN, &ed_saved);
	ed_raw = 0;
}

/* Write a buffer straight to the terminal. */
void ed_put(const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(2, p, n);
		if (w <= 0)
			return;
		p += w;
		n -= (size_t)w;
	}
}

/* Report the terminal width, falling back to eighty columns. */
int ed_cols(void)
{
	struct winsize w;

	if (ioctl(2, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
		return (int)w.ws_col;
	return 80;
}

/* Lay the prompt and buffer out in rows, reporting the cursor cell. */
void ed_pos(const char *ps, str *b, size_t pos, int cols, int *rows, int *crow,
	    int *ccol, int *ecol)
{
	const char *p = ps;
	size_t i = 0, pn = strlen(ps);
	unsigned cp;
	int row = 0, col = 0, w, l;

	while (*p) {
		if (*p == 27) {
			p++;
			if (*p == '[' || *p == ']')
				p++;
			while (*p && !(*p >= '@' && *p <= '~'))
				p++;
			if (*p)
				p++;
			continue;
		}
		if (*p == '\n') {
			row++;
			col = 0;
			p++;
			continue;
		}
		l = u8dec(p, pn - (size_t)(p - ps), &cp);
		w = u8w(cp);
		if (col + w > cols) {
			row++;
			col = 0;
		}
		col += w;
		p += l;
	}
	*crow = row;
	*ccol = col;
	while (i < b->n) {
		l = u8dec(b->p + i, b->n - i, &cp);
		w = u8w(cp);
		if (col + w > cols) {
			row++;
			col = 0;
		}
		if (i == pos) {
			*crow = row;
			*ccol = col;
		}
		col += w;
		i += (size_t)l;
	}
	if (pos >= b->n) {
		*crow = row;
		*ccol = col;
	}
	*rows = row + 1;
	*ecol = col;
}

/* Repaint a line that may wrap over several rows, then place the cursor. */
void ed_draw(est *e, str *b, size_t pos)
{
	int cols = ed_cols();
	str o;
	int rows, crow, ccol, ecol, i;

	ed_pos(e->ps, b, pos, cols, &rows, &crow, &ccol, &ecol);
	s_init(&o);
	if (e->orows - 1 - e->orow > 0) {
		s_cat(&o, "\033[");
		s_num(&o, e->orows - 1 - e->orow);
		s_ch(&o, 'B');
	}
	for (i = 0; i < e->orows - 1; i++)
		s_cat(&o, "\r\033[0K\033[1A");
	s_cat(&o, "\r\033[0K");
	s_cat(&o, e->ps);
	s_add(&o, b->p ? b->p : "", b->n);
	if (pos >= b->n && ecol >= cols) {
		s_cat(&o, "\n\r");
		rows++;
		crow = rows - 1;
		ccol = 0;
	}
	if (rows - 1 - crow > 0) {
		s_cat(&o, "\033[");
		s_num(&o, rows - 1 - crow);
		s_ch(&o, 'A');
	}
	s_ch(&o, '\r');
	if (ccol) {
		s_cat(&o, "\033[");
		s_num(&o, ccol);
		s_ch(&o, 'C');
	}
	ed_put(o.p, o.n);
	s_free(&o);
	e->orows = rows;
	e->orow = crow;
	lg(HIBR_LTRC, "draw %d rows, cursor row %d col %d", rows, crow, ccol);
}

/* Forget the painted layout because the cursor moved to a fresh line. */
void ed_fresh(est *e)
{
	e->orows = 1;
	e->orow = 0;
}

/* Replace the edit buffer with a history entry. */
void ed_load(str *b, size_t *pos, const char *t)
{
	b->n = 0;
	if (b->p)
		b->p[0] = 0;
	s_cat(b, t ? t : "");
	*pos = b->n;
}

/* Append a line to the history, skipping blanks and repeats. */
void hs_add(sh *s, const char *line)
{
	const char *p = line;
	size_t i;

	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p || *p == '\n')
		return;
	if (s->hist.n && !strcmp((char *)s->hist.p[s->hist.n - 1], line))
		return;
	v_add(&s->hist, xs(line));
	if (s->hist.n > HIBR_HIST) {
		free(s->hist.p[0]);
		for (i = 1; i < s->hist.n; i++)
			s->hist.p[i - 1] = s->hist.p[i];
		s->hist.n--;
	}
}

/* Work out where the history file lives. */
char *hs_path(sh *s)
{
	const char *h = hibr_get(s, "HIBR_HISTFILE");
	const char *home;
	str b;

	if (h && *h)
		return xs(h);
	home = hibr_get(s, "HOME");
	if (!home || !*home)
		return 0;
	s_init(&b);
	s_cat(&b, home);
	s_cat(&b, "/.hibr_history");
	return b.p;
}

/* Load the saved history. */
int ed_init(sh *s)
{
	FILE *f;
	char *line;

	s->hfile = hs_path(s);
	if (!s->hfile)
		return HIBR_OK;
	f = fopen(s->hfile, "r");
	if (!f) {
		lg(HIBR_LDBG, "no history at %s", s->hfile);
		return HIBR_OK;
	}
	while ((line = rdline(f))) {
		size_t n = strlen(line);
		if (n && line[n - 1] == '\n')
			line[n - 1] = 0;
		hs_add(s, line);
		free(line);
	}
	fclose(f);
	lg(HIBR_LDBG, "loaded %lu history lines", (unsigned long)s->hist.n);
	return HIBR_OK;
}

/* Save the history and drop it. */
void ed_fini(sh *s)
{
	FILE *f;
	size_t i;

	ed_off();
	if (s->hfile && s->hist.n) {
		f = fopen(s->hfile, "w");
		if (f) {
			for (i = 0; i < s->hist.n; i++)
				fprintf(f, "%s\n", (char *)s->hist.p[i]);
			fclose(f);
		} else {
			lg(HIBR_LWRN, "cannot write %s: %s", s->hfile,
			   strerror(errno));
		}
	}
	for (i = 0; i < s->hist.n; i++)
		free(s->hist.p[i]);
	v_free(&s->hist);
	free(s->hfile);
	s->hfile = 0;
}

/* Add a candidate if it carries the prefix. */
void ed_cand1(vec *out, const char *nm, const char *pre, size_t pn)
{
	size_t i;

	if (strncmp(nm, pre, pn))
		return;
	for (i = 0; i < out->n; i++)
		if (!strcmp((char *)out->p[i], nm))
			return;
	v_add(out, xs(nm));
}

/* Collect executables on PATH that carry the prefix. */
void ed_path(sh *s, const char *pre, size_t pn, vec *out)
{
	const char *p = hibr_get(s, "PATH");
	const char *q;
	DIR *d;
	struct dirent *de;
	str dir, full;
	struct stat st;

	if (!p)
		return;
	while (*p) {
		size_t n;
		q = strchr(p, ':');
		n = q ? (size_t)(q - p) : strlen(p);
		s_init(&dir);
		s_add(&dir, n ? p : ".", n ? n : 1);
		d = opendir(dir.p);
		if (d) {
			while ((de = readdir(d))) {
				if (strncmp(de->d_name, pre, pn))
					continue;
				if (de->d_name[0] == '.')
					continue;
				s_init(&full);
				s_add(&full, dir.p, dir.n);
				s_ch(&full, '/');
				s_cat(&full, de->d_name);
				if (!stat(full.p, &st) && (st.st_mode & 0111))
					ed_cand1(out, de->d_name, pre, pn);
				s_free(&full);
			}
			closedir(d);
		}
		s_free(&dir);
		if (!q)
			break;
		p = q + 1;
	}
}

/* Ask a user supplied function for candidates. */
int ed_hook(sh *s, const char *cmd, const char *word, vec *out)
{
	const char *word0 = word;
	char *key = (char *)cmd;
	const char *fn = v_getp(s, "COMPLETE", &key, 1);
	str c;
	vec *res;
	size_t i;

	if (!fn || !*fn || !fn_find(s, fn))
		return 0;
	s_init(&c);
	s_cat(&c, fn);
	s_cat(&c, " '");
	s_cat(&c, cmd);
	s_cat(&c, "' '");
	for (; *word; word++) {
		if (*word == '\'')
			s_cat(&c, "'\\''");
		else
			s_ch(&c, *word);
	}
	s_cat(&c, "'");
	hibr_run(s, c.p);
	s_free(&c);
	res = vb_get(s);
	v_list(s, "RET", 0, 0, res, 0);
	for (i = 0; i < res->n; i++)
		if (*(char *)res->p[i])
			ed_cand1(out, (char *)res->p[i], word0, strlen(word0));
	vb_put(s, res);
	lg(HIBR_LTRC, "completion hook %s gave %lu", fn, (unsigned long)out->n);
	return 1;
}

/* Collect completion candidates for a partial word. */
void ed_cand(sh *s, str *b, size_t start, const char *word, vec *out, str *dir)
{
	const char *sl = strrchr(word, '/');
	const char *base = sl ? sl + 1 : word;
	size_t bn = strlen(base), i;
	const hibr_bi *bi;
	DIR *d;
	struct dirent *de;
	str full, cmd;
	node *f;
	var *v;
	int iscmd = 1;

	s_init(dir);
	for (i = 0; i < start; i++)
		if (b->p[i] != ' ' && b->p[i] != '\t' && b->p[i] != ';' &&
		    b->p[i] != '|' && b->p[i] != '&')
			iscmd = 0;
	if (word[0] == '$') {
		for (i = 0; i < s->tsz; i++)
			for (v = s->tab[i]; v; v = v->nx) {
				s_init(&full);
				s_ch(&full, '$');
				s_cat(&full, v->k);
				ed_cand1(out, full.p, word, strlen(word));
				s_free(&full);
			}
		return;
	}
	if (!iscmd) {
		size_t cs = 0, ce;
		while (cs < b->n && (b->p[cs] == ' ' || b->p[cs] == '\t'))
			cs++;
		ce = cs;
		while (ce < b->n && b->p[ce] != ' ' && b->p[ce] != '\t')
			ce++;
		s_init(&cmd);
		s_add(&cmd, b->p + cs, ce - cs);
		if (cmd.n && ed_hook(s, cmd.p, word, out)) {
			s_free(&cmd);
			return;
		}
		s_free(&cmd);
	}
	if (iscmd && !sl) {
		for (bi = bitab; bi->nm; bi++)
			ed_cand1(out, bi->nm, base, bn);
		for (i = 0; i < s->fns.n; i++) {
			f = (node *)s->fns.p[i];
			ed_cand1(out, f->s, base, bn);
		}
		ed_path(s, base, bn, out);
	}
	if (sl)
		s_add(dir, word, (size_t)(sl - word + 1));
	d = opendir(dir->n ? dir->p : ".");
	if (!d) {
		lg(HIBR_LTRC, "completion: cannot read %s", dir->n ? dir->p : ".");
		return;
	}
	while ((de = readdir(d))) {
		struct stat st;
		if (strncmp(de->d_name, base, bn))
			continue;
		if (de->d_name[0] == '.' && base[0] != '.')
			continue;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		s_init(&full);
		s_add(&full, dir->p ? dir->p : "", dir->n);
		s_cat(&full, de->d_name);
		{
			int isd = !stat(full.p, &st) && S_ISDIR(st.st_mode);
			s_free(&full);
			s_init(&full);
			s_cat(&full, de->d_name);
			if (isd)
				s_ch(&full, '/');
		}
		ed_cand1(out, full.p, base, bn);
		s_free(&full);
	}
	closedir(d);
}

/* Order completion candidates. */
int edcmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Extend the word under the cursor from the filesystem and builtins. */
void ed_tab(sh *s, est *e, str *b, size_t *pos)
{
	size_t st = *pos;
	vec c = { 0, 0, 0 };
	str dir, word, common;
	size_t i, k;

	while (st && b->p[st - 1] != ' ' && b->p[st - 1] != '\t')
		st--;
	s_init(&word);
	s_add(&word, b->p + st, *pos - st);
	ed_cand(s, b, st, word.p ? word.p : "", &c, &dir);
	if (!c.n) {
		s_free(&word);
		s_free(&dir);
		return;
	}
	if (c.n > 1)
		qsort(c.p, c.n, sizeof *c.p, edcmp);
	s_init(&common);
	s_cat(&common, (char *)c.p[0]);
	for (i = 1; i < c.n; i++) {
		char *o = (char *)c.p[i];
		for (k = 0; k < common.n && o[k] && o[k] == common.p[k]; k++)
			;
		common.n = k;
		common.p[k] = 0;
	}
	if (c.n > 1) {
		str o;
		s_init(&o);
		s_cat(&o, "\r\n");
		for (i = 0; i < c.n; i++) {
			s_cat(&o, (char *)c.p[i]);
			s_ch(&o, ' ');
		}
		s_cat(&o, "\r\n");
		ed_put(o.p, o.n);
		s_free(&o);
		ed_fresh(e);
	}
	{
		str nb;
		const char *sl = strrchr(word.p ? word.p : "", '/');
		size_t keep = sl ? (size_t)(sl - word.p + 1) : 0;
		s_init(&nb);
		s_add(&nb, b->p, st);
		s_add(&nb, word.p ? word.p : "", keep);
		s_add(&nb, common.p ? common.p : "", common.n);
		k = nb.n;
		if (c.n == 1 && common.n && common.p[common.n - 1] != '/') {
			s_ch(&nb, ' ');
			k = nb.n;
		}
		s_add(&nb, b->p + *pos, b->n - *pos);
		b->n = 0;
		if (b->p)
			b->p[0] = 0;
		s_add(b, nb.p ? nb.p : "", nb.n);
		*pos = k;
		s_free(&nb);
	}
	for (i = 0; i < c.n; i++)
		free(c.p[i]);
	v_free(&c);
	s_free(&common);
	s_free(&word);
	s_free(&dir);
	ed_draw(e, b, *pos);
}

/* Delete the word before the cursor. */
void ed_killw(str *b, size_t *pos)
{
	size_t e = *pos;

	while (*pos && (b->p[*pos - 1] == ' ' || b->p[*pos - 1] == '\t'))
		(*pos)--;
	while (*pos && b->p[*pos - 1] != ' ' && b->p[*pos - 1] != '\t')
		(*pos)--;
	memmove(b->p + *pos, b->p + e, b->n - e);
	b->n -= e - *pos;
	b->p[b->n] = 0;
}

/* Incremental reverse search through the history. */
int ed_search(sh *s, str *b, size_t *pos)
{
	str q, line;
	size_t hi = s->hist.n;
	size_t found = s->hist.n;
	char c;
	int rc = 0;

	s_init(&q);
	s_init(&line);
	for (;;) {
		size_t i;
		int hit = 0;
		for (i = hi; i-- > 0;) {
			if (!q.n || strstr((char *)s->hist.p[i], q.p)) {
				found = i;
				hit = 1;
				break;
			}
		}
		line.n = 0;
		s_cat(&line, "\r\033[K(reverse-i-search)`");
		s_add(&line, q.p ? q.p : "", q.n);
		s_cat(&line, hit ? "': " : "' (failed): ");
		if (hit)
			s_cat(&line, (char *)s->hist.p[found]);
		ed_put(line.p, line.n);
		if (read(0, &c, 1) != 1)
			break;
		if (c == 18) {
			if (hit && found)
				hi = found;
			continue;
		}
		if (c == 7 || c == 27) {
			rc = 0;
			goto out;
		}
		if (c == '\r' || c == '\n') {
			rc = 1;
			goto take;
		}
		if (c == 127 || c == 8) {
			if (q.n)
				q.p[--q.n] = 0;
			hi = s->hist.n;
			continue;
		}
		if ((unsigned char)c < 32)
			goto take;
		s_ch(&q, c);
		hi = s->hist.n;
	}
take:
	if (found < s->hist.n) {
		b->n = 0;
		if (b->p)
			b->p[0] = 0;
		s_cat(b, (char *)s->hist.p[found]);
		*pos = b->n;
	}
out:
	ed_put("\r\033[K", 4);
	s_free(&q);
	s_free(&line);
	lg(HIBR_LTRC, "history search %s", rc ? "accepted" : "left");
	return rc;
}

/* Finish a line: repaint, leave raw mode and hand back the text. */
char *ed_done(sh *s, est *e, str *b)
{
	char *r;

	ed_draw(e, b, b->n);
	ed_put("\r\n", 2);
	ed_off();
	hs_add(s, b->p ? b->p : "");
	s_ch(b, '\n');
	r = xs(b->p);
	return r;
}

/* Read one edited line, returning heap memory or NULL at end of input. */
char *ed_line(sh *s, const char *ps)
{
	str b, save;
	size_t pos = 0;
	size_t hi;
	est e;
	char c;
	int esc = 0;
	char *r;

	if (!s->it || !isatty(0)) {
		ed_put(ps, strlen(ps));
		return rdline(stdin);
	}
	if (ed_on() != HIBR_OK) {
		ed_put(ps, strlen(ps));
		return rdline(stdin);
	}
	s_init(&b);
	s_init(&save);
	hi = s->hist.n;
	e.ps = ps;
	ed_fresh(&e);
	ed_draw(&e, &b, pos);
	for (;;) {
		if (read(0, &c, 1) != 1) {
			if (errno == EINTR && g_int) {
				g_int = 0;
				ed_put("^C\r\n", 4);
				b.n = 0;
				if (b.p)
					b.p[0] = 0;
				pos = 0;
				ed_fresh(&e);
				ed_draw(&e, &b, pos);
				continue;
			}
			if (!b.n) {
				ed_off();
				s_free(&b);
				s_free(&save);
				return 0;
			}
			break;
		}
		if (esc == 1) {
			esc = c == '[' || c == 'O' ? 2 : 0;
			continue;
		}
		if (esc == 2) {
			esc = 0;
			if (c == 'A' || c == 'B') {
				if (hi == s->hist.n) {
					save.n = 0;
					s_add(&save, b.p ? b.p : "", b.n);
				}
				if (c == 'A' && hi)
					hi--;
				else if (c == 'B' && hi < s->hist.n)
					hi++;
				ed_load(&b, &pos,
					hi < s->hist.n ? (char *)s->hist.p[hi] :
							 save.p);
			} else if (c == 'C' && pos < b.n) {
				pos = u8next(&b, pos);
			} else if (c == 'D' && pos) {
				pos = u8prev(&b, pos);
			} else if (c == 'H') {
				pos = 0;
			} else if (c == 'F') {
				pos = b.n;
			} else if (c == '3') {
				if (read(0, &c, 1) == 1 && pos < b.n) {
					size_t k = u8next(&b, pos);
					memmove(b.p + pos, b.p + k, b.n - k);
					b.n -= k - pos;
					b.p[b.n] = 0;
				}
			}
			ed_draw(&e, &b, pos);
			continue;
		}
		switch (c) {
		case '\r':
		case '\n':
			r = ed_done(s, &e, &b);
			s_free(&b);
			s_free(&save);
			return r;
		case 18:
			if (ed_search(s, &b, &pos)) {
				r = ed_done(s, &e, &b);
				s_free(&b);
				s_free(&save);
				return r;
			}
			ed_fresh(&e);
			break;
		case 27:
			esc = 1;
			continue;
		case 9:
			ed_tab(s, &e, &b, &pos);
			continue;
		case 3:
			ed_put("^C\r\n", 4);
			ed_off();
			s_free(&b);
			s_free(&save);
			r = xs("\n");
			return r;
		case 4:
			if (!b.n) {
				ed_put("\r\n", 2);
				ed_off();
				s_free(&b);
				s_free(&save);
				return 0;
			}
			if (pos < b.n) {
				size_t k = u8next(&b, pos);
				memmove(b.p + pos, b.p + k, b.n - k);
				b.n -= k - pos;
				b.p[b.n] = 0;
			}
			break;
		case 1:
			pos = 0;
			break;
		case 5:
			pos = b.n;
			break;
		case 2:
			pos = u8prev(&b, pos);
			break;
		case 6:
			pos = u8next(&b, pos);
			break;
		case 11:
			b.n = pos;
			b.p[b.n] = 0;
			break;
		case 21:
			memmove(b.p, b.p + pos, b.n - pos);
			b.n -= pos;
			b.p[b.n] = 0;
			pos = 0;
			break;
		case 23:
			if (pos)
				ed_killw(&b, &pos);
			break;
		case 12:
			ed_put("\033[H\033[2J", 7);
			ed_fresh(&e);
			break;
		case 16:
			if (hi) {
				if (hi == s->hist.n) {
					save.n = 0;
					s_add(&save, b.p ? b.p : "", b.n);
				}
				hi--;
				ed_load(&b, &pos, (char *)s->hist.p[hi]);
			}
			break;
		case 14:
			if (hi < s->hist.n) {
				hi++;
				ed_load(&b, &pos,
					hi < s->hist.n ? (char *)s->hist.p[hi] :
							 save.p);
			}
			break;
		case 127:
		case 8:
			if (pos) {
				size_t k = u8prev(&b, pos);
				memmove(b.p + k, b.p + pos, b.n - pos);
				b.n -= pos - k;
				pos = k;
				b.p[b.n] = 0;
			}
			break;
		default: {
			str ch;
			int need, i;
			char cb;
			if ((unsigned char)c < 32)
				break;
			s_init(&ch);
			s_ch(&ch, c);
			need = u8len((unsigned char)c) - 1;
			for (i = 0; i < need; i++) {
				if (read(0, &cb, 1) != 1)
					break;
				s_ch(&ch, cb);
			}
			s_grow(&b, ch.n);
			memmove(b.p + pos + ch.n, b.p + pos, b.n - pos);
			memcpy(b.p + pos, ch.p, ch.n);
			b.n += ch.n;
			pos += ch.n;
			b.p[b.n] = 0;
			s_free(&ch);
			break;
		}
		}
		ed_draw(&e, &b, pos);
	}
	ed_off();
	s_ch(&b, '\n');
	r = xs(b.p);
	s_free(&b);
	s_free(&save);
	return r;
}

/* Expand !!, !$, !n, !-n and !prefix against the history list. */
char *hx_expand(sh *s, const char *line, int *changed, int *bad)
{
	str o, key;
	const char *p = line, *ev;
	int sq = 0, dq = 0;
	size_t n = s->hist.n, i;
	long k;

	*changed = 0;
	*bad = 0;
	s_init(&o);
	while (*p) {
		if (*p == '\\' && p[1] && !sq) {
			s_ch(&o, *p++);
			s_ch(&o, *p++);
			continue;
		}
		if (*p == '\'' && !dq)
			sq = !sq;
		else if (*p == '"' && !sq)
			dq = !dq;
		if (*p != '!' || sq || !p[1] || strchr(" \t\n=(\"", p[1]) ||
		    (p > line && p[-1] == '$')) {
			s_ch(&o, *p++);
			continue;
		}
		ev = 0;
		if (p[1] == '!') {
			ev = n ? (char *)s->hist.p[n - 1] : 0;
			p += 2;
		} else if (p[1] == '$') {
			const char *last = n ? (char *)s->hist.p[n - 1] : 0;
			p += 2;
			if (last) {
				const char *e = last + strlen(last);
				while (e > last && (e[-1] == ' ' || e[-1] == '\t'))
					e--;
				ev = e;
				while (ev > last && ev[-1] != ' ' && ev[-1] != '\t')
					ev--;
				s_init(&key);
				s_add(&key, ev, (size_t)(e - ev));
				s_cat(&o, key.p ? key.p : "");
				s_free(&key);
				*changed = 1;
				continue;
			}
		} else if (isdigit((unsigned char)p[1]) ||
			   (p[1] == '-' && isdigit((unsigned char)p[2]))) {
			char *e;
			k = strtol(p + 1, &e, 10);
			i = k < 0 ? n + (size_t)k : (size_t)k - 1;
			if ((k < 0 && (size_t)(-k) <= n) || (k > 0 && (size_t)k <= n))
				ev = (char *)s->hist.p[i];
			s_init(&key);
			s_add(&key, p, (size_t)(e - p));
			p = e;
			if (!ev) {
				lg(HIBR_LERR, "%s: event not found", key.p);
				s_free(&key);
				*bad = 1;
				break;
			}
			s_free(&key);
		} else {
			const char *b = p + 1;
			s_init(&key);
			p++;
			while (*p && !strchr(" \t\n;&|<>()\"'", *p))
				p++;
			s_add(&key, b, (size_t)(p - b));
			for (i = n; i-- > 0;)
				if (!strncmp((char *)s->hist.p[i], key.p, key.n)) {
					ev = (char *)s->hist.p[i];
					break;
				}
			if (!ev) {
				lg(HIBR_LERR, "!%s: event not found", key.p);
				s_free(&key);
				*bad = 1;
				break;
			}
			s_free(&key);
		}
		if (!ev) {
			lg(HIBR_LERR, "!: event not found");
			*bad = 1;
			break;
		}
		s_cat(&o, ev);
		*changed = 1;
	}
	return o.p ? o.p : xs("");
}
