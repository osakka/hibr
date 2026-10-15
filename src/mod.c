#include "pri.h"
#include <dirent.h>
#include <dlfcn.h>
#include <stdarg.h>

#ifndef HIBR_MODDIR
#define HIBR_MODDIR "/usr/local/lib/hibr"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct mod {
	void *h;
	const hibr_mod *m;
	char *path;
};

/* Look up a builtin provided by a loaded module. */
const hibr_bi *m_find(sh *s, const char *nm)
{
	size_t i;
	mod *m;
	const hibr_bi *b;

	for (i = s->mods.n; i-- > 0;) {
		m = (mod *)s->mods.p[i];
		for (b = m->m->bi; b && b->nm; b++)
			if (!strcmp(b->nm, nm))
				return b;
	}
	return 0;
}

/* True when a name already carries the shared object suffix. */
int m_hasso(const char *p)
{
	size_t n = strlen(p);

	return n > 3 && !strcmp(p + n - 3, ".so");
}

/* Try one candidate path, reporting a miss at debug level. */
void *m_try(str *p, const char *dir, const char *nm, int addso)
{
	void *h;

	p->n = 0;
	if (p->p)
		p->p[0] = 0;
	if (dir) {
		s_cat(p, dir);
		if (p->n && p->p[p->n - 1] != '/')
			s_ch(p, '/');
	}
	s_cat(p, nm);
	if (addso)
		s_cat(p, ".so");
	h = dlopen(p->p, RTLD_NOW | RTLD_LOCAL);
	if (!h)
		lg(HIBR_LDBG, "mod: %s: %s", p->p, dlerror());
	return h;
}

/* Whether the current directory is left out of the module search: for
   root, and during a plan, where opening a module found there would run
   its code. */
int m_nodot(sh *s)
{
	return geteuid() == 0 || (s->sopt & O_PLAN);
}

/* Where a module the person installed lands: $XDG_DATA_HOME/hibr/mods, or
   $HOME/.local/share/hibr/mods when that is unset -- the same shape the
   application manager already uses for every other kind. Nothing for root,
   whose rule is HIBR_MODDIR and nothing else (ADR 0016), and nothing with
   no home to put it in. Fills `out` and answers 1 when there is one. */
int m_udir(sh *s, str *out)
{
	const char *d;

	if (geteuid() == 0)
		return 0;
	d = hibr_get(s, "XDG_DATA_HOME");
	if (d && *d) {
		s_cat(out, d);
		s_cat(out, "/hibr/mods");
		return 1;
	}
	d = hibr_get(s, "HOME");
	if (!d || !*d)
		return 0;
	s_cat(out, d);
	s_cat(out, "/.local/share/hibr/mods");
	return 1;
}

/* The system module directory: where an administrator's catalogue has
   installed for every user of this machine (ADR 0045). It sits between
   the person's own folder and HIBR_MODDIR, so a user's copy still wins
   and the package's is still the floor.

   This widens ADR 0016, which says root loads modules only from
   HIBR_MODDIR, and the widening is paid for rather than assumed. Root
   gets the compiled-in path and nothing else -- an environment variable
   that could redirect root's module search is a way in, which is why
   HIBR_MODPATH is already refused for root a few lines above. And the
   directory has to look like the system's: owned by root, writable by
   nobody else. A path that fails either test is not an error, it is
   simply not a system scope, so the walk goes on to HIBR_MODDIR.

   `$HIBR_SYSDIR` is both how a script learns the path and how a person
   changes it, which is one knob rather than two for one idea: the shell
   sets it at startup from the compiled value unless the environment
   already did, so `catalogue --system` can read where to write without
   a second copy of `/var/lib/hibr` living in a script. The rule that
   keeps that safe is one sentence -- **the default is checked, a path
   you chose is yours** -- so the ownership test applies exactly when
   the value is the compiled one, and root never reads the environment's
   at all. */
int m_sysdir(sh *s, str *out)
{
	const char *d = 0;
	struct stat st;
	int own;

	if (geteuid() != 0)
		d = hibr_get(s, "HIBR_SYSDIR");
	if (!d || !*d)
		d = HIBR_SYSDIR;
	own = strcmp(d, HIBR_SYSDIR) == 0;
	s_cat(out, d);
	s_cat(out, "/mods");
	if (stat(out->p, &st) != 0 || !S_ISDIR(st.st_mode))
		return 0;
	if (own && (st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)))) {
		lg(HIBR_LDBG, "%s is not root-owned and private; not a system "
		   "module directory", out->p);
		return 0;
	}
	return 1;
}

