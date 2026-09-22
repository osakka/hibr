#define _GNU_SOURCE

#include "hibr.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

/* Read a whole file, which for /proc cannot be sized in advance. */
char *si_slurp(const char *path)
{
	int fd = open(path, O_RDONLY);
	str b;
	char t[8192];
	ssize_t k;

	if (fd < 0)
		return 0;
	s_init(&b);
	while ((k = read(fd, t, sizeof t)) > 0)
		s_add(&b, t, (size_t)k);
	close(fd);
	if (!b.p) {
		b.p = xm(1);
		b.p[0] = 0;
	}
	b.p[b.n] = 0;
	return b.p;
}

/* A KEY=value or KEY="value" line out of an os-release style file. */
void si_rel(const char *t, const char *key, str *out)
{
	const char *p = t;
	size_t kn = strlen(key);

	while (p && *p) {
		if (!strncmp(p, key, kn) && p[kn] == '=') {
			p += kn + 1;
			if (*p == '"')
				p++;
			while (*p && *p != '"' && *p != '\n')
				s_ch(out, *p++);
			return;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
}

/* The value after a key in a "Key: 1234 kB" file. */
unsigned long long si_field(const char *t, const char *key)
{
	const char *p = t ? strstr(t, key) : 0;

	if (!p)
		return 0;
	p += strlen(key);
	while (*p && !isdigit((unsigned char)*p))
		p++;
	return strtoull(p, 0, 10);
}

/* The first "key : value" out of /proc/cpuinfo. */
void si_cpuinfo(const char *t, const char *key, str *out)
{
	const char *p = t ? strstr(t, key) : 0;

	if (!p)
		return;
	p = strchr(p, ':');
	if (!p)
		return;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	while (*p && *p != '\n')
		s_ch(out, *p++);
	while (out->n && out->p[out->n - 1] == ' ')
		out->n--;
	if (out->p)
		out->p[out->n] = 0;
}

/* A size in whatever unit keeps it short. */
void si_bytes(str *o, double v)
{
	static const char *u[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	int i = 0;
	long w;

	while (v >= 1024 && i < 4) {
		v /= 1024;
		i++;
	}
	w = (long)(v * 10 + 0.5);
	s_num(o, w / 10);
	s_ch(o, '.');
	s_num(o, w % 10);
	s_ch(o, ' ');
	s_cat(o, u[i]);
}

/* How long the machine has been up, in words. */
void si_uptime(str *o, double s)
{
	long d = (long)(s / 86400), h = (long)(s / 3600) % 24;
	long m = (long)(s / 60) % 60;
	int any = 0;

	if (d) {
		s_num(o, d);
		s_cat(o, d == 1 ? " day" : " days");
		any = 1;
	}
	if (h) {
		if (any)
			s_cat(o, ", ");
		s_num(o, h);
		s_cat(o, h == 1 ? " hour" : " hours");
		any = 1;
	}
	if (!any || m) {
		if (any)
			s_cat(o, ", ");
		s_num(o, m);
		s_cat(o, m == 1 ? " minute" : " minutes");
	}
}

static const char *si_hibr[] = {
	"        \\\\   //        ",
	"         \\\\ //         ",
	"    ======\\X/======    ",
	"         // \\\\         ",
	"        //   \\\\        ",
	"     _   _ _   _       ",
	"    | |_(_) |_| |__    ",
	"    | ' \\ | '_ \\ '_|   ",
	"    |_||_|_|_.__/_|    ",
	"                       ",
	0
};

static const char *si_debian[] = {
	"       _,met$$$$$gg.    ",
	"    ,g$$$$$$$$$$$$$$$P. ",
	"  ,g$$P\"     \"\"\"Y$$.\". ",
	" ,$$P'              `$$$.",
	"',$$P       ,ggs.     `$$b:",
	"`d$$'     ,$P\"'   .    $$$",
	" $$P      d$'     ,    $$P",
	" $$:      $$.   -    ,d$$'",
	" $$;      Y$b._   _,d$P'  ",
	" Y$$.    `.`\"Y$$$$P\"'    ",
	" `$$b      \"-.__         ",
	"  `Y$$b                  ",
	0
};

static const char *si_alpine[] = {
	"       .hddddddddddddddddddddddh.  ",
	"      :dddddddddddddddddddddddddd: ",
	"     /dddddddddddddddddddddddddddd/",
	"    +dddddddddddddddddddddddddddddd+",
	"  `sdddddddddddddddddddddddddddddddds`",
	" `ydddddddddddd++hdddddddddddddddddddy`",
	".hddddddddddd+`  `+ddddh:-sdddddddddddh.",
	"hdddddddddd+`      `+y:    .sddddddddddh",
	"ddddddddh+`   `//`   `.` `:ohddddddddddd",
	"dddddh+.  `/hddh/`   `:h+ `.+hddddddddddd",
	0
};

/* The picture for this system, or the shell's own. */
const char **si_logo(const char *id)
{
	if (!id || !*id)
		return si_hibr;
	if (!strcmp(id, "debian") || !strcmp(id, "raspbian"))
		return si_debian;
	if (!strcmp(id, "alpine"))
		return si_alpine;
	return si_hibr;
}

/* Add one labelled line to the list. */
void si_row(vec *v, const char *k, const char *val)
{
	str *r = xm(sizeof *r);

	s_init(r);
	if (k) {
		s_cat(r, k);
		s_cat(r, "\t");
	}
	s_cat(r, val ? val : "");
	v_add(v, r);
}

/* Show what this machine is. */
int m_sysinfo(sh *s, int ac, char **av)
{
	int tty = isatty(1), plain = 0, i;
	struct utsname un;
	char *rel, *cpu, *mem;
	str id, pretty, t, u;
	vec rows;
	const char **logo;
	size_t k, n, wide;
	unsigned long long mt, ma;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-p")) {
			plain = 1;
		} else {
			lg(HIBR_LERR, "usage: sysinfo [-p]");
			return 2;
		}
	}
	if (plain)
		tty = 0;
	rows.p = 0;
	rows.n = 0;
	rows.cap = 0;
	s_init(&id);
	s_init(&pretty);
	s_init(&t);
	s_init(&u);
	rel = si_slurp("/etc/os-release");
	if (rel) {
		si_rel(rel, "ID", &id);
		si_rel(rel, "PRETTY_NAME", &pretty);
		if (!pretty.n)
			si_rel(rel, "NAME", &pretty);
	}
	uname(&un);

	{
		char hn[128];
		const char *usr = hibr_get(s, "USER");
		if (gethostname(hn, sizeof hn) != 0)
			strcpy(hn, "?");
		hn[sizeof hn - 1] = 0;
		t.n = 0;
		s_cat(&t, usr && *usr ? usr : "?");
		s_ch(&t, '@');
		s_cat(&t, hn);
		si_row(&rows, 0, t.p);
		n = t.n;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		for (k = 0; k < n; k++)
			s_ch(&t, '-');
		si_row(&rows, 0, t.p);
	}
	si_row(&rows, "OS", pretty.n ? pretty.p : un.sysname);
	si_row(&rows, "Kernel", un.release);
	si_row(&rows, "Arch", un.machine);
	{
		char *up = si_slurp("/proc/uptime");
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		si_uptime(&t, up ? strtod(up, 0) : 0);
		free(up);
		si_row(&rows, "Uptime", t.p);
	}
	{
		const char *sh_ = hibr_get(s, "HIBR_VERSION");
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		s_cat(&t, "hibr ");
		s_cat(&t, sh_ && *sh_ ? sh_ : HIBR_VER);
		s_cat(&t, " (abi ");
		s_num(&t, (long)HIBR_ABI);
		s_ch(&t, ')');
		si_row(&rows, "Shell", t.p);
	}
	{
		const char *tm = hibr_get(s, "TERM");
		si_row(&rows, "Terminal", tm && *tm ? tm : "none");
	}
	cpu = si_slurp("/proc/cpuinfo");
	if (cpu) {
		const char *p;
		long ncpu = 0;
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		si_cpuinfo(cpu, "model name", &t);
		if (!t.n)
			si_cpuinfo(cpu, "Model", &t);
		for (p = cpu; (p = strstr(p, "processor")) != 0; p++)
			ncpu++;
		if (ncpu) {
			s_cat(&t, " (");
			s_num(&t, ncpu);
			s_ch(&t, ')');
		}
		si_row(&rows, "CPU", t.n ? t.p : un.machine);
		free(cpu);
	}
	mem = si_slurp("/proc/meminfo");
	mt = si_field(mem, "MemTotal:");
	ma = si_field(mem, "MemAvailable:");
	free(mem);
	if (mt) {
		t.n = 0;
		if (t.p)
			t.p[0] = 0;
		si_bytes(&t, (double)(mt - ma) * 1024);
		s_cat(&t, " / ");
		si_bytes(&t, (double)mt * 1024);
		si_row(&rows, "Memory", t.p);
	}
	{
		struct statvfs v;
		if (statvfs("/", &v) == 0 && v.f_blocks) {
			double total = (double)v.f_blocks * (double)v.f_frsize;
			double free_ = (double)v.f_bavail * (double)v.f_frsize;
			t.n = 0;
			if (t.p)
				t.p[0] = 0;
			si_bytes(&t, total - free_);
			s_cat(&t, " / ");
			si_bytes(&t, total);
			s_cat(&t, "  (/)");
			si_row(&rows, "Disk", t.p);
		}
	}
	{
		char *ld = si_slurp("/proc/loadavg");
		if (ld) {
			char *e = strchr(ld, ' ');
			if (e) {
				e = strchr(e + 1, ' ');
				if (e) {
					e = strchr(e + 1, ' ');
					if (e)
						*e = 0;
				}
			}
			si_row(&rows, "Load", ld);
			free(ld);
		}
	}

	logo = si_logo(id.p);
	for (n = 0; logo[n]; n++)
		;
	wide = 0;
	for (k = 0; k < n; k++)
		if (strlen(logo[k]) > wide)
			wide = strlen(logo[k]);
	for (k = 0; k < n || k < rows.n; k++) {
		size_t pad = wide;
		u.n = 0;
		if (u.p)
			u.p[0] = 0;
		if (k < n) {
			if (tty)
				s_cat(&u, "\033[38;5;110m");
			s_cat(&u, logo[k]);
			if (tty)
				s_cat(&u, "\033[0m");
			pad = wide - strlen(logo[k]);
		}
		while (pad--)
			s_ch(&u, ' ');
		s_cat(&u, "  ");
		if (k < rows.n) {
			str *r = (str *)rows.p[k];
			char *tab = strchr(r->p ? r->p : "", '\t');
			if (tab) {
				*tab = 0;
				if (tty)
					s_cat(&u, "\033[1;38;5;173m");
				s_cat(&u, r->p);
				if (tty)
					s_cat(&u, "\033[0m");
				s_cat(&u, ": ");
				s_cat(&u, tab + 1);
			} else {
				if (tty)
					s_cat(&u, "\033[1;38;5;110m");
				s_cat(&u, r->p ? r->p : "");
				if (tty)
					s_cat(&u, "\033[0m");
			}
		}
		while (u.n && u.p[u.n - 1] == ' ')
			u.n--;
		if (u.p)
			u.p[u.n] = 0;
		printf("%s\n", u.p ? u.p : "");
	}
	if (tty) {
		size_t w = wide;
		printf("\n");
		while (w--)
			printf(" ");
		printf("  ");
		for (i = 0; i < 8; i++)
			printf("\033[48;5;%dm   \033[0m", i);
		printf("\n");
		w = wide;
		while (w--)
			printf(" ");
		printf("  ");
		for (i = 8; i < 16; i++)
			printf("\033[48;5;%dm   \033[0m", i);
		printf("\n");
	}
	for (k = 0; k < rows.n; k++) {
		s_free((str *)rows.p[k]);
		free(rows.p[k]);
	}
	v_free(&rows);
	free(rel);
	s_free(&id);
	s_free(&pretty);
	s_free(&t);
	s_free(&u);
	return HIBR_OK;
}

const hibr_bi sysinfo_bi[] = {
	{ "sysinfo", m_sysinfo, "show what this machine is" },
	HIBR_BI_END
};

HIBR_MODULE("sysinfo", "0.21", "what this machine is, with a picture",
	    sysinfo_bi, 0, 0);
