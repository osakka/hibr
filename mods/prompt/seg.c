#include "pr.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Read a configuration value, with no segment naming a top level key. */
const char *pr_cfg(pctx *c, const char *sg, const char *key)
{
	char *ks[2];
	const char *v;
	int n = 0;

	if (sg)
		ks[n++] = (char *)sg;
	ks[n++] = (char *)key;
	v = hibr_getp(c->s, "PROMPT", ks, n);
	return v && *v ? v : 0;
}

/* Read a top level configuration value. */
const char *pr_top(pctx *c, const char *key)
{
	return pr_cfg(c, 0, key);
}

/* Read a numeric configuration value, falling back to a default. */
int pr_cfgi(pctx *c, const char *sg, const char *key, int dflt)
{
	const char *v = pr_cfg(c, sg, key);

	return v ? atoi(v) : dflt;
}

/* Read a variable from the shell, then from the environment. */
const char *pr_env(pctx *c, const char *nm)
{
	const char *v = hibr_get(c->s, nm);

	if (!v || !*v)
		v = getenv(nm);
	return v && *v ? v : 0;
}

/* Read at most max bytes of a file into fresh memory, or NULL. */
char *pr_slurp(const char *path, size_t max)
{
	FILE *f = fopen(path, "rb");
	str o;
	char *buf;
	size_t n;

	if (!f)
		return 0;
	s_init(&o);
	buf = xm(1024);
	while (o.n < max && (n = fread(buf, 1, 1024, f)) > 0)
		s_add(&o, buf, n);
	free(buf);
	fclose(f);
	if (!o.p) {
		s_grow(&o, 1);
		o.p[0] = 0;
	}
	return o.p;
}

/* Copy the last path component. */
char *pr_base(const char *path)
{
	const char *b = strrchr(path, '/');

	return xs(b && b[1] ? b + 1 : path);
}

/* Pull a value out of key: value text, or out of flat JSON. */
char *pr_field(const char *txt, const char *key, int json)
{
	size_t kl = strlen(key);
	const char *p = txt;

	while (p && *p) {
		const char *q = p;
		while (*q == ' ' || *q == '\t')
			q++;
		if (json && *q == '"' && !strncmp(q + 1, key, kl) &&
		    q[kl + 1] == '"')
			q += kl + 2;
		else if (!json && !strncmp(q, key, kl))
			q += kl;
		else {
			p = strchr(p, '\n');
			p = p ? p + 1 : 0;
			continue;
		}
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q != ':' && *q != '=') {
			p = strchr(p, '\n');
			p = p ? p + 1 : 0;
			continue;
		}
		q++;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q == '"') {
			const char *b = ++q;
			while (*q && *q != '"')
				q++;
			return pr_span(b, q);
		}
		{
			const char *b = q;
			while (*q && *q != '\n' && *q != ',' && *q != '\r')
				q++;
			while (q > b && (q[-1] == ' ' || q[-1] == '\t'))
				q--;
			return pr_span(b, q);
		}
	}
	return 0;
}

/* True when the working directory holds one of the listed markers. */
int sg_marker(pctx *c, const char *list)
{
	const char *p = list;

	while (p && *p) {
		const char *b = p;
		char *nm;
		int hit = 0;
		while (*p && *p != ' ')
			p++;
		nm = pr_span(b, p);
		if (*nm == '*' && nm[1]) {
			DIR *d = opendir(c->cwd ? c->cwd : ".");
			struct dirent *e;
			size_t sl = strlen(nm + 1);
			while (d && (e = readdir(d)) != 0) {
				size_t l = strlen(e->d_name);
				if (l > sl && !strcmp(e->d_name + l - sl, nm + 1)) {
					hit = 1;
					break;
				}
			}
			if (d)
				closedir(d);
		} else {
			str q;
			s_init(&q);
			s_cat(&q, c->cwd ? c->cwd : ".");
			s_ch(&q, '/');
			s_cat(&q, nm);
			hit = access(q.p, F_OK) == 0;
			s_free(&q);
		}
		free(nm);
		if (hit)
			return 1;
		while (*p == ' ')
			p++;
	}
	return 0;
}