/* One walk of the module search path, so the five places that search it
   cannot drift apart: the current directory unless root or planning,
   each entry of $HIBR_MODPATH, the folder above, then HIBR_MODDIR. The
   directory returned is valid until the next call.

   `bix` leaves the user's own folder out, and that asymmetry is the whole
   of ADR 0044: the autoload index is built by **any** command that is not
   a builtin, not a function and not on PATH -- a typo, in a cron job as
   much as at a prompt -- and building it dlopens every candidate to read
   its descriptor, which runs that object's ELF constructors. A module you
   installed is one you can ask for by name; it is never loaded because
   you mistyped something. */
void m_wstart(sh *s, struct mwalk *w, int bix)
{
	w->s = s;
	w->mp = geteuid() == 0 ? 0 : hibr_get(s, "HIBR_MODPATH");
	w->step = 0;
	w->bix = bix;
	s_init(&w->d);
}

const char *m_wnext(struct mwalk *w)
{
	sh *s = w->s;
	const char *e;
	size_t n;

	switch (w->step) {
	case 0:
		w->step = 1;
		if (!m_nodot(s))
			return ".";
		/* fall through */
	case 1:
		while (w->mp && *w->mp) {
			e = strchr(w->mp, ':');
			n = e ? (size_t)(e - w->mp) : strlen(w->mp);
			w->d.n = 0;
			if (n)
				s_add(&w->d, w->mp, n);
			w->mp = e ? e + 1 : w->mp + n;
			if (n)
				return w->d.p;
		}
		w->step = 2;
		/* fall through */
	case 2:
		w->step = 3;
		w->d.n = 0;
		if (!w->bix && m_udir(s, &w->d))
			return w->d.p;
		/* fall through */
	case 3:
		w->step = 4;
		w->d.n = 0;
		if (!w->bix && m_sysdir(s, &w->d))
			return w->d.p;
		/* fall through */
	case 4:
		w->step = 5;
		return HIBR_MODDIR;
	}
	return 0;
}

void m_wend(struct mwalk *w)
{
	s_free(&w->d);
}

/* Search the module path for a bare name, or open a given path. */
void *m_open(sh *s, const char *path, str *p)
{
	int so = m_hasso(path);
	void *h = 0;
	struct mwalk w;
	const char *d;

	if (strchr(path, '/')) {
		h = m_try(p, 0, path, 0);
		if (!h && !so)
			h = m_try(p, 0, path, 1);
		return h;
	}
	if (geteuid() == 0)
		lg(HIBR_LDBG, "root: searching only %s for %s", HIBR_MODDIR,
		   path);
	m_wstart(s, &w, 0);
	while (!h && (d = m_wnext(&w))) {
		h = m_try(p, d, path, 0);
		if (!h && !so)
			h = m_try(p, d, path, 1);
	}
	m_wend(&w);
	return h;
}

/* Load a shared object exporting the module descriptor. */
int m_load(sh *s, const char *path)
{
	void *h;
	const hibr_mod *d;
	mod *m;
	size_t i;
	str p;

	s_init(&p);
	h = m_open(s, path, &p);
	if (!h) {
		lg(HIBR_LERR, "mod: %s: not found on the module path", path);
		s_free(&p);
		return HIBR_FAIL;
	}
	d = (const hibr_mod *)dlsym(h, "hibr_module");
	if (!d) {
		lg(HIBR_LERR, "mod: %s: no hibr_module symbol", p.p);
		dlclose(h);
		s_free(&p);
		return HIBR_FAIL;
	}
	if (d->abi != HIBR_ABI) {
		lg(HIBR_LERR, "mod: %s: abi %u, shell expects %u", d->nm, d->abi,
		   HIBR_ABI);
		dlclose(h);
		s_free(&p);
		return HIBR_FAIL;
	}
	for (i = 0; i < s->mods.n; i++)
		if (!strcmp(((mod *)s->mods.p[i])->m->nm, d->nm)) {
			lg(HIBR_LWRN, "mod: %s already loaded", d->nm);
			dlclose(h);
			s_free(&p);
			return HIBR_FAIL;
		}
	m = xm(sizeof *m);
	m->h = h;
	m->m = d;
	m->path = p.p;
	v_add(&s->mods, m);
	if (d->ini && d->ini(s) != HIBR_OK) {
		lg(HIBR_LERR, "mod: %s: init failed", d->nm);
		/* This record goes by identity, never by position: an init
		   may load a module of its own -- hold asks hibr_require for
		   pty and term -- so by now this one is not the last, and
		   `s->mods.n--` dropped *that* module's record and left this
		   one, freed below, for every later walk of the list to
		   dereference. */
		for (i = 0; i < s->mods.n; i++)
			if (s->mods.p[i] == m) {
				s->mods.p[i] = s->mods.p[--s->mods.n];
				break;
			}
		dlclose(h);
		free(m->path);
		free(m);
		return HIBR_FAIL;
	}
	lg(HIBR_LINF, "loaded module %s %s", d->nm, d->ver ? d->ver : "");
	return HIBR_OK;
}

