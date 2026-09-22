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

static const char *si_cix[] = {
	"\001           ▄▄▄▄",
	"\001      ▄▄███████████▄",
	"\001    ▄██████▀▀▀▀▀██████▄           ▄▄██████▄",
	"\001   ████▀▀         ▀█████▄      ▄█████████▀▀",
	"\001  ████▀     \002▄▄\001      ▀█████▄  ▄█████▀",
	"\001 ▄███▀    \002▄█████▄\001     ▀▀██▀▄████▀",
	"\001 ████     \002███████\001        ▄█████",
	"\001 ▀███▄    \002▀█████▀\001      ▄████████▄",
	"\001  ████▄     \002▀▀\001       ▄████▀  ▀████▄▄",
	"\001   ████▄▄         ▄▄████▀      ▀█████▄▄▄▄▄",
	"\001    ▀██████▄▄▄▄▄██████▀          ▀▀███████▀",
	"\001      ▀▀███████████▀▀",
	"\001           ▀▀▀▀",
	0
};

static const char *si_arch[] = {
	"\001                   -`",
	"\001                  .o+`",
	"\001                 `ooo/",
	"\001                `+oooo:",
	"\001               `+oooooo:",
	"\001               -+oooooo+:",
	"\001             `/:-:++oooo+:",
	"\001            `/++++/+++++++:",
	"\001           `/++++++++++++++:",
	"\001          `/+++ooooooooooooo/`",
	"\001         ./ooosssso++osssssso+`",
	"\001        .oossssso-````/ossssss+`",
	"\001       -osssssso.      :ssssssso.",
	"\001      :osssssss/        osssso+++.",
	"\001     /ossssssss/        +ssssooo/-",
	"\001   `/ossssso+/:-        -:/+osssso+-",
	"\001  `+sso+:-`                 `.-/+oso:",
	"\001 `++:.                           `-/+/",
	"\001 .`                                 `/",
	0
};

static const char *si_ubuntu[] = {
	"\001            .-/+oossssoo+/-.",
	"\001        `:+ssssssssssssssssss+:`",
	"\001      -+ssssssssssssssssssyyssss+-",
	"\001    .ossssssssssssssssss\002dMMMNy\001sssso.",
	"\001   /sssssssssss\002hdmmNNmmyNMMMMh\001ssssss/",
	"\001  +sssssssss\002hm\001yd\002MMMMMMMNddddy\001ssssssss+",
	"\001 /ssssssss\002hNMMM\001yh\002hyyyyhmNMMMNh\001ssssssss/",
	"\001.ssssssss\002dMMMNh\001ssssssssss\002hNMMMd\001ssssssss.",
	"\001+ssss\002hhhyNMMNy\001ssssssssssss\002yNMMMy\001sssssss+",
	"\001oss\002yNMMMNyMMh\001ssssssssssssss\002hmmmh\001sssssso",
	"\001oss\002yNMMMNyMMh\001sssssssssssssshmmmh\001sssssso",
	"\001+ssss\002hhhyNMMNy\001ssssssssssss\002yNMMMy\001sssssss+",
	"\001.ssssssss\002dMMMNh\001ssssssssss\002hNMMMd\001ssssssss.",
	"\001 /ssssssss\002hNMMM\001yh\002hyyyyhdNMMMNh\001ssssssss/",
	"\001  +sssssssss\002dm\001yd\002MMMMMMMMddddy\001ssssssss+",
	"\001   /sssssssssss\002hdmNNNNmyNMMMMh\001ssssss/",
	"\001    .ossssssssssssssssss\002dMMMNy\001sssso.",
	"\001      -+sssssssssssssssss\002yyy\001ssss+-",
	"\001        `:+ssssssssssssssssss+:`",
	"\001            .-/+oossssoo+/-.",
	0
};