/* Format a millisecond duration the way a person reads it. */
char *sg_dur(long ms)
{
	str o;
	long sec = ms / 1000;

	s_init(&o);
	if (ms < 1000) {
		s_num(&o, ms);
		s_cat(&o, "ms");
		return o.p;
	}
	if (sec >= 3600) {
		s_num(&o, sec / 3600);
		s_ch(&o, 'h');
	}
	if (sec >= 60) {
		s_num(&o, (sec % 3600) / 60);
		s_ch(&o, 'm');
		s_num(&o, sec % 60);
		s_ch(&o, 's');
		return o.p;
	}
	s_num(&o, sec);
	s_ch(&o, '.');
	s_num(&o, (ms % 1000) / 100);
	s_ch(&o, 's');
	return o.p;
}

/* Build the directory path, home relative and truncated. */
char *sg_path(pctx *c)
{
	const char *ts = pr_cfg(c, "dir", "truncation_symbol");
	char *rel = gt_rel(c);
	const char *p = rel ? rel : (c->cwd ? c->cwd : "?");
	int tr = pr_cfgi(c, "dir", "truncate", 3);
	size_t hl = c->home ? strlen(c->home) : 0;
	int home = 0;
	vec v = { 0, 0, 0 };
	str o;

	if (!ts)
		ts = "\342\200\246/";
	s_init(&o);
	if (!rel && hl && !strncmp(p, c->home, hl) &&
	    (p[hl] == 0 || p[hl] == '/')) {
		home = 1;
		p += hl;
	}
	if (tr > 0) {
		const char *r = p;
		while (*r) {
			if (*r == '/')
				v_add(&v, (void *)(r + 1));
			r++;
		}
		if ((int)v.n > tr) {
			s_cat(&o, ts);
			s_cat(&o, (char *)v.p[v.n - tr]);
			v_free(&v);
			free(rel);
			return o.p;
		}
	}
	v_free(&v);
	if (home)
		s_ch(&o, '~');
	s_cat(&o, p);
	if (!o.n)
		s_ch(&o, '/');
	free(rel);
	return o.p;
}

/* Report the battery charge and charging state. */
int sg_bat(int *pct, char **state)
{
	DIR *d = opendir("/sys/class/power_supply");
	struct dirent *e;
	int got = 0;

	*pct = 0;
	*state = 0;
	while (d && (e = readdir(d)) != 0) {
		str b;
		char *cap, *st;
		if (strncmp(e->d_name, "BAT", 3))
			continue;
		s_init(&b);
		s_cat(&b, "/sys/class/power_supply/");
		s_cat(&b, e->d_name);
		s_cat(&b, "/capacity");
		cap = pr_slurp(b.p, 32);
		b.n -= 8;
		b.p[b.n] = 0;
		s_cat(&b, "status");
		st = pr_slurp(b.p, 32);
		s_free(&b);
		if (cap) {
			*pct = atoi(cap);
			got = 1;
		}
		free(cap);
		if (st) {
			char *nl = strchr(st, '\n');
			if (nl)
				*nl = 0;
			free(*state);
			*state = st;
		}
		if (got)
			break;
	}
	if (d)
		closedir(d);
	return got;
}

/* Report used memory as a percentage of the total. */
int sg_mem(void)
{
	char *t = pr_slurp("/proc/meminfo", 4096);
	char *tot, *av;
	long lt, la;

	if (!t)
		return -1;
	tot = pr_field(t, "MemTotal", 0);
	av = pr_field(t, "MemAvailable", 0);
	free(t);
	lt = tot ? atol(tot) : 0;
	la = av ? atol(av) : 0;
	free(tot);
	free(av);
	if (lt <= 0)
		return -1;
	return (int)(100 - la * 100 / lt);
}

/* Supply the prompt character style, which follows the last status. */
char *sg_charv(pctx *c, const seg *g, const char *f)
{
	const char *v;

	if (strcmp(f, "style") || !c->st)
		return 0;
	v = pr_cfg(c, g->nm, "error_style");
	return xs(v ? v : "bold red");
}