/* Unload a module by name. */
int m_drop(sh *s, const char *nm)
{
	size_t i;
	mod *m;

	for (i = 0; i < s->mods.n; i++) {
		m = (mod *)s->mods.p[i];
		if (strcmp(m->m->nm, nm))
			continue;
		if (m->m->fin)
			m->m->fin(s);
		dlclose(m->h);
		free(m->path);
		free(m);
		s->mods.p[i] = s->mods.p[s->mods.n - 1];
		s->mods.n--;
		lg(HIBR_LINF, "dropped module %s", nm);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "mod: %s: not loaded", nm);
	return HIBR_FAIL;
}

/* Unload everything, newest first -- so a module that asked for another's
   table goes before the one that offered it. Returns how many went. */
int m_dropall(sh *s)
{
	mod *m;
	int n = 0;

	while (s->mods.n) {
		char *nm;
		m = (mod *)s->mods.p[s->mods.n - 1];
		/* The descriptor lives inside the object, so its name has to
		   be taken before dlclose unmaps it. */
		nm = xs(m->m->nm);
		if (m->m->fin)
			m->m->fin(s);
		dlclose(m->h);
		lg(HIBR_LINF, "dropped module %s", nm);
		free(nm);
		free(m->path);
		free(m);
		s->mods.n--;
		n++;
	}
	return n;
}

/* Compare two loaded modules by name, for the terminal listing. */
int m_cmpnm(const void *a, const void *b)
{
	const mod *x = *(const mod **)a, *y = *(const mod **)b;

	return strcmp(x->m->nm, y->m->nm);
}

/* Print the loaded module table.

   Two forms, decided on isatty(1), the same discipline the cat module keeps:
   in a pipe it is byte for byte what it has always printed, because
   tests/090-module.expected records it and anything parsing it reads that;
   on a terminal it is fitted to the width.

   Unfitted, six of the modules a desktop loads ran past eighty columns --
   hold at 85, pty at 83 -- so they wrapped and every other row was a
   continuation (Gitea #132). Fitted: sorted by name, since the load order
   is not interesting; the columns only as wide as the widest name and
   version actually present; the abi said once in the heading, because a
   module whose abi does not match the shell's is refused at load, so every
   loaded one has the same; and the description cut to what is left. */
/* One line of a listing, to the screen or into a result slot: `out` is set
   only while a slot is bound, and then nothing is printed at all. A builtin
   that answers something has to go quiet when its answer is being captured
   -- `x := mod list` used to leave x empty and write the table into the
   middle of whatever was on screen, which is #167's shape (Gitea #205). */
