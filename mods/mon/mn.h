#ifndef MN_H
#define MN_H

#include "hibr.h"

#ifndef MN_HIST
#define MN_HIST 120
#endif

/* One reading of the whole machine. Everything is a counter read twice and
   differenced, because that is how /proc reports work done rather than rate. */
typedef struct mn_cpu mn_cpu;
struct mn_cpu {
	unsigned long long busy, total;
	double pct;
};

typedef struct mn_proc mn_proc;
struct mn_proc {
	long pid;
	char name[64];
	char user[32];
	unsigned long long ticks, prev;
	long rss;
	double pct;
	int seen;
};

typedef struct mn_dev mn_dev;
struct mn_dev {
	char name[32];
	unsigned long long a, b, pa, pb;
	double ra, rb;
};

typedef struct mn_st mn_st;
struct mn_st {
	mn_cpu all;
	mn_cpu *core;
	int ncore;
	unsigned long long mtotal, mfree, mavail, mbuf, mcache, stotal, sfree;
	double load1, load5, load15;
	double uptime;
	vec procs;
	vec nets;
	vec disks;
	double cpuhist[MN_HIST];
	double memhist[MN_HIST];
	double nethist[MN_HIST];
	int hn;
	long clk;
	int first;
};

char *mn_slurp(const char *path);
void mn_init(mn_st *s);
void mn_free(mn_st *s);
void mn_sample(mn_st *s, double secs);
int mn_live(sh *s, double every, int bypid);

#endif