/* Supply the directory fields. */
char *sg_dirv(pctx *c, const seg *g, const char *f)
{
	(void)g;
	if (!strcmp(f, "path"))
		return sg_path(c);
	if (!strcmp(f, "read_only"))
		return access(c->cwd ? c->cwd : ".", W_OK) == 0 ? 0 : xs("\360\237\224\222");
	return 0;
}

/* True when the shell is running over ssh or as root. */
int sg_remote(pctx *c)
{
	return pr_env(c, "SSH_CONNECTION") || pr_env(c, "SSH_CLIENT") ||
	       pr_env(c, "SSH_TTY");
}

/* Show the user name when it is worth showing. */
int sg_usera(pctx *c, const seg *g)
{
	return pr_cfgi(c, g->nm, "show_always", 0) || geteuid() == 0 ||
	       sg_remote(c);
}

/* Supply the user name. */
char *sg_userv(pctx *c, const seg *g, const char *f)
{
	const char *v;

	(void)g;
	if (strcmp(f, "user"))
		return 0;
	v = pr_env(c, "USER");
	if (!v)
		v = pr_env(c, "LOGNAME");
	return v ? xs(v) : 0;
}

/* Show the host name over ssh, or when asked. */
int sg_hosta(pctx *c, const seg *g)
{
	return pr_cfgi(c, g->nm, "show_always", 0) || sg_remote(c);
}

/* Supply the host name. */
char *sg_hostv(pctx *c, const seg *g, const char *f)
{
	char *h;
	char *d;

	if (strcmp(f, "hostname"))
		return 0;
	h = xm(256);
	if (gethostname(h, 255) != 0) {
		free(h);
		return 0;
	}
	h[255] = 0;
	if (!pr_cfgi(c, g->nm, "full", 0) && (d = strchr(h, '.')) != 0)
		*d = 0;
	return h;
}

/* Show a failing status. */
int sg_stata(pctx *c, const seg *g)
{
	(void)g;
	return c->st != 0;
}

/* Supply the exit status. */
char *sg_statv(pctx *c, const seg *g, const char *f)
{
	str o;

	(void)g;
	if (strcmp(f, "int"))
		return 0;
	s_init(&o);
	s_num(&o, c->st);
	return o.p;
}

/* Show a command duration once it is long enough to notice. */
int sg_dura(pctx *c, const seg *g)
{
	return c->dur >= pr_cfgi(c, g->nm, "min", 2000);
}

/* Supply the command duration. */
char *sg_durv(pctx *c, const seg *g, const char *f)
{
	(void)g;
	return strcmp(f, "time") ? 0 : sg_dur(c->dur);
}

/* Show the job count when jobs are running. */
int sg_jobsa(pctx *c, const seg *g)
{
	return (int)c->s->jobs.n >= pr_cfgi(c, g->nm, "threshold", 1);
}

/* Supply the job count. */
char *sg_jobsv(pctx *c, const seg *g, const char *f)
{
	str o;

	(void)g;
	if (strcmp(f, "number"))
		return 0;
	s_init(&o);
	s_num(&o, (long)c->s->jobs.n);
	return o.p;
}

/* Supply the wall clock time. */
char *sg_timev(pctx *c, const seg *g, const char *f)
{
	const char *tf = pr_cfg(c, g->nm, "time_format");
	time_t now = time(0);
	struct tm *tm = localtime(&now);
	str o;

	if (strcmp(f, "time"))
		return 0;
	s_init(&o);
	s_grow(&o, 128);
	o.n = strftime(o.p, 128, tf ? tf : "%H:%M:%S", tm);
	o.p[o.n] = 0;
	return o.p;
}

/* Show nested shells past the configured depth. */
int sg_shlvla(pctx *c, const seg *g)
{
	const char *v = pr_env(c, "SHLVL");

	return v && atoi(v) >= pr_cfgi(c, g->nm, "threshold", 2);
}