void m_putln(str *out, const char *fmt, ...)
{
	va_list ap;
	char *b;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(0, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	b = xm((size_t)n + 1);
	va_start(ap, fmt);
	vsnprintf(b, (size_t)n + 1, fmt, ap);
	va_end(ap);
	if (out)
		s_cat(out, b);
	else
		fputs(b, stdout);
	free(b);
}

/* Hand a built listing over: the slot gets it without its last newline,
   the way every other answer is bound, and the screen gets it whole. */
void m_bound(sh *s, str *o)
{
	if (o->n && o->p[o->n - 1] == '\n')
		o->p[--o->n] = 0;
	hibr_set(s, "RET", o->p ? o->p : "", 0);
}

void m_list(sh *s)
{
	size_t i;
	mod *m;
	mod **srt;
	str o;
	int nw = 6, vw = 7, cols, dw;

	if (!s->mods.n)
		return;
	/* The plain form is what a bound slot gets, whatever stdout is: the
	   fitted one is laid out for a terminal nobody is reading here, and
	   the columns of this one are what tests/090-module.t reads. */
	if (s->bind || !isatty(1)) {
		s_init(&o);
		for (i = 0; i < s->mods.n; i++) {
			m = (mod *)s->mods.p[i];
			m_putln(s->bind ? &o : 0,
				"%-12s %-8s abi %u  %s\n", m->m->nm,
				m->m->ver ? m->m->ver : "-", m->m->abi,
				m->m->dsc ? m->m->dsc : "");
		}
		if (s->bind)
			m_bound(s, &o);
		s_free(&o);
		return;
	}
	srt = xm(s->mods.n * sizeof *srt);
	for (i = 0; i < s->mods.n; i++) {
		srt[i] = (mod *)s->mods.p[i];
		if ((int)strlen(srt[i]->m->nm) > nw)
			nw = (int)strlen(srt[i]->m->nm);
		if (srt[i]->m->ver && (int)strlen(srt[i]->m->ver) > vw)
			vw = (int)strlen(srt[i]->m->ver);
	}
	qsort(srt, s->mods.n, sizeof *srt, m_cmpnm);
	cols = ed_cols();
	/* Two separators of two columns each, and one column left unwritten:
	   a row exactly as wide as the terminal is one some terminals wrap
	   on the last cell, which is the wrapping this is here to stop. */
	dw = cols - nw - vw - 5;
	if (dw < 8)
		dw = 8;
	printf("%-*s  %-*s  %s\n", nw, "MODULE", vw, "VERSION", "DESCRIPTION");
	for (i = 0; i < s->mods.n; i++) {
		const char *d;
		int dn;

		m = srt[i];
		d = m->m->dsc ? m->m->dsc : "";
		dn = (int)strlen(d);
		printf("%-*s  %-*s  ", nw, m->m->nm, vw,
		       m->m->ver ? m->m->ver : "-");
		if (dn <= dw) {
			printf("%s\n", d);
		} else {
			/* Back off a UTF-8 continuation byte rather than cut a
			   character in half; every description here is ASCII,
			   and one added later may not be. */
			int k = dw - 1;

			while (k > 0 && ((unsigned char)d[k] & 0xC0) == 0x80)
				k--;
			printf("%.*s\xe2\x80\xa6\n", k, d);
		}
	}
	printf("%zu loaded, module ABI %u\n", s->mods.n,
	       ((mod *)s->mods.p[0])->m->abi);
	free(srt);
}

/* Whether this candidate is loaded: 2 from this very file, 1 by name only. */
int m_isload(sh *s, const char *nm, const char *path)
{
	size_t i;
	mod *m;
	int byname = 0;

	for (i = 0; i < s->mods.n; i++) {
		m = (mod *)s->mods.p[i];
		if (strcmp(m->m->nm, nm))
			continue;
		if (m->path && path && !strcmp(m->path, path))
			return 2;
		byname = 1;
	}
	return byname;
}

/* Describe one candidate object, without ever making it a loaded module.
   On a terminal that is a line of columns and a second line of what it
   adds; in a pipe it is one line of eight tab-separated fields -- name,
   version, ABI, state, path, the interface it offers, its builtins and
   its description -- because a program reading this wants fields, not a
   table, the same rule mod list already follows. Every branch writes all
   eight, `-` or empty where there is nothing, or a reader's own fields
   would silently shift on the first object that will not open. */
void m_probe(sh *s, const char *path, const char *file, str *out)
{
	void *h;
	const hibr_mod *d;
	const hibr_bi *b;
	const char *state;
	str ab, bl;
	int i, tty = out ? 0 : isatty(1);

	s_init(&ab);
	s_init(&bl);
	h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		lg(HIBR_LDBG, "mod avail: %s: %s", path, dlerror());
		s_cat(&ab, "-");
		if (tty)
			m_putln(out, "%-12s %-8s %-8s %-10s %s\n", file, "-",
				ab.p, "unreadable", path);
		else
			m_putln(out, "%s\t-\t-\tunreadable\t%s\t-\t-\t-\n",
				file, path);
		s_free(&ab);
		s_free(&bl);
		return;
	}
	d = (const hibr_mod *)dlsym(h, "hibr_module");
	if (!d) {
		s_cat(&ab, "-");
		if (tty)
			m_putln(out, "%-12s %-8s %-8s %-10s %s\n", file, "-",
				ab.p, "no descr", path);
		else
			m_putln(out, "%s\t-\t-\tno descr\t%s\t-\t-\t-\n",
				file, path);
		dlclose(h);
		s_free(&ab);
		s_free(&bl);
		return;
	}
	s_cat(&ab, "abi ");
	s_num(&ab, (long)d->abi);
	i = m_isload(s, d->nm, path);
	if (i == 2)
		state = "loaded";
	else if (d->abi != HIBR_ABI)
		state = "wrong abi";
	else if (i)
		state = "elsewhere";
	else
		state = "available";
	for (b = d->bi; b && b->nm; b++) {
		if (bl.n)
			s_cat(&bl, ", ");
		s_cat(&bl, b->nm);
	}
	if (!tty) {
		/* `-` where there is nothing, never an empty field: tab is
		   IFS whitespace, so a reader splitting on it loses an empty
		   one and every field after shifts along. */
		m_putln(out, "%s\t%s\t%u\t%s\t%s\t%s\t%s\t%s\n", d->nm,
			d->ver ? d->ver : "-", d->abi, state, path,
			d->prov ? d->prov : "-", bl.n ? bl.p : "-",
			d->dsc ? d->dsc : "-");
		dlclose(h);
		s_free(&ab);
		s_free(&bl);
		return;
	}
	if (d->prov) {
		if (bl.n)
			s_cat(&bl, "   ");
		s_cat(&bl, "offers ");
		s_cat(&bl, d->prov);
	}
	m_putln(out, "%-12s %-8s %-8s %-10s %s\n", d->nm,
		d->ver ? d->ver : "-", ab.p, state, path);
	if (bl.n)
		m_putln(out, "             %s\n", bl.p);
	dlclose(h);
	s_free(&ab);
	s_free(&bl);
}