static const char *si_fedora[] = {
	"\001             .',;::::;,'.",
	"\001         .';:cccccccccccc:;,.",
	"\001      .;cccccccccccccccccccccc;.",
	"\001    .:cccccccccccccccccccccccccc:.",
	"\001  .;ccccccccccccc;\002.:dddl:.\001;ccccccc;.",
	"\001 .:ccccccccccccc;\002OWMKOOXMWd\001;ccccccc:.",
	"\001.:ccccccccccccc;\002KMMc\001;cc;\002xMMc\001;ccccccc:.",
	"\001,cccccccccccccc;\002MMM.\001;cc;\002;WW:\001;cccccccc,",
	"\001:cccccccccccccc;\002MMM.\001;cccccccccccccccc:",
	"\001:ccccccc;\002oxOOOo\001;\002MMM000k.\001;cccccccccccc:",
	"\001cccccc;\0020MMKxdd:\001;\002MMMkddc.\001;cccccccccccc;",
	"\001ccccc;\002XM0'\001;cccc;\002MMM.\001;cccccccccccccccc'",
	"\001ccccc;\002MMo\001;ccccc;\002MMW.\001;ccccccccccccccc;",
	"\001ccccc;\0020MNc.\001ccc\002.xMMd\001;ccccccccccccccc;",
	"\001cccccc;\002dNMWXXXWM0:\001;cccccccccccccc,",
	"\001cccccccc;\002.:odl:.\001;cccccccccccccc:,",
	"\001ccccccccccccccccccccccccccccc:'.",
	"\001:ccccccccccccccccccccccc:;,..",
	"\001 ':cccccccccccccc::;,.",
	0
};

static const char *si_mint[] = {
	"\001 ___________",
	"\001|_          \\",
	"\001  |\002 | _____ \001|",
	"\001  |\002 | | | | \001|",
	"\001  |\002 | | | | \001|",
	"\001  |\002 \\__\001___\002/ \001|",
	"\001  \\_________/",
	0
};

static const char *si_opensuse[] = {
	"\001           .;ldkO0000Okdl;.",
	"\001       .;d00xl:^''''''^:oko,",
	"\001     .d00l'                'o0d.",
	"\001   .d0Kd'\002  Okxol:;,.      \001 :O0d.",
	"\001  .OK\002KKK0kOKKKKKKKKKKOxo:,     \001lKO.",
	"\001 ,0K\002KKKKKKKKKKKKKKK0P^\001,,,\002^dx:\001    ;00,",
	"\001.OK\002KKKKKKKKKKKKKKKk'\001.oOPPb.\002'0k.\001   cKO.",
	"\001:KK\002KKKKKKKKKKKKKKK: \001kKx..dd \002lKd\001   'OK:",
	"\001dKK\002KKKKKKKKKOx0KKKd \001^0KKKO'\002 kKKc\001   dKd",
	"\001dKK\002KKKKKKKKKK;.;oOKx,,^${1;,x;;\002:kKKc\001   dKd",
	"\001:KK\002KKKKKKKKKK0o;...^cdxxOK0O/^^'\001  .0K:",
	"\001 kKK\002KKKKKKKKKKKKK0x:,,......,;od\001  lKk",
	"\001 '0K\002KKKKKKKKKKKKKKKKKKKK00KKOo^\001  c00'",
	"\001  'kK\002KKOxddxkOO00000Okxoc;''\001   .dKk'",
	"\001    l0Ko.                    .c00l'",
	"\001     'l0Kk:.              .;xK0l'",
	"\001        'lkK0xl:;,,,,;:ldO0kl'",
	"\001            '^:ldxkkkkxdl:^'",
	0
};

static const char *si_gentoo[] = {
	"\001         -/oyddmdhs+:.",
	"\001     -o\002dNMMMMMMMMNNmhy+\001-`",
	"\001   -y\002NMMMMMMMMMMMNNNmmdhy\001+-",
	"\001 `o\002mMMMMMMMMMMMMNmdmmmmddhhy\001/`",
	"\001 om\002MMMMMMMMMMMN\001hhyyyo\002hmdddhhhd\001o`",
	"\001.y\002dMMMMMMMMMMd\001hs++so/s\002mdddhhhhdm\001+`",
	"\001 oy\002hdmNMMMMMMMN\001dyooy\002dmddddhhhhyhN\001d.",
	"\001  :o\002yhhdNNMMMMMMMNNNmmdddhhhhhyym\001Mh",
	"\001    .:\002+sydNMMMMMNNNmmmdddhhhhhhmM\001my",
	"\001       /m\002MMMMMMNNNmmmdddhhhhhmMNh\001s:",
	"\001    `o\002NMMMMMMMNNNmmmddddhhdmMNhs\001+`",
	"\001  `s\002NMMMMMMMMNNNmmmdddddmNMmhs\001/.",
	"\001 /N\002MMMMMMMMNNNNmmmdddmNMNdso\001:`",
	"\001+M\002MMMMMMNNNNNmmmmdmNMNdso\001/-",
	"\001yM\002MNNNNNNNmmmmmNNMmhs+/\001-`",
	"\001/h\002MMNNNNNNNNMNdhs++/\001-`",
	"\001`/o\002hdmmddhys+++/:\001.`",
	"\001  `-//////:--.",
	0
};