/* Supply the shell nesting depth. */
char *sg_shlvlv(pctx *c, const seg *g, const char *f)
{
	const char *v = pr_env(c, "SHLVL");

	(void)g;
	return strcmp(f, "shlvl") || !v ? 0 : xs(v);
}

/* Show a segment driven by the presence of an environment variable. */
int sg_enva(pctx *c, const seg *g)
{
	return pr_env(c, g->aux) != 0;
}

/* Supply the value of the segment's environment variable. */
char *sg_envv(pctx *c, const seg *g, const char *f)
{
	const char *v = pr_env(c, g->aux);

	if (!v)
		return 0;
	if (!strcmp(f, "env"))
		return xs(v);
	if (!strcmp(f, "name"))
		return pr_base(v);
	return 0;
}

/* Supply the aws profile and region. */
char *sg_awsv(pctx *c, const seg *g, const char *f)
{
	const char *v;

	(void)g;
	if (!strcmp(f, "profile")) {
		v = pr_env(c, "AWS_PROFILE");
		if (!v)
			v = pr_env(c, "AWS_VAULT");
		return v ? xs(v) : 0;
	}
	if (!strcmp(f, "region")) {
		v = pr_env(c, "AWS_REGION");
		if (!v)
			v = pr_env(c, "AWS_DEFAULT_REGION");
		return v ? xs(v) : 0;
	}
	return 0;
}

/* Show the aws segment when a profile or region is set. */
int sg_awsa(pctx *c, const seg *g)
{
	(void)g;
	return pr_env(c, "AWS_PROFILE") || pr_env(c, "AWS_VAULT") ||
	       pr_env(c, "AWS_REGION") || pr_env(c, "AWS_DEFAULT_REGION");
}

/* Read the active kubernetes context. */
char *sg_kubectx(pctx *c)
{
	const char *kc = pr_env(c, "KUBECONFIG");
	str b;
	char *t, *r;

	s_init(&b);
	if (kc) {
		const char *e = strchr(kc, ':');
		s_add(&b, kc, e ? (size_t)(e - kc) : strlen(kc));
	} else {
		if (!c->home)
			return 0;
		s_cat(&b, c->home);
		s_cat(&b, "/.kube/config");
	}
	t = pr_slurp(b.p, 262144);
	s_free(&b);
	if (!t)
		return 0;
	r = pr_field(t, "current-context", 0);
	free(t);
	if (r && !*r) {
		free(r);
		return 0;
	}
	return r;
}

/* Show the kubernetes segment when a context is selected. */
int sg_kubea(pctx *c, const seg *g)
{
	char *v = sg_kubectx(c);

	(void)g;
	free(v);
	return v != 0;
}

/* Supply the kubernetes context. */
char *sg_kubev(pctx *c, const seg *g, const char *f)
{
	(void)g;
	return strcmp(f, "context") ? 0 : sg_kubectx(c);
}

/* Read the active docker context. */
char *sg_dockctx(pctx *c)
{
	const char *v = pr_env(c, "DOCKER_CONTEXT");
	str b;
	char *t, *r;

	if (v)
		return xs(v);
	if (!c->home)
		return 0;
	s_init(&b);
	s_cat(&b, c->home);
	s_cat(&b, "/.docker/config.json");
	t = pr_slurp(b.p, 65536);
	s_free(&b);
	if (!t)
		return 0;
	r = pr_field(t, "currentContext", 1);
	free(t);
	if (r && (!*r || !strcmp(r, "default"))) {
		free(r);
		return 0;
	}
	return r;
}

/* Show the docker segment when a context is selected. */
int sg_docka(pctx *c, const seg *g)
{
	char *v = sg_dockctx(c);

	(void)g;
	free(v);
	return v != 0;
}

/* Supply the docker context. */
char *sg_dockv(pctx *c, const seg *g, const char *f)
{
	(void)g;
	return strcmp(f, "context") ? 0 : sg_dockctx(c);
}

