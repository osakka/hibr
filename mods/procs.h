#ifndef HIBR_PROCS_H
#define HIBR_PROCS_H

#include "hibr.h"

/* One reader of the process table under /proc, shared by the two modules
   that list processes: proctab, which answers Task Manager, and mon, the
   system monitor. Each process is read the same way in both:

     ut, st   clock ticks in user and kernel mode, stat's fields 14 and 15,
              counted from the last ')' since a name may hold one
     uid      the real uid, from status
     rss      VmRSS in kB, from status -- 0 for a kernel thread, which has
              none; stat's own rss field is documented by proc(5) as
              inaccurate, so it is not used
     name     comm, extended from argv[0]'s base name when comm sits at the
              kernel's 15-byte limit and argv[0] begins with it -- a process
              that renamed its own argv keeps the name it reports

   A process that goes between two reads is left out, never half read. */

typedef struct pq_ent {
	long pid;
	unsigned long long ut, st;
	long uid, rss;
	str name;
} pq_ent;

/* Call fn once for every process, with what was read of it and ctx; 0 when
   there is no /proc to read. fn's own answer is ignored. */
int pq_each(void (*fn)(const pq_ent *, void *), void *ctx);

#endif