static const char *si_nixos[] = {
	"\001          ▗▄▄▄       \002▗▄▄▄▄    ▄▄▄▖",
	"\001          ▜███▙       \002▜███▙  ▟███▛",
	"\001           ▜███▙       \002▜███▙▟███▛",
	"\001            ▜███▙       \002▜██████▛",
	"\001     ▟█████████████████▙ \002▜████▛     \001▟▙",
	"\001    ▟███████████████████▙ \002▜███▙    \001▟██▙",
	"\001           ▄▄▄▄▖           \002▜███▙  \001▟███▛",
	"\001          ▟███▛             \002▜██▛ \001▟███▛",
	"\001         ▟███▛               \002▜▛ \001▟███▛",
	"\001▟███████████▛                  \002▟██████████▙",
	"\001▜██████████▛                  \002▟███████████▛",
	"\001      ▟███▛ \002▟▙               ▟███▛",
	"\001     ▟███▛ \002▟██▙             ▟███▛",
	"\001    ▟███▛  \002▜███▙           ▝▀▀▀▀",
	"\001    ▜██▛    \002▜███▙ ▜██████████████████▛",
	"\001     ▜▛     \002▟████▙ ▜████████████████▛",
	"\001           ▟██████▙       \002▜███▙",
	"\001          ▟███▛▜███▙       \002▜███▙",
	"\001         ▟███▛  ▜███▙       \002▜███▙",
	"\001         ▝▀▀▀    ▀▀▀▀▘       \002▀▀▀▘",
	0
};

static const char *si_void[] = {
	"\001                __.;=====;.__",
	"\001            _.=+==++=++=+=+===;.",
	"\001             -=+++=+===+=+=+++++=_",
	"\001        .     -=:``     `--==+=++==.",
	"\001       _vi,    `            --+=++++:",
	"\001      .uvnvi.       _._       -==+==+.",
	"\001     .vvnvnI`    .;==|==;.     :|=||=|.",
	"\001_.   vvnvnnvnI  .;=|===|=;.  .|=||=||=|",
	"\001nvvnvvvnvvnvvI .|=|====|=|.  |=||=||=||",
	"\001`vvnvnvnvnvnI` |=|=====|=|   |=||=||=||",
	"\001 `nvnvnvnvnI`  |=|=====|=|   |=||=||=||",
	"\001   `vnvnvnI`    |=|====|=|   |=||=||=|`",
	"\001     `vnvI`      `;=|==|=;`  `|=||=|`",
	"\001       `v`          `-==-`     `|=|`",
	"\001                                 `",
	0
};

static const char *si_rhel[] = {
	"\001           .MMM..:MMMMMMM",
	"\001          MMMMMMMMMMMMMMMMMM",
	"\001          MMMMMMMMMMMMMMMMMMMM.",
	"\001         MMMMMMMMMMMMMMMMMMMMMM",
	"\001        ,MMMMMMMMMMMMMMMMMMMMMM:",
	"\001        MMMMMMMMMMMMMMMMMMMMMMMM",
	"\001  .MMMM'  MMMMMMMMMMMMMMMMMMMMMM",
	"\001 MMMMMM    `MMMMMMMMMMMMMMMMMMMM.",
	"\001MMMMMMMM      MMMMMMMMMMMMMMMMMM .",
	"\001MMMMMMMMM.       `MMMMMMMMMMMMM' MM.",
	"\001MMMMMMMMMMM.                     MMMM",
	"\001`MMMMMMMMMMMMM.                 ,MMMMM.",
	"\001 `MMMMMMMMMMMMMMMMM.          ,MMMMMMMM.",
	"\001    MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
	"\001      `MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
	"\001         `MMMMMMMMMMMMMMMMMMMMMMMM:",
	"\001            ``MMMMMMMMMMMMMMMMM'",
	0
};

static const char *si_freebsd[] = {
	"\001   ```                        `",
	"\001  ` `.....---.......--.```   -/",
	"\001  +o   .--`         /y:`      +.",
	"\001   yo`:.            :o      `+-",
	"\001    y/               -/`   -o/",
	"\001   .-                  ::/sy+:.",
	"\001   /                     `--  /",
	"\001  `:                          :`",
	"\001  `:                          :`",
	"\001   /                          /",
	"\001   .-                        -.",
	"\001    --                      -.",
	"\001     `:`                  `:`",
	"\001       .--             `--.",
	"\001          .---.....----.",
	0
};

