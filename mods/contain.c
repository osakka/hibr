/* contain -- run a program that may read and compute but not act on the
 * world: it writes only inside one folder, opens no socket of any kind, and
 * cannot signal or trace another process. What a sheet's formulas run in
 * (Gitea #432, ADR 0052), where --plan was a dry run standing in for one.
 *
 *   contain probe               what this system offers: "landlock N",
 *                                "sandbox", or nothing and status 1
 *   contain exec DIR [--] CMD [ARG...]
 *                                close every descriptor but 0, 1 and 2,
 *                                contain this process and exec CMD in it;
 *                                it never runs CMD uncontained: a DIR that
 *                                is not there is status 1, and a failure
 *                                once descriptors are closed ends the
 *                                process with 126
 *
 * On Linux it is Landlock -- read and execute everywhere, every kind of
 * write inside DIR and /dev/null, TCP bind and connect refused where the
 * kernel can (ABI 4), signals and abstract sockets scoped where it can
 * (ABI 6) -- and a seccomp filter that answers EACCES to socket,
 * socketpair, connect, bind, ptrace, process_vm_readv/writev and io_uring,
 * so a pathname socket the user can reach (the desktop's own control
 * socket, the session bus) is no way out either, and to every call that
 * changes a file's mode, owner, times or extended attributes, which
 * Landlock does not govern; and below ABI 6, where Landlock cannot scope
 * signals, EPERM to a signal sent to anything but itself -- its own
 * children included, which seccomp cannot tell apart. Both outlive exec, and
 * no_new_privs keeps a setuid program from shedding them. On macOS it is
 * the system sandbox, with a profile saying the same; that branch is
 * compiled there and has not been run here.
 */
#include "hibr.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif
#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif
#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif

/* The ruleset's attributes, as the kernel reads them at each ABI's size. */
struct cj_rs {
	uint64_t fs;
	uint64_t net;
	uint64_t scoped;
};

/* A rule that a path and everything beneath it may be reached this way. */
struct cj_pb {
	uint64_t allowed;
	int32_t fd;
} __attribute__((packed));

#define CJ_EXEC (1ULL << 0)
#define CJ_WRITE (1ULL << 1)
#define CJ_READ (1ULL << 2)
#define CJ_READDIR (1ULL << 3)
#define CJ_TRUNC (1ULL << 14)
#define CJ_IOCTL (1ULL << 15)
#define CJ_NETBIND 1ULL
#define CJ_NETCONN 2ULL
#define CJ_SCOPEUNIX 1ULL
#define CJ_SCOPESIG 2ULL

#define CJ_FILTERN 128

#if defined(__x86_64__)
#define CJ_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define CJ_ARCH AUDIT_ARCH_AARCH64
#endif

/* The Landlock ABI this kernel speaks, or 0 for none. */
int cj_abi(void)
{
	long v = syscall(__NR_landlock_create_ruleset, 0, 0, 1);

	return v < 0 ? 0 : (int)v;
}

/* Every filesystem right this ABI knows. */
uint64_t cj_fsall(int abi)
{
	if (abi >= 5)
		return (1ULL << 16) - 1;
	if (abi >= 3)
		return (1ULL << 15) - 1;
	if (abi >= 2)
		return (1ULL << 14) - 1;
	return (1ULL << 13) - 1;
}

/* Allow `allowed` on path and beneath it, within what the ruleset handles. */
int cj_rule(int rs, const char *path, uint64_t allowed, uint64_t handled)
{
	struct cj_pb pb;
	int fd = open(path, O_PATH | O_CLOEXEC), r;

	if (fd < 0)
		return -1;
	pb.allowed = allowed & handled;
	pb.fd = fd;
	r = (int)syscall(__NR_landlock_add_rule, rs, 1, &pb, 0);
	close(fd);
	return r;
}

