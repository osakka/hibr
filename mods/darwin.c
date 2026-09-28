#include "hibr.h"
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

/* What Task Manager and About hibr need on macOS that /proc gives Linux for
   free: a system-wide CPU and memory reading with no fork, no parsing top's
   text, and no wait for a second sample. Native host_statistics(64) calls
   answer in one syscall, the same numbers top's own headline percentage is
   built from.

   cpu returns the two raw counters (total, idle), not a percentage: a
   percentage needs a delta between two readings, and the previous reading
   has to belong to whichever caller is asking. About hibr and Task Manager
   can both be open at once, each on its own throttle, and a single static
   pair of "previous" counters in this module would hand one of them the
   other's delta instead of its own. The Linux path already solves this by
   keeping the previous reading in the caller's own per-window state
   (AB[$id]["ptotal"], TK_PREV[$id]["cput"]) and diffing there; darwin cpu
   is deliberately stateless so the same script-side pattern works
   unchanged on both platforms. mem has no such problem -- it is a snapshot
   ratio, not a rate -- so it answers a ready percentage directly. */

/* Two space-separated integers: total and idle CPU ticks since boot,
   summed over every core, for the caller to diff between two readings. */
int dw_cpu(sh *s, int ac, char **av)
{
	host_t host;
	host_cpu_load_info_data_t c;
	mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
	unsigned long total, idle;
	char buf[48];

	(void)ac;
	(void)av;
	host = mach_host_self();
	if (host_statistics(host, HOST_CPU_LOAD_INFO, (host_info_t)&c, &n) !=
	    KERN_SUCCESS) {
		mach_port_deallocate(mach_task_self(), host);
		hibr_fail(s, "darwin: host_statistics failed");
		return HIBR_FAIL;
	}
	mach_port_deallocate(mach_task_self(), host);
	total = (unsigned long)c.cpu_ticks[CPU_STATE_USER] +
		(unsigned long)c.cpu_ticks[CPU_STATE_SYSTEM] +
		(unsigned long)c.cpu_ticks[CPU_STATE_IDLE] +
		(unsigned long)c.cpu_ticks[CPU_STATE_NICE];
	idle = (unsigned long)c.cpu_ticks[CPU_STATE_IDLE];
	snprintf(buf, sizeof buf, "%lu %lu", total, idle);
	hibr_ret(s, buf);
	if (!s->bind)
		printf("%s\n", buf);
	return HIBR_OK;
}

/* Percent memory in use: free and inactive pages count as available, the
   same "rough equivalent of MemAvailable" about.hibr's own vm_stat-parsing
   fallback already reasoned through -- inactive is reclaimable on demand,
   the same way a Linux page cache is, so it reads as free rather than
   used. This answers the identical formula natively, without forking
   vm_stat and parsing its text. */
int dw_mem(sh *s, int ac, char **av)
{
	host_t host;
	vm_statistics64_data_t v;
	mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
	int64_t total = 0;
	size_t sz = sizeof total;
	unsigned long long avail, pagesize;
	int pct = 0;
	char buf[16];

	(void)ac;
	(void)av;
	if (sysctlbyname("hw.memsize", &total, &sz, 0, 0) != 0 || total <= 0) {
		hibr_fail(s, "darwin: sysctlbyname hw.memsize failed");
		return HIBR_FAIL;
	}
	host = mach_host_self();
	if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&v, &n) !=
	    KERN_SUCCESS) {
		mach_port_deallocate(mach_task_self(), host);
		hibr_fail(s, "darwin: host_statistics64 failed");
		return HIBR_FAIL;
	}
	mach_port_deallocate(mach_task_self(), host);
	pagesize = (unsigned long long)getpagesize();
	avail = ((unsigned long long)v.free_count +
		 (unsigned long long)v.inactive_count) * pagesize;
	pct = (int)(((unsigned long long)total - avail) * 100 /
		    (unsigned long long)total);
	snprintf(buf, sizeof buf, "%d", pct);
	hibr_ret(s, buf);
	if (!s->bind)
		printf("%s\n", buf);
	return HIBR_OK;
}

/* Dispatch cpu|mem, the same shape console's own single entry point uses. */
int m_darwin(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";

	if (!strcmp(sub, "cpu"))
		return dw_cpu(s, ac, av);
	if (!strcmp(sub, "mem"))
		return dw_mem(s, ac, av);
	lg(HIBR_LERR, "usage: darwin cpu|mem");
	return 2;
}

const hibr_bi darwin_bi[] = {
	{ "darwin", m_darwin, "macOS system stats: cpu, mem" },
	HIBR_BI_END
};

HIBR_MODULE("darwin", HIBR_VER, "cpu, mem", darwin_bi, 0, 0);