/* Every picture, by the ID a system calls itself. */
static const struct { const char *id; const char **art; } si_arts[] = {
	{ "arch", si_arch },        { "ubuntu", si_ubuntu },
	{ "fedora", si_fedora },    { "linuxmint", si_mint },
	{ "mint", si_mint },        { "opensuse", si_opensuse },
	{ "opensuse-leap", si_opensuse },
	{ "opensuse-tumbleweed", si_opensuse },
	{ "suse", si_opensuse },    { "gentoo", si_gentoo },
	{ "nixos", si_nixos },      { "void", si_void },
	{ "rhel", si_rhel },        { "centos", si_rhel },
	{ "freebsd", si_freebsd },  { "debian", si_debian },
	{ "raspbian", si_debian },  { "alpine", si_alpine },
	{ "cix", si_cix },          { "hibr", si_hibr },
	{ 0, 0 }
};

/* The picture for one name, or null when there is none. */
const char **si_named(const char *id)
{
	int i;

	if (!id || !*id)
		return 0;
	for (i = 0; si_arts[i].id; i++)
		if (!strcmp(si_arts[i].id, id))
			return si_arts[i].art;
	return 0;
}

/* The picture for this system: what it calls itself, then what it says it is
   like -- which is how a derivative gets its parent's picture without every
   one of them needing its own -- and hibr's own if neither says anything. */
const char **si_logo(const char *id, const char *like)
{
	const char **a = si_named(id);
	str w;

	if (a)
		return a;
	s_init(&w);
	while (like && *like) {
		if (*like == ' ' || *like == ',') {
			a = si_named(w.p);
			if (a) {
				s_free(&w);
				return a;
			}
			w.n = 0;
			if (w.p)
				w.p[0] = 0;
		} else {
			s_ch(&w, *like);
		}
		like++;
	}
	a = w.n ? si_named(w.p) : 0;
	s_free(&w);
	return a ? a : si_hibr;
}

/* How many columns a logo line takes: tone marks none, glyphs their width. */
size_t si_width(const char *p)
{
	size_t n = strlen(p), i = 0, w = 0;
	unsigned cp;
	int l;

	while (i < n) {
		if (p[i] == 1 || p[i] == 2) {
			i++;
			continue;
		}
		l = u8dec(p + i, n - i, &cp);
		w += (size_t)u8w(cp);
		i += (size_t)l;
	}
	return w;
}

/* Write a logo line, turning the tone marks into colour or into nothing. */
void si_art(str *o, const char *p, int tty)
{
	size_t n = strlen(p), i = 0;

	if (tty && n && p[0] != 1 && p[0] != 2)
		s_cat(o, "\033[38;5;110m");
	for (i = 0; i < n; i++) {
		if (p[i] == 1) {
			if (tty)
				s_cat(o, "\033[38;5;110m");
			continue;
		}
		if (p[i] == 2) {
			if (tty)
				s_cat(o, "\033[38;5;215m");
			continue;
		}
		s_ch(o, p[i]);
	}
	if (tty)
		s_cat(o, "\033[0m");
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
	const char *want = 0;
	struct utsname un;
	char *rel, *cpu, *mem;
	str id, like, pretty, t, u;
	vec rows;
	const char **logo;
	size_t k, n, wide;
	unsigned long long mt, ma;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-p")) {
			plain = 1;
		} else if (!strcmp(av[i], "-l") && i + 1 < ac) {
			want = av[++i];
		} else {
			lg(HIBR_LERR, "usage: sysinfo [-p] [-l picture]");
			return 2;
		}
	}
	if (plain)
		tty = 0;
	rows.p = 0;
	rows.n = 0;
	rows.cap = 0;
	s_init(&id);
	s_init(&like);
	s_init(&pretty);
	s_init(&t);
	s_init(&u);
	rel = si_slurp("/etc/os-release");
	if (rel) {
		si_rel(rel, "ID", &id);
		si_rel(rel, "ID_LIKE", &like);
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

	logo = si_logo(want ? want : id.p, want ? 0 : like.p);
	for (n = 0; logo[n]; n++)
		;
	wide = 0;
	for (k = 0; k < n; k++)
		if (si_width(logo[k]) > wide)
			wide = si_width(logo[k]);
	for (k = 0; k < n || k < rows.n; k++) {
		size_t pad = wide;
		u.n = 0;
		if (u.p)
			u.p[0] = 0;
		if (k < n) {
			si_art(&u, logo[k], tty);
			pad = wide - si_width(logo[k]);
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
	s_free(&like);
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