/* Landlock: read and execute anywhere, write only under dir. */
int cj_landlock(const char *dir)
{
	struct cj_rs a;
	size_t sz = 8;
	int abi = cj_abi(), rs;
	uint64_t ro = CJ_EXEC | CJ_READ | CJ_READDIR;

	if (abi < 1) {
		lg(HIBR_LERR, "contain: this kernel offers no Landlock");
		return -1;
	}
	memset(&a, 0, sizeof a);
	a.fs = cj_fsall(abi);
	if (abi >= 4) {
		a.net = CJ_NETBIND | CJ_NETCONN;
		sz = 16;
	}
	if (abi >= 6) {
		a.scoped = CJ_SCOPEUNIX | CJ_SCOPESIG;
		sz = 24;
	}
	rs = (int)syscall(__NR_landlock_create_ruleset, &a, sz, 0);
	if (rs < 0) {
		lg(HIBR_LERR, "contain: landlock: %s", strerror(errno));
		return -1;
	}
	if (cj_rule(rs, "/", ro, a.fs) < 0 ||
	    cj_rule(rs, dir, a.fs, a.fs) < 0 ||
	    cj_rule(rs, "/dev/null", CJ_READ | CJ_WRITE | CJ_TRUNC | CJ_IOCTL,
		    a.fs & (CJ_READ | CJ_WRITE | CJ_TRUNC | CJ_IOCTL)) < 0) {
		lg(HIBR_LERR, "contain: landlock rule: %s", strerror(errno));
		close(rs);
		return -1;
	}
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0 ||
	    syscall(__NR_landlock_restrict_self, rs, 0) < 0) {
		lg(HIBR_LERR, "contain: landlock: %s", strerror(errno));
		close(rs);
		return -1;
	}
	close(rs);
	lg(HIBR_LDBG, "contain: landlock %d, writes only under %s", abi, dir);
	return abi;
}

/* Add one instruction, refusing to run past the filter's room. */
void cj_put(struct sock_filter *f, int *n, struct sock_filter x)
{
	if (*n >= CJ_FILTERN) {
		lg(HIBR_LERR, "contain: the filter outgrew its %d instructions", CJ_FILTERN);
		abort();
	}
	f[(*n)++] = x;
}

/* One instruction of the filter: answer EACCES when the call is nr. */
void cj_deny(struct sock_filter *f, int *n, long nr)
{
	struct sock_filter j = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)nr, 0, 1);
	struct sock_filter r = BPF_STMT(BPF_RET | BPF_K,
					SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));

	cj_put(f, n, j);
	cj_put(f, n, r);
}

/* The metadata calls Landlock does not govern: mode, owner, times, xattrs. */
void cj_nometa(struct sock_filter *f, int *n)
{
#ifdef __NR_chmod
	cj_deny(f, n, __NR_chmod);
#endif
#ifdef __NR_chown
	cj_deny(f, n, __NR_chown);
#endif
#ifdef __NR_lchown
	cj_deny(f, n, __NR_lchown);
#endif
#ifdef __NR_utime
	cj_deny(f, n, __NR_utime);
#endif
#ifdef __NR_utimes
	cj_deny(f, n, __NR_utimes);
#endif
#ifdef __NR_futimesat
	cj_deny(f, n, __NR_futimesat);
#endif
	cj_deny(f, n, __NR_fchmod);
	cj_deny(f, n, __NR_fchmodat);
	cj_deny(f, n, 452);
	cj_deny(f, n, __NR_fchown);
	cj_deny(f, n, __NR_fchownat);
	cj_deny(f, n, __NR_utimensat);
	cj_deny(f, n, __NR_setxattr);
	cj_deny(f, n, __NR_lsetxattr);
	cj_deny(f, n, __NR_fsetxattr);
	cj_deny(f, n, __NR_removexattr);
	cj_deny(f, n, __NR_lremovexattr);
	cj_deny(f, n, __NR_fremovexattr);
	cj_deny(f, n, 463);
	cj_deny(f, n, 466);
}