/* Compare two names for sorting a directory listing. */
int m_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Probe every shared object in one directory. */
void m_scan(sh *s, const char *dir, vec *seen, str *out)
{
	DIR *dp = opendir(dir);
	struct dirent *de;
	vec names;
	size_t i, j;
	str full;

	if (!dp) {
		lg(HIBR_LDBG, "mod avail: %s: not a directory", dir);
		return;
	}
	names.p = 0;
	names.n = 0;
	names.cap = 0;
	while ((de = readdir(dp)))
		if (m_hasso(de->d_name))
			v_add(&names, xs(de->d_name));
	closedir(dp);
	if (names.n > 1)
		qsort(names.p, names.n, sizeof *names.p, m_cmp);
	for (i = 0; i < names.n; i++) {
		for (j = 0; j < seen->n; j++)
			if (!strcmp((char *)seen->p[j], (char *)names.p[i]))
				break;
		if (j < seen->n) {
			lg(HIBR_LDBG, "mod avail: %s/%s is shadowed", dir,
			   (char *)names.p[i]);
			free(names.p[i]);
			continue;
		}
		v_add(seen, xs((char *)names.p[i]));
		s_init(&full);
		s_cat(&full, dir);
		if (full.n && full.p[full.n - 1] != '/')
			s_ch(&full, '/');
		s_cat(&full, (char *)names.p[i]);
		m_probe(s, full.p, (char *)names.p[i], out);
		s_free(&full);
		free(names.p[i]);
	}
	v_free(&names);
}

/* List every module that could be loaded, in the order a bare name finds. */
void m_avail(sh *s)
{
	struct mwalk w;
	const char *d;
	vec seen;
	size_t i;
	str o;
	str *out = 0;

	s_init(&o);
	if (s->bind)
		out = &o;
	seen.p = 0;
	seen.n = 0;
	seen.cap = 0;
	/* No heading in the bound form, which is the tab-separated one a
	   script reads field by field. */
	if (!out && isatty(1))
		printf("%-12s %-8s %-8s %-10s %s\n", "NAME", "VERSION", "ABI",
		       "STATE", "PATH");
	if (geteuid() == 0)
		lg(HIBR_LDBG, "root: listing only %s", HIBR_MODDIR);
	m_wstart(s, &w, 0);
	while ((d = m_wnext(&w)))
		m_scan(s, d, &seen, out);
	m_wend(&w);
	for (i = 0; i < seen.n; i++)
		free(seen.p[i]);
	v_free(&seen);
	if (out)
		m_bound(s, &o);
	s_free(&o);
}

struct api {
	char *nm;
	unsigned ver;
	void *p;
};

/* Offer a table of functions to other modules, replacing one of the same name. */
int hibr_provide(sh *s, const char *nm, unsigned ver, void *api)
{
	struct api *a;
	size_t i;

	if (!nm || !*nm || !api)
		return HIBR_FAIL;
	for (i = 0; i < s->apis.n; i++) {
		a = (struct api *)s->apis.p[i];
		if (strcmp(a->nm, nm))
			continue;
		a->ver = ver;
		a->p = api;
		lg(HIBR_LDBG, "api %s v%u replaced", nm, ver);
		return HIBR_OK;
	}
	a = xm(sizeof *a);
	a->nm = xs(nm);
	a->ver = ver;
	a->p = api;
	v_add(&s->apis, a);
	lg(HIBR_LDBG, "api %s v%u offered", nm, ver);
	return HIBR_OK;
}

/* Look for an offer already made. */
void *m_offered(sh *s, const char *nm, unsigned ver, int *wrong)
{
	struct api *a;
	size_t i;

	*wrong = 0;
	for (i = 0; i < s->apis.n; i++) {
		a = (struct api *)s->apis.p[i];
		if (strcmp(a->nm, nm))
			continue;
		if (a->ver != ver) {
			*wrong = 1;
			lg(HIBR_LERR, "api %s is v%u, v%u was asked for", nm,
			   a->ver, ver);
			return 0;
		}
		return a->p;
	}
	return 0;
}