/* Supply the operating system name. */
char *sg_osv(pctx *c, const seg *g, const char *f)
{
	char *t, *r;

	(void)c;
	(void)g;
	if (strcmp(f, "name"))
		return 0;
	t = pr_slurp("/etc/os-release", 8192);
	if (!t)
		return 0;
	r = pr_field(t, "NAME", 0);
	free(t);
	return r;
}

/* Show the container segment inside a container. */
int sg_conta(pctx *c, const seg *g)
{
	(void)c;
	(void)g;
	return access("/run/.containerenv", F_OK) == 0 ||
	       access("/.dockerenv", F_OK) == 0;
}

/* Show the battery segment when the charge is low. */
int sg_bata(pctx *c, const seg *g)
{
	int pct;
	char *st;

	if (!sg_bat(&pct, &st))
		return 0;
	free(st);
	return pct <= pr_cfgi(c, g->nm, "threshold", 20);
}

/* Supply the battery charge and state. */
char *sg_batv(pctx *c, const seg *g, const char *f)
{
	int pct;
	char *st;
	str o;

	(void)c;
	(void)g;
	if (!sg_bat(&pct, &st))
		return 0;
	if (!strcmp(f, "percentage")) {
		free(st);
		s_init(&o);
		s_num(&o, pct);
		s_ch(&o, '%');
		return o.p;
	}
	if (!strcmp(f, "state"))
		return st;
	free(st);
	return 0;
}

/* Show the memory segment once usage passes the threshold. */
int sg_mema(pctx *c, const seg *g)
{
	int u = sg_mem();

	return u >= 0 && u >= pr_cfgi(c, g->nm, "threshold", 75);
}

/* Supply the memory usage. */
char *sg_memv(pctx *c, const seg *g, const char *f)
{
	int u = sg_mem();
	str o;

	(void)c;
	(void)g;
	if (strcmp(f, "ram_pct") || u < 0)
		return 0;
	s_init(&o);
	s_num(&o, u);
	s_ch(&o, '%');
	return o.p;
}

/* Show a language segment when its marker files are present. */
int sg_langa(pctx *c, const seg *g)
{
	return sg_marker(c, g->aux);
}

