#include "pri.h"
#include <dlfcn.h>

#ifndef HIBR_MODDIR
#define HIBR_MODDIR "/usr/local/lib/hibr"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Search the module path for a bare name, or open a given path. */
void *m_open(sh *s, const char *path, str *p)
{
	const char *mp = hibr_get(s, "HIBR_MODPATH");
	int so = m_hasso(path);
	void *h;

	if (strchr(path, '/'))
		return m_try(p, 0, path, 0);
	h = m_try(p, ".", path, 0);
	if (!h && !so)
		h = m_try(p, ".", path, 1);
	while (!h && mp && *mp) {
		const char *e = strchr(mp, ':');
		size_t n = e ? (size_t)(e - mp) : strlen(mp);
		char *d = ar_dup(s->xa, mp, n);
		if (n) {
			h = m_try(p, d, path, 0);
			if (!h && !so)
				h = m_try(p, d, path, 1);
		}
		mp = e ? e + 1 : mp + n;
	}
	if (!h)
		h = m_try(p, HIBR_MODDIR, path, 0);
	if (!h && !so)
		h = m_try(p, HIBR_MODDIR, path, 1);
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
		s->mods.n--;
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

/* Print the loaded module table. */
void m_list(sh *s)
{
	size_t i;
	mod *m;

	for (i = 0; i < s->mods.n; i++) {
		m = (mod *)s->mods.p[i];
		printf("%-12s %-8s abi %u  %s\n", m->m->nm,
		       m->m->ver ? m->m->ver : "-", m->m->abi,
		       m->m->dsc ? m->m->dsc : "");
	}
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