/* Does the module in this file say it offers that interface? */
int m_declares(const char *path, const char *iface)
{
	void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	const hibr_mod *d;
	int yes = 0;

	if (!h)
		return 0;
	d = (const hibr_mod *)dlsym(h, "hibr_module");
	if (d && d->abi == HIBR_ABI && d->prov && !strcmp(d->prov, iface))
		yes = 1;
	dlclose(h);
	return yes;
}

/* Does the module in this file register a builtin by this exact name?
   The same probe m_declares makes for an interface, reading the static
   descriptor without ever calling the module's own init -- so finding out
   costs a dlopen/dlclose, not the module's own startup work, for every
   candidate that turns out not to be the one. */
int m_declaresbi(const char *path, const char *nm)
{
	void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	const hibr_mod *d;
	const hibr_bi *b;
	int yes = 0;

	if (!h)
		return 0;
	d = (const hibr_mod *)dlsym(h, "hibr_module");
	if (d && d->abi == HIBR_ABI)
		for (b = d->bi; b && b->nm; b++)
			if (!strcmp(b->nm, nm)) {
				yes = 1;
				break;
			}
	dlclose(h);
	return yes;
}

/* Which end of the command search a module may answer from: HIBR_MODULES,
   one of off, after or before, and after when it says nothing (ADR 0040).
   Cached the way FUNCNEST's own limit is, because this is asked on every
   command that is neither a builtin nor a function; v_named clears it. */
int m_ldmode = -1;

int m_ldwhen(sh *s)
{
	const char *v;

	if (m_ldmode >= 0)
		return m_ldmode;
	v = hibr_get(s, "HIBR_MODULES");
	if (!v || !*v)
		m_ldmode = MD_AFTER;
	else if (!strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "0"))
		m_ldmode = MD_OFF;
	else if (!strcmp(v, "before"))
		m_ldmode = MD_BEFORE;
	else if (!strcmp(v, "after"))
		m_ldmode = MD_AFTER;
	else {
		lg(HIBR_LWRN, "HIBR_MODULES: %s is not off, after or before",
		   v);
		m_ldmode = MD_AFTER;
	}
	lg(HIBR_LDBG, "module autoload is %s",
	   m_ldmode == MD_OFF ? "off" : m_ldmode == MD_BEFORE ? "before" : "after");
	return m_ldmode;
}

/* The builtins every module on the path declares, each with the object that
   declares it, sorted so a lookup is a bsearch.

   `before` asks this of every command that is not a builtin or a function
   -- every ls, git, grep -- and the scan behind it dlopens each candidate
   to read its descriptor, which is 4.3 ms for this machine's 35 modules:
   fine once, ruinous per command. So it is built once, lazily, and kept.
   A module installed after this shell started is therefore not seen by it;
   `need` and `mod load` still find one by name, and a new shell indexes it.
   Not thrown away when a module loads or drops, because what is on the path
   has not changed -- only what is in memory, which m_find already answers. */
struct bix {
	char *nm;
	char *path;
};

struct bix *m_bix;
size_t m_bixn;
int m_bixok;

int m_bixcmp(const void *a, const void *b)
{
	return strcmp(((const struct bix *)a)->nm, ((const struct bix *)b)->nm);
}

/* Index every builtin one candidate object declares. */
void m_bixadd(const char *path)
{
	void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	const hibr_mod *d;
	const hibr_bi *b;

	if (!h)
		return;
	d = (const hibr_mod *)dlsym(h, "hibr_module");
	if (d && d->abi == HIBR_ABI)
		for (b = d->bi; b && b->nm; b++) {
			struct bix *g = realloc(m_bix,
						(m_bixn + 1) * sizeof *g);
			if (!g)
				break;
			m_bix = g;
			m_bix[m_bixn].nm = xs(b->nm);
			m_bix[m_bixn].path = xs(path);
			m_bixn++;
		}
	dlclose(h);
}

/* Index one directory, in name order so two modules declaring the same
   builtin resolve the same way twice running. */
void m_bixdir(sh *s, const char *dir)
{
	DIR *dp = opendir(dir);
	struct dirent *de;
	vec names;
	size_t i;

	(void)s;
	if (!dp)
		return;
	names.p = 0;
	names.n = 0;
	names.cap = 0;
	while ((de = readdir(dp)))
		if (m_hasso(de->d_name))
			v_add(&names, xs(de->d_name));
	closedir(dp);
	if (names.n > 1)
		qsort(names.p, names.n, sizeof *names.p, m_cmp);
	for (i = 0; i < names.n; i++) {
		str full;

		s_init(&full);
		s_cat(&full, dir);
		if (full.n && full.p[full.n - 1] != '/')
			s_ch(&full, '/');
		s_cat(&full, (char *)names.p[i]);
		m_bixadd(full.p);
		s_free(&full);
	}
	for (i = 0; i < names.n; i++)
		free(names.p[i]);
	v_free(&names);
}