const seg pr_segs[] = {
	{ "user", "[$user]($style)", "bold yellow", "", 0, 0,
	  sg_usera, sg_userv },
	{ "host", "[@$hostname]($style) ", "bold green", "", 0, 0,
	  sg_hosta, sg_hostv },
	{ "dir", "[$path]($style)[$read_only]($read_only_style) ",
	  "bold cyan", "", 0, 0, 0, sg_dirv },
	{ "git", "[$symbol$branch]($style)( [$state]($state_style))"
		 "( [$ahead_behind]($ahead_behind_style))"
		 "( [$status]($status_style)) ",
	  "bold purple", "", 0, 0, gt_act, gt_val },
	{ "duration", "[$symbol$time]($style) ", "yellow", "took ", 0, 0,
	  sg_dura, sg_durv },
	{ "jobs", "[$symbol$number]($style) ", "bold blue", "\342\234\246", 0,
	  0, sg_jobsa, sg_jobsv },
	{ "status", "[$symbol$int]($style) ", "bold red", "\342\234\230", 0, 0,
	  sg_stata, sg_statv },
	{ "char", "[$symbol]($style) ", "bold green", "\342\235\257", 0, 0,
	  0, sg_charv },
	{ "time", "[$symbol$time]($style) ", "bold bright-black", "", 0, 1,
	  0, sg_timev },
	{ "shlvl", "[$symbol$shlvl]($style) ", "bold yellow", "lvl ", 0, 1,
	  sg_shlvla, sg_shlvlv },
	{ "venv", "[$symbol$name]($style) ", "bold green", "py ",
	  "VIRTUAL_ENV", 0, sg_enva, sg_envv },
	{ "conda", "[$symbol$env]($style) ", "bold green", "conda ",
	  "CONDA_DEFAULT_ENV", 0, sg_enva, sg_envv },
	{ "nix", "[$symbol$env]($style) ", "bold blue", "nix ",
	  "IN_NIX_SHELL", 0, sg_enva, sg_envv },
	{ "aws", "[$symbol$profile(@$region)]($style) ", "bold yellow",
	  "aws ", 0, 0, sg_awsa, sg_awsv },
	{ "kube", "[$symbol$context]($style) ", "bold cyan", "k8s ", 0, 1,
	  sg_kubea, sg_kubev },
	{ "docker", "[$symbol$context]($style) ", "bold blue", "docker ", 0,
	  0, sg_docka, sg_dockv },
	{ "os", "[$symbol$name]($style) ", "bold white", "", 0, 1, 0, sg_osv },
	{ "container", "[$symbol]($style) ", "bold red", "container", 0, 0,
	  sg_conta, 0 },
	{ "battery", "[$symbol$percentage]($style) ", "bold red", "bat ", 0,
	  0, sg_bata, sg_batv },
	{ "memory", "[$symbol$ram_pct]($style) ", "bold dimmed white",
	  "mem ", 0, 1, sg_mema, sg_memv },
	{ "rust", "[$symbol]($style) ", "bold red", "rust", "Cargo.toml", 0,
	  sg_langa, 0 },
	{ "node", "[$symbol]($style) ", "bold green", "node",
	  "package.json .nvmrc", 0, sg_langa, 0 },
	{ "golang", "[$symbol]($style) ", "bold cyan", "go",
	  "go.mod go.sum", 0, sg_langa, 0 },
	{ "python", "[$symbol]($style) ", "bold yellow", "py",
	  "pyproject.toml requirements.txt setup.py", 0, sg_langa, 0 },
	{ "c", "[$symbol]($style) ", "bold blue", "c",
	  "Makefile CMakeLists.txt", 0, sg_langa, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* Find a segment by name. */
const seg *pr_find(const char *nm)
{
	const seg *g;

	for (g = pr_segs; g->nm; g++)
		if (!strcmp(g->nm, nm))
			return g;
	return 0;
}

/* Look up one field of a segment being rendered. */
char *pr_fldlk(pctx *c, void *ud, const char *nm)
{
	struct sgc *x = (struct sgc *)ud;
	const char *v;
	char *r;

	if (x->g->val) {
		r = x->g->val(c, x->g, nm);
		if (r)
			return r;
	}
	v = pr_cfg(c, x->g->nm, nm);
	if (v)
		return xs(v);
	if (!strcmp(nm, "symbol"))
		return xs(x->g->sym ? x->g->sym : "");
	if (!strcmp(nm, "style"))
		return xs(x->g->style ? x->g->style : "");
	return 0;
}

/* Render one segment, or nothing when it does not apply. */
char *pr_seg(pctx *c, const char *nm)
{
	const seg *g = pr_find(nm);
	struct sgc x;
	const char *f, *p;
	int any = 0;

	if (!g) {
		lg(HIBR_LDBG, "prompt: no segment named %s", nm);
		return 0;
	}
	if (pr_cfgi(c, nm, "disabled", g->off))
		return 0;
	if (g->act && !g->act(c, g))
		return 0;
	f = pr_cfg(c, nm, "format");
	if (!f)
		f = g->fmt;
	x.c = c;
	x.g = g;
	p = f;
	return pr_fmt(c, &p, 0, 0, &any, pr_fldlk, &x);
}

/* Look up a top level variable as a rendered segment. */
char *pr_look(pctx *c, void *ud, const char *nm)
{
	(void)ud;
	return pr_seg(c, nm);
}

/* Render the whole prompt from the top level format. */
char *pr_render(pctx *c)
{
	const char *f = pr_top(c, "format");
	const char *p;
	char *r;
	int any = 0;

	if (!f)
		f = "$user$host$dir$git$duration$status$jobs$char";
	p = f;
	r = pr_fmt(c, &p, 0, 0, &any, pr_look, 0);
	if (pr_cfgi(c, 0, "newline", 0)) {
		str o;
		s_init(&o);
		s_ch(&o, '\n');
		s_cat(&o, r);
		free(r);
		r = o.p;
	}
	lg(HIBR_LDBG, "prompt rendered %lu bytes", (unsigned long)strlen(r));
	return r;
}
