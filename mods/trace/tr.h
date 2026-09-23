#ifndef TR_H
#define TR_H

#include "hibr.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#ifndef TR_PORT
#define TR_PORT 33434
#endif

#ifndef TR_HIST
#define TR_HIST 40
#endif

/* What is known about one hop after any number of rounds. */
typedef struct tr_hop tr_hop;
struct tr_hop {
	char ip[INET_ADDRSTRLEN];
	char host[NI_MAXHOST];
	int seen, named, final;
	long sent, recv;
	double last, best, worst, sum, prev, jsum;
	long jn;
	double hist[TR_HIST];
	int hn;
};

double tr_ms(struct timeval *a, struct timeval *b);
int tr_round(struct sockaddr_in *d, int maxttl, int wait, tr_hop *h);
void tr_name(tr_hop *h);
int tr_live(sh *s, struct sockaddr_in *d, const char *host, int maxttl,
	    int wait, int resolve);

#endif