/* Signals to anybody but itself, where Landlock cannot scope them (ABI 6). */
void cj_nosig(struct sock_filter *f, int *n)
{
	long nr[] = { __NR_kill, __NR_tkill, __NR_tgkill, __NR_rt_sigqueueinfo,
		      __NR_rt_tgsigqueueinfo };
	struct sock_filter ld_a0 = BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					    offsetof(struct seccomp_data, args));
	struct sock_filter self = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)getpid(), 0, 1);
	struct sock_filter allow = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	struct sock_filter deny = BPF_STMT(BPF_RET | BPF_K,
					   SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
	struct sock_filter over = BPF_STMT(BPF_JMP | BPF_JA, 4);
	int i, k = (int)(sizeof nr / sizeof nr[0]);

	cj_deny(f, n, 424);
	for (i = 0; i < k; i++) {
		struct sock_filter j = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)nr[i],
						(unsigned char)(k - i - 1 + 1), 0);
		cj_put(f, n, j);
	}
	cj_put(f, n, over);
	cj_put(f, n, ld_a0);
	cj_put(f, n, self);
	cj_put(f, n, allow);
	cj_put(f, n, deny);
}

/* seccomp: no sockets, no tracing, no io_uring, no metadata changes, and
   below Landlock ABI 6 no signal to another process. */
int cj_seccomp(int abi)
{
#ifdef CJ_ARCH
	struct sock_filter *f = xm(CJ_FILTERN * sizeof *f);
	struct sock_fprog p;
	int n = 0, r = 0;
	struct sock_filter ld_arch = BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, arch));
	struct sock_filter is_arch = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CJ_ARCH, 1, 0);
	struct sock_filter deny = BPF_STMT(BPF_RET | BPF_K,
					   SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));
	struct sock_filter ld_nr = BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					    offsetof(struct seccomp_data, nr));
	struct sock_filter allow = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

	cj_put(f, &n, ld_arch);
	cj_put(f, &n, is_arch);
	cj_put(f, &n, deny);
	cj_put(f, &n, ld_nr);
#ifdef __x86_64__
	{
		struct sock_filter x32 = BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1);

		cj_put(f, &n, x32);
		cj_put(f, &n, deny);
	}
#endif
	cj_deny(f, &n, __NR_socket);
	cj_deny(f, &n, __NR_socketpair);
	cj_deny(f, &n, __NR_connect);
	cj_deny(f, &n, __NR_bind);
	cj_deny(f, &n, __NR_ptrace);
	cj_deny(f, &n, __NR_process_vm_readv);
	cj_deny(f, &n, __NR_process_vm_writev);
#ifdef __NR_io_uring_setup
	cj_deny(f, &n, __NR_io_uring_setup);
	cj_deny(f, &n, __NR_io_uring_enter);
	cj_deny(f, &n, __NR_io_uring_register);
#endif
	cj_nometa(f, &n);
	if (abi < 6)
		cj_nosig(f, &n);
	cj_put(f, &n, allow);

	p.len = (unsigned short)n;
	p.filter = f;
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0 ||
	    prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p, 0, 0) < 0) {
		lg(HIBR_LERR, "contain: seccomp: %s", strerror(errno));
		r = -1;
	}
	free(f);
	return r;
#else
	lg(HIBR_LERR, "contain: no seccomp filter for this architecture");
	return -1;
#endif
}

/* Contain this process: Landlock, then seccomp. */
int cj_apply(const char *dir)
{
	int abi = cj_landlock(dir);

	if (abi < 0)
		return -1;
	return cj_seccomp(abi);
}

/* What this system offers, for contain probe. */
int cj_what(str *o)
{
	int abi = cj_abi();

#ifndef CJ_ARCH
	abi = 0;
#endif
	if (abi < 1)
		return 0;
	s_cat(o, "landlock ");
	s_num(o, abi);
	return 1;
}
#elif defined(__APPLE__)
int sandbox_init(const char *profile, uint64_t flags, char **err);
void sandbox_free_error(char *err);

