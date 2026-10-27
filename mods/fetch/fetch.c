#define _GNU_SOURCE

#include "ft.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int sx_out(sh *s, const char *nm, const char *v);
void v_del(sh *s, const char *nm);

/* An empty request: every field its default. */
void ft_reqinit(ft_req *rq)
{
	memset(rq, 0, sizeof *rq);
	rq->bfd = -1;
}

/* The interface's own fetch: the request carried out. */
int ft_fetch(sh *s, const ft_req *rq, ft_res *rs)
{
	(void)s;
	return ft_do(rq, rs);
}

ft_api ft_tab = {
	FT_API_VER, ft_reqinit, ft_fetch, ft_resfree, ft_header, ft_closeall,
	ft_jarclear
};

/* Where the script verb's body goes when it is not kept: a descriptor. */
typedef struct ft_out ft_out;
struct ft_out {
	int fd;
	int failed;
};

int ft_tofd(void *ctx, const char *p, size_t n)
{
	ft_out *o = ctx;

	if (hibr_wrall(o->fd, p, n) != (ssize_t)n) {
		o->failed = errno ? errno : EIO;
		return 1;
	}
	return 0;
}

/* Put one field of the reply in the result map. */
void ft_retset(sh *s, const char *k1, const char *k2, const char *v)
{
	char *ks[2];

	ks[0] = (char *)k1;
	ks[1] = (char *)k2;
	hibr_setp(s, "RET", ks, k2 ? 2 : 1, v ? v : "");
}

/* The reply's headers as a map, by lowercased name; a header sent more
   than once is joined with ", ", as HTTP allows -- Set-Cookie, which may
   not be, with a newline. */
void ft_rethdrs(sh *s, const ft_res *rs)
{
	size_t i;
	str nm, v;
	const char *line, *c, *old;
	char *ks[2];

	s_init(&nm);
	s_init(&v);
	ks[0] = "headers";
	for (i = 0; i < rs->hdr.n; i++) {
		line = rs->hdr.p[i];
		c = strchr(line, ':');
		if (!c)
			continue;
		nm.n = 0;
		s_add(&nm, line, (size_t)(c - line));
		for (size_t k = 0; k < nm.n; k++)
			if (nm.p[k] >= 'A' && nm.p[k] <= 'Z')
				nm.p[k] = (char)(nm.p[k] + 32);
		for (c++; *c == ' ' || *c == '\t'; c++)
			;
		ks[1] = nm.p;
		old = hibr_getp(s, "RET", ks, 2);
		v.n = 0;
		if (old && *old) {
			s_cat(&v, old);
			s_cat(&v, strcmp(nm.p, "set-cookie") ? ", " : "\n");
		}
		s_cat(&v, c);
		hibr_setp(s, "RET", ks, 2, v.p);
	}
	s_free(&nm);
	s_free(&v);
}

/* fetch [METHOD] URL [-H 'Name: value']... [-d body | -f file] [-o file]
   [-j jar] [-u user:pass] [-t seconds] [-k] [-n] [-i]: a request, its body
   printed or written to a file, or with := the reply as a map. FETCH_STATUS
   is the status, 0 when no reply came. fetch jar clear NAME forgets a jar. */