/* Build the index: the same places, in the same order, that m_findbi
   searches -- including root's own rule, which is HIBR_MODDIR and nothing
   else (ADR 0016). */
void m_bixbuild(sh *s)
{
	struct mwalk w;
	const char *d;

	m_bixok = 1;
	m_wstart(s, &w, 1);
	while ((d = m_wnext(&w)))
		m_bixdir(s, d);
	m_wend(&w);
	if (m_bixn > 1)
		qsort(m_bix, m_bixn, sizeof *m_bix, m_bixcmp);
	lg(HIBR_LDBG, "indexed %zu builtins on the module path", m_bixn);
}

/* The object declaring this builtin, or NULL. */
const char *m_bixfind(sh *s, const char *nm)
{
	struct bix k, *g;

	if (!m_bixok)
		m_bixbuild(s);
	if (!m_bixn)
		return 0;
	k.nm = (char *)nm;
	k.path = 0;
	g = bsearch(&k, m_bix, m_bixn, sizeof *m_bix, m_bixcmp);
	return g ? g->path : 0;
}

/* Load whichever module declares this builtin, from the index: `before`'s
   own path, which must not walk the module directory per command. */
int m_ldauto(sh *s, const char *nm)
{
	const char *p;

	if (s->sopt & O_PLAN)
		return HIBR_FAIL;
	p = m_bixfind(s, nm);
	if (!p)
		return HIBR_FAIL;
	lg(HIBR_LINF, "loading %s, which declares %s", p, nm);
	return m_load(s, p);
}

/* Walk one directory for a module that offers the interface, and load it. */
int m_seek(sh *s, const char *dir, const char *iface)
{
	DIR *dp = opendir(dir);
	struct dirent *de;
	vec names;
	size_t i;
	int got = 0;

	if (!dp)
		return 0;
	names.p = 0;
	names.n = 0;
	names.cap = 0;
	while ((de = readdir(dp)))
		if (m_hasso(de->d_name))
			v_add(&names, xs(de->d_name));
	closedir(dp);
	if (names.n > 1)
		qsort(names.p, names.n, sizeof *names.p, m_cmp);
	for (i = 0; i < names.n && !got; i++) {
		str full;
		s_init(&full);
		s_cat(&full, dir);
		if (full.n && full.p[full.n - 1] != '/')
			s_ch(&full, '/');
		s_cat(&full, (char *)names.p[i]);
		if (m_declares(full.p, iface)) {
			lg(HIBR_LINF, "loading %s, which offers %s", full.p,
			   iface);
			got = m_load(s, full.p) == HIBR_OK;
		}
		s_free(&full);
	}
	for (i = 0; i < names.n; i++)
		free(names.p[i]);
	v_free(&names);
	return got;
}

/* Walk one directory for a module that registers this builtin, and load
   it. Same shape as m_seek, one directory below it, checking m_declaresbi
   instead of m_declares -- kept as its own copy rather than a shared
   walker taking a predicate, since the two are three lines apart and a
   parameter here would only be used by these two callers. */
int m_seekbi(sh *s, const char *dir, const char *nm)
{
	DIR *dp = opendir(dir);
	struct dirent *de;
	vec names;
	size_t i;
	int got = 0;

	if (!dp)
		return 0;
	names.p = 0;
	names.n = 0;
	names.cap = 0;
	while ((de = readdir(dp)))
		if (m_hasso(de->d_name))
			v_add(&names, xs(de->d_name));
	closedir(dp);
	if (names.n > 1)
		qsort(names.p, names.n, sizeof *names.p, m_cmp);
	for (i = 0; i < names.n && !got; i++) {
		str full;
		s_init(&full);
		s_cat(&full, dir);
		if (full.n && full.p[full.n - 1] != '/')
			s_ch(&full, '/');
		s_cat(&full, (char *)names.p[i]);
		if (m_declaresbi(full.p, nm)) {
			lg(HIBR_LINF, "loading %s, which registers %s", full.p,
			   nm);
			got = m_load(s, full.p) == HIBR_OK;
		}
		s_free(&full);
	}
	for (i = 0; i < names.n; i++)
		free(names.p[i]);
	v_free(&names);
	return got;
}

/* Find and load whichever module on the module path registers a builtin
   by this exact name -- `mod find`, which somebody types. Already loaded
   is checked first and is free; everything past that is the same walk
   hibr_require and `need` make, the user's own folder included, because
   asking for a builtin by name is an explicit act. The *index* that a
   missing command builds is the one walk that leaves that folder out --
   see m_wstart and ADR 0044. */