/* The system sandbox, with a profile saying what Landlock and seccomp say. */
int cj_apply(const char *dir)
{
	str p;
	char *err = 0;
	const char *c;
	int r;

	s_init(&p);
	s_cat(&p, "(version 1)(allow default)(deny network*)"
		  "(deny process-info* (target others))(deny signal (target others))"
		  "(deny file-write*)(allow file-write* (literal \"/dev/null\") (subpath \"");
	for (c = dir; *c; c++) {
		if (*c == '"' || *c == '\\')
			s_ch(&p, '\\');
		s_ch(&p, *c);
	}
	s_cat(&p, "\"))");
	r = sandbox_init(p.p, 0, &err);
	s_free(&p);
	if (r) {
		lg(HIBR_LERR, "contain: sandbox: %s", err ? err : "refused");
		if (err)
			sandbox_free_error(err);
		return -1;
	}
	return 0;
}

/* What this system offers, for contain probe. */
int cj_what(str *o)
{
	s_cat(o, "sandbox");
	return 1;
}
#else
/* Nothing to contain with here. */
int cj_apply(const char *dir)
{
	(void)dir;
	lg(HIBR_LERR, "contain: nothing on this system can contain a program");
	return -1;
}

/* What this system offers, for contain probe. */
int cj_what(str *o)
{
	(void)o;
	return 0;
}
#endif

/* Close every descriptor above 2: a contained program holds nobody else's
   terminal, pipe or socket -- the desktop's pty masters and its control
   socket among them, which no filter on new sockets would stop. */
void cj_closerest(void)
{
	DIR *d;
	struct dirent *e;
	vec fds = { 0, 0, 0 };
	size_t i;
	long fd;
	int dfd;

	d = opendir("/dev/fd");
	if (!d) {
		for (fd = 3; fd < 1024; fd++)
			close((int)fd);
		return;
	}
	dfd = dirfd(d);
	while ((e = readdir(d))) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		fd = atol(e->d_name);
		if (fd > 2 && fd != dfd)
			v_add(&fds, (void *)fd);
	}
	closedir(d);
	for (i = 0; i < fds.n; i++)
		close((int)(long)fds.p[i]);
	v_free(&fds);
}

/* contain probe | contain exec DIR [--] CMD [ARG...] */
int m_contain(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	char *dir;
	int i;
	str o;

	if (!strcmp(sub, "probe")) {
		s_init(&o);
		i = cj_what(&o);
		hibr_ret(s, o.p ? o.p : "");
		if (i && !s->bind)
			printf("%s\n", o.p);
		s_free(&o);
		return i ? HIBR_OK : HIBR_FAIL;
	}
	if (strcmp(sub, "exec") || ac < 4) {
		lg(HIBR_LERR, "usage: contain probe | contain exec DIR [--] CMD [ARG...]");
		return 2;
	}
	i = 3;
	if (!strcmp(av[i], "--"))
		i++;
	if (i >= ac) {
		lg(HIBR_LERR, "contain: exec: no command");
		return 2;
	}
	dir = realpath(av[2], 0);
	if (!dir) {
		lg(HIBR_LERR, "contain: %s: %s", av[2], strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
	cj_closerest();
	if (cj_apply(dir) < 0) {
		lg(HIBR_LERR, "contain: not running %s uncontained", av[i]);
		_exit(126);
	}
	free(dir);
	execvp(av[i], av + i);
	lg(HIBR_LERR, "contain: %s: %s", av[i], strerror(errno));
	_exit(127);
	return HIBR_FAIL;
}

const hibr_bi contain_bi[] = {
	{ "contain", m_contain, "run a program that writes only in one folder and opens no socket" },
	HIBR_BI_END
};

/* Prepare the module. */
int cj_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Tear the module down. */
void cj_fin(sh *s)
{
	(void)s;
}

HIBR_MODULE("contain", "1.0", "run a program that writes only in one folder and opens no socket",
	    contain_bi, cj_ini, cj_fin);