int m_fetch(sh *s, int ac, char **av)
{
	static const char *methods[] = { "get", "head", "post", "put", "patch",
					 "delete", "options", 0 };
	ft_req rq;
	ft_res rs;
	ft_out out;
	vec hdr = { 0, 0, 0 };
	str meth, user, num;
	const char *url = 0, *ofile = 0, *ffile = 0;
	char *colon;
	int k = 1, i, rc, ofd = -1, ffd = -1;
	struct stat st;

	if (ac >= 4 && !strcmp(av[1], "jar") && !strcmp(av[2], "clear")) {
		if (ft_jarclear(s, av[3]) != HIBR_OK) {
			lg(HIBR_LERR, "fetch jar clear: %s: not a jar, or it "
				      "could not be removed", av[3]);
			return HIBR_FAIL;
		}
		return HIBR_OK;
	}
	ft_reqinit(&rq);
	s_init(&meth);
	s_init(&user);
	s_init(&num);
	s_cat(&meth, "GET");
	if (k < ac)
		for (i = 0; methods[i]; i++)
			if (!strcasecmp(av[k], methods[i])) {
				meth.n = 0;
				for (const char *p = av[k]; *p; p++)
					s_ch(&meth, *p >= 'a' && *p <= 'z' ?
							    *p - 32 : *p);
				k++;
				break;
			}
	for (; k < ac; k++) {
		const char *a = av[k];

		if (!strcmp(a, "-H") && k + 1 < ac) {
			v_add(&hdr, av[++k]);
		} else if (!strcmp(a, "-d") && k + 1 < ac) {
			rq.body = av[++k];
			rq.blen = strlen(rq.body);
		} else if (!strcmp(a, "-f") && k + 1 < ac) {
			ffile = av[++k];
		} else if (!strcmp(a, "-o") && k + 1 < ac) {
			ofile = av[++k];
		} else if (!strcmp(a, "-j") && k + 1 < ac) {
			rq.jar = av[++k];
		} else if (!strcmp(a, "-u") && k + 1 < ac) {
			user.n = 0;
			s_cat(&user, av[++k]);
			colon = strchr(user.p, ':');
			if (colon) {
				*colon = 0;
				rq.pass = colon + 1;
			}
			rq.user = user.p;
		} else if (!strcmp(a, "-t") && k + 1 < ac) {
			rq.timeout = atoi(av[++k]);
		} else if (!strcmp(a, "-k")) {
			rq.flags |= FT_NOVERIFY;
		} else if (!strcmp(a, "-n")) {
			rq.flags |= FT_NOFOLLOW;
		} else if (!strcmp(a, "-i")) {
			rq.flags |= FT_IDENTITY;
		} else if (a[0] == '-' && a[1] && !url) {
			lg(HIBR_LERR, "fetch: %s: not an option", a);
			goto usage;
		} else if (!url) {
			url = a;
		} else {
			goto usage;
		}
	}
	if (!url)
		goto usage;
	if (ffile) {
		ffd = open(ffile, O_RDONLY | O_CLOEXEC);
		if (ffd < 0 || fstat(ffd, &st) != 0) {
			lg(HIBR_LERR, "fetch: %s: %s", ffile, strerror(errno));
			goto fail;
		}
		rq.bfd = ffd;
		rq.bflen = (long long)st.st_size;
	}
	if (ofile) {
		ofd = open(ofile, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (ofd < 0) {
			lg(HIBR_LERR, "fetch: %s: %s", ofile, strerror(errno));
			goto fail;
		}
	}
	out.failed = 0;
	out.fd = ofd >= 0 ? ofd : 1;
	if (ofd >= 0 || !s->bind) {
		fflush(stdout);
		rq.ondata = ft_tofd;
		rq.ctx = &out;
	}
	/* An interactive shell's interrupt, so ctrl-c stops a request waiting
	   on a slow server rather than the shell waiting it out. One that
	   came before this request is not for it. */
	{
		volatile int *gi = (volatile int *)dlsym(RTLD_DEFAULT, "g_int");

		if (gi) {
			*gi = 0;
			rq.cancel = gi;
		}
	}
	rq.method = meth.p;
	rq.url = url;
	rq.hdr = (const char **)hdr.p;
	rq.nhdr = hdr.n;
	rc = ft_do(&rq, &rs);
	if (out.failed && rs.err && !strcmp(rs.err, "cancelled")) {
		free(rs.err);
		rs.err = 0;
		s_fmt(&num, "cannot write what arrived: %s", strerror(out.failed));
		rs.err = xs(num.p);
		num.n = 0;
	}
	s_fmt(&num, "%d", rs.code);
	hibr_set(s, "FETCH_STATUS", num.p, 0);
	if (s->bind) {
		v_del(s, "RET");
		hibr_set(s, "RET", "", 0);
		num.n = 0;
		s_fmt(&num, "%d", rs.code);
		ft_retset(s, "status", 0, num.p);
		ft_retset(s, "reason", 0, rs.reason);
		ft_retset(s, "url", 0, rs.url);
		ft_retset(s, "type", 0, rs.ctype);
		num.n = 0;
		s_fmt(&num, "%d", rs.redirects);
		ft_retset(s, "redirects", 0, num.p);
		num.n = 0;
		s_fmt(&num, "%lld", rs.length);
		ft_retset(s, "length", 0, num.p);
		if (rs.err)
			ft_retset(s, "error", 0, rs.err);
		ft_rethdrs(s, &rs);
		if (ofd < 0) {
			s_grow(&rs.body, 1);
			rs.body.p[rs.body.n] = 0;
			ft_retset(s, "body", 0, rs.body.p);
		}
	}
	if (rc != 0)
		lg(HIBR_LERR, "fetch: %s: %s", url, rs.err ? rs.err : "failed");
	i = rc == 0 && rs.code < 400 ? HIBR_OK : HIBR_FAIL;
	ft_resfree(&rs);
	if (ofd >= 0)
		close(ofd);
	if (ffd >= 0)
		close(ffd);
	v_free(&hdr);
	s_free(&meth);
	s_free(&user);
	s_free(&num);
	return i;
usage:
	lg(HIBR_LERR, "usage: fetch [get|head|post|put|patch|delete|options] URL "
		      "[-H 'Name: value']... [-d body | -f file] [-o file] "
		      "[-j jar] [-u user:pass] [-t seconds] [-k] [-n] [-i]; "
		      "fetch jar clear NAME");
	v_free(&hdr);
	s_free(&meth);
	s_free(&user);
	s_free(&num);
	return 2;
fail:
	if (ffd >= 0)
		close(ffd);
	v_free(&hdr);
	s_free(&meth);
	s_free(&user);
	s_free(&num);
	return HIBR_FAIL;
}

/* Offer the interface, so a module that asks for "fetch" finds it. */
int ft_ini(sh *s)
{
	return hibr_provide(s, "fetch", FT_API_VER, &ft_tab);
}

/* Close every connection, forget every login and jar, and take the
   interface back, when the module goes. */
void ft_fini(sh *s)
{
	ft_closeall();
	ft_authfree();
	ft_jarfree();
	hibr_unprovide(s, "fetch");
}

const hibr_bi fetch_bi[] = {
	{ "fetch", m_fetch, "an HTTP client: a request, its reply printed, written to a file or as a map" },
	HIBR_BI_END
};

HIBR_MODULE_P("fetch", "1.0", "an HTTP client for scripts and modules",
	      fetch_bi, ft_ini, ft_fini, "fetch");