int m_findbi(sh *s, const char *nm)
{
	struct mwalk w;
	const char *d;

	if (m_find(s, nm))
		return HIBR_OK;
	m_wstart(s, &w, 0);
	while ((d = m_wnext(&w)))
		if (m_seekbi(s, d, nm)) {
			m_wend(&w);
			return HIBR_OK;
		}
	m_wend(&w);
	return HIBR_FAIL;
}


/* Ask for a table another module offered. When nothing has offered it yet,
   look along the module path for one that says it would, and load that --
   so a tool asks for what it needs rather than for the module that has it. */
void *hibr_require(sh *s, const char *nm, unsigned ver)
{
	struct mwalk w;
	const char *d;
	void *p;
	int wrong;

	p = m_offered(s, nm, ver, &wrong);
	if (p || wrong)
		return p;
	lg(HIBR_LDBG, "no loaded module offers %s; looking for one", nm);
	m_wstart(s, &w, 0);
	while ((d = m_wnext(&w)))
		if (m_seek(s, d, nm)) {
			m_wend(&w);
			return m_offered(s, nm, ver, &wrong);
		}
	m_wend(&w);
	lg(HIBR_LDBG, "nothing on the module path offers %s", nm);
	return 0;
}

/* Make an interface available: already offered, or found on the module path,
   or -- failing both -- the name of a module to load outright. This is what
   hibr_require does, with a status instead of a pointer, so a script can ask
   for what it needs the same way a module does. */
int m_need(sh *s, const char *nm)
{
	const char *mp;
	size_t i;

	if (!nm || !*nm)
		return HIBR_FAIL;
	/* Present is enough; the version is the caller's business, and asking
	   m_offered here would complain about a mismatch nobody asked about. */
	for (i = 0; i < s->apis.n; i++)
		if (!strcmp(((struct api *)s->apis.p[i])->nm, nm))
			return HIBR_OK;
	mp = geteuid() == 0 ? 0 : hibr_get(s, "HIBR_MODPATH");
	if (!m_nodot(s) && m_seek(s, ".", nm))
		return HIBR_OK;
	while (mp && *mp) {
		const char *e = strchr(mp, ':');
		size_t n = e ? (size_t)(e - mp) : strlen(mp);
		if (n) {
			char *d = ar_dup(s->xa, mp, n);
			if (m_seek(s, d, nm))
				return HIBR_OK;
		}
		mp = e ? e + 1 : mp + n;
	}
	if (m_seek(s, HIBR_MODDIR, nm))
		return HIBR_OK;
	/* Not an interface anyone offers; it may simply be a module. */
	for (i = 0; i < s->mods.n; i++)
		if (!strcmp(((mod *)s->mods.p[i])->m->nm, nm))
			return HIBR_OK;
	return m_load(s, nm);
}

/* Withdraw an offer, which a module must do before it is unloaded. */
int hibr_unprovide(sh *s, const char *nm)
{
	struct api *a;
	size_t i;

	for (i = 0; i < s->apis.n; i++) {
		a = (struct api *)s->apis.p[i];
		if (strcmp(a->nm, nm))
			continue;
		free(a->nm);
		free(a);
		s->apis.p[i] = s->apis.p[--s->apis.n];
		lg(HIBR_LDBG, "api %s withdrawn", nm);
		return HIBR_OK;
	}
	return HIBR_FAIL;
}

/* Release every offer, at shutdown. */
void m_apifree(sh *s)
{
	struct api *a;

	while (s->apis.n) {
		a = (struct api *)s->apis.p[--s->apis.n];
		free(a->nm);
		free(a);
	}
	v_free(&s->apis);
}

/* Unload every module at shutdown. */
void m_fini(sh *s)
{
	mod *m;

	while (s->mods.n) {
		m = (mod *)s->mods.p[--s->mods.n];
		if (m->m->fin)
			m->m->fin(s);
		dlclose(m->h);
		free(m->path);
		free(m);
	}
	v_free(&s->mods);
	m_apifree(s);
}

/* Print the builtins each loaded module contributes. */
void m_help(sh *s)
{
	size_t i;
	mod *m;
	const hibr_bi *b;

	for (i = 0; i < s->mods.n; i++) {
		m = (mod *)s->mods.p[i];
		printf("module %s (%s):\n", m->m->nm, m->m->ver ? m->m->ver : "-");
		for (b = m->m->bi; b && b->nm; b++)
			printf("  %-10s %s\n", b->nm, b->hp ? b->hp : "");
	}
}
