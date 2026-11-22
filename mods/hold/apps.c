#define _GNU_SOURCE
/* apps -- pictures an app in a process of its own sends (ADR 0050).
 *
 * An app's own process is a child inside the held session, so its frames
 * come to this server straight, on a connection of its own (HD_APPOPEN),
 * and never pass through the desktop's loop. The app names a picture by a
 * slot of its own, never by an image id: ids are per Blit session, and two
 * senders on one id are one image, so the desktop -- which places every
 * picture on its own terminal and is the session's one allocator of ids --
 * grants each slot an id it took fresh (`grant PID SLOT ID`, on its own
 * producer connection), and this server writes that id into each of the
 * slot's frames on the way to the displays, and the slot back into what a
 * display's Blit says of them. A frame for a slot not granted yet is kept,
 * the newest of it, until the grant comes.
 *
 * Who an app is comes from the connection (SO_PEERCRED's pid), which the
 * desktop also knows of the process it started; it is for blame and for
 * accidents, not a defence against a process of the same user, which could
 * open any connection this server takes.
 *
 * Each app is capped -- frames a second, bytes a frame, slots kept, dmabuf
 * frames not yet given back -- since Blit's own ceilings are the session's
 * and an app's frames spend the desktop's. Past a cap a frame is refused
 * and counted, a dmabuf one given straight back, and the log says so at
 * most every HD_APPLOGMS; the app is never paused or cut off.
 */
#include "hd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef HD_APPFPS
#define HD_APPFPS 60
#endif
#ifndef HD_APPBYTES
#define HD_APPBYTES (32L << 20)
#endif
#ifndef HD_APPSLOTS
#define HD_APPSLOTS 16
#endif
#ifndef HD_APPBUFS
#define HD_APPBUFS 8
#endif
#ifndef HD_APPLOGMS
#define HD_APPLOGMS 5000
#endif

struct hd_app {
	int fd;
	long pid;
	long frames, refused, bytes, early;
	long long wstart, lastlog;
	int wn, pic;
	long logged;
};

struct hd_aslot {
	long pid;
	unsigned slot, id;
	int granter, kind;
	struct hd_frm f;
};

static struct hd_app *hd_apps;
static size_t hd_napp;
static struct hd_aslot *hd_aslots;
static size_t hd_naslot;
static int hd_picnow = -1, hd_piccw, hd_picch;

/* Milliseconds on the monotonic clock. */
long long hd_appms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* The app a producer connection is, or none. */
struct hd_app *hd_appof(int fd)
{
	size_t i;

	for (i = 0; i < hd_napp; i++)
		if (hd_apps[i].fd == fd)
			return &hd_apps[i];
	return 0;
}

/* Whether a producer connection is an app's. */
int hd_appis(int fd)
{
	return hd_appof(fd) != 0;
}

/* A slot of an app's, by its pid and number, or none. */
struct hd_aslot *hd_aslotof(long pid, unsigned slot)
{
	size_t i;

	for (i = 0; i < hd_naslot; i++)
		if (hd_aslots[i].pid == pid && hd_aslots[i].slot == slot)
			return &hd_aslots[i];
	return 0;
}

/* The slot an image id is granted to, or none. */
struct hd_aslot *hd_aslotid(unsigned id)
{
	size_t i;

	if (!id)
		return 0;
	for (i = 0; i < hd_naslot; i++)
		if (hd_aslots[i].id == id)
			return &hd_aslots[i];
	return 0;
}

/* Whether an image id belongs to an app: no other sender may write it. */
int hd_appowned(unsigned id)
{
	return hd_aslotid(id) != 0;
}

/* The newest memfd frame of an app's image, as hd_frmget answers. */
struct hd_frm *hd_appfrm(unsigned id)
{
	struct hd_aslot *a = hd_aslotid(id);

	return a && a->f.fd >= 0 ? &a->f : 0;
}

/* Whether an app's image has no frame yet, so its placement waits: a
   placement Blit is handed before any frame counts against the eight its
   session may have waiting (BLIT_FRAME_PENDING). */
int hd_appwait(unsigned id)
{
	struct hd_aslot *a = hd_aslotid(id);

	return a && a->f.fd < 0 && !hd_f3has(id);
}

/* Let a slot go: its kept frame closed, its dmabuf frames given back. */
void hd_aslotdrop(size_t i)
{
	struct hd_aslot *a = &hd_aslots[i];

	if (a->f.fd >= 0)
		close(a->f.fd);
	if (a->id)
		hd_f3gone(a->id);
	hd_fgen++;
	hd_aslots[i] = hd_aslots[--hd_naslot];
}

/* A slot for an app, made when asked: 0 when the app has as many as it may
   keep. */
struct hd_aslot *hd_aslotnew(long pid, unsigned slot)
{
	struct hd_aslot *a = hd_aslotof(pid, slot);
	size_t i, n = 0;

	if (a)
		return a;
	for (i = 0; i < hd_naslot; i++)
		if (hd_aslots[i].pid == pid)
			n++;
	if (n >= HD_APPSLOTS)
		return 0;
	hd_aslots = xr(hd_aslots, (hd_naslot + 1) * sizeof *hd_aslots);
	a = &hd_aslots[hd_naslot++];
	memset(a, 0, sizeof *a);
	a->pid = pid;
	a->slot = slot;
	a->granter = -1;
	a->f.fd = -1;
	return a;
}

/* What every app is told of pictures: "PIC CW CH" while every display can
   be sent frames and draws kitty pictures, with what a cell measures, and
   "PIC 0 0" otherwise -- the same answer the desktop's own pictures go by
   descriptor on. */
void hd_apppicsay(struct hd_app *ap)
{
	str o;

	s_init(&o);
	s_cat(&o, "PIC ");
	s_num(&o, hd_picnow > 0 ? hd_piccw : 0);
	s_ch(&o, ' ');
	s_num(&o, hd_picnow > 0 ? hd_picch : 0);
	hd_send(ap->fd, HD_FRMANS, o.p, o.n);
	s_free(&o);
	ap->pic = hd_picnow;
}

/* Work out whether apps may send pictures, and tell each app a change. */
void hd_apppic(vec *cls)
{
	size_t i;
	int ok = cls->n > 0 && hd_scw > 1 && hd_sch > 1;

	for (i = 0; ok && i < cls->n; i++) {
		struct hd_cli *cn = cls->p[i];

		if (!cn->fdok || (cn->gfx != GX_KITTY && cn->gfx != GX_UNASKED))
			ok = 0;
	}
	if (ok == hd_picnow && (!ok || (hd_piccw == hd_scw && hd_picch == hd_sch)))
		return;
	hd_picnow = ok;
	hd_piccw = hd_scw;
	hd_picch = hd_sch;
	for (i = 0; i < hd_napp; i++)
		hd_apppicsay(&hd_apps[i]);
}

/* A connection an app opened for its pictures: who it is, from the
   connection itself. 0 when that cannot be known. */
int hd_appopen(int c, vec *cls)
{
	long pid = 0;
	struct hd_app *ap;
#ifdef SO_PEERCRED
	struct ucred u;
	socklen_t l = sizeof u;

	if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &u, &l) == 0)
		pid = (long)u.pid;
#endif
	if (pid <= 0)
		return 0;
	hd_apps = xr(hd_apps, (hd_napp + 1) * sizeof *hd_apps);
	ap = &hd_apps[hd_napp++];
	memset(ap, 0, sizeof *ap);
	ap->fd = c;
	ap->pid = pid;
	if (hd_picnow < 0)
		hd_apppic(cls);
	hd_apppicsay(ap);
	lg(HIBR_LDBG, "hold: app %ld opened its pictures' connection", pid);
	return 1;
}

/* Say why an app's frame was refused, at most every HD_APPLOGMS. */
void hd_apprefuse(struct hd_app *ap, unsigned slot, const char *why)
{
	long long now = hd_appms();

	ap->refused++;
	if (ap->logged && now - ap->lastlog < HD_APPLOGMS)
		return;
	lg(HIBR_LWRN, "hold: app %ld: a frame of slot %u refused, %s "
		      "(%ld refused so far; the app goes on)",
	   ap->pid, slot, why, ap->refused);
	ap->lastlog = now;
	ap->logged = 1;
}

/* A line to a producer about one of its frames, a slot in place of the
   image id when the producer is an app: its own number, never a session's. */
void hd_appsay(int prod, const char *line, size_t n)
{
	struct hd_app *ap = hd_appof(prod);
	struct hd_aslot *a;
	unsigned long id;
	size_t i = 0;
	int sp = 0;
	str o;

	if (prod < 0)
		return;
	if (!ap) {
		hd_send(prod, HD_FRMANS, line, n);
		return;
	}
	while (i < n && sp < 2)
		if (line[i++] == ' ')
			sp++;
	id = i < n ? strtoul(line + i, 0, 10) : 0;
	a = hd_aslotid((unsigned)id);
	if (!a || a->pid != ap->pid) {
		hd_send(prod, HD_FRMANS, line, n);
		return;
	}
	s_init(&o);
	s_add(&o, line, i);
	s_num(&o, (long)a->slot);
	while (i < n && line[i] != ' ')
		i++;
	s_add(&o, line + i, n - i);
	hd_send(prod, HD_FRMANS, o.p, o.n);
	s_free(&o);
}

/* Whether an app may send one more frame this second; counted when it may. */
int hd_apprate(struct hd_app *ap)
{
	long long now = hd_appms();

	if (now - ap->wstart >= 1000) {
		ap->wstart = now;
		ap->wn = 0;
	}
	if (ap->wn >= HD_APPFPS)
		return 0;
	ap->wn++;
	return 1;
}

/* Give a dmabuf frame straight back to the app that sent it, unshown. */
void hd_appback(struct hd_app *ap, const unsigned char *h, const int *fds, int nf)
{
	str o;
	int k;

	for (k = 0; k < nf; k++)
		close(fds[k]);
	s_init(&o);
	s_cat(&o, "FRAME release ");
	s_num(&o, (long)hd_frmnum(h + 4, 4));
	s_ch(&o, ' ');
	s_fmt(&o, "%llu", hd_frmnum(h + 8, 8));
	hd_send(ap->fd, HD_FRMANS, o.p, o.n);
	s_free(&o);
}

/* A frame an app sent on its own connection: a memfd (HD_FRAME) or a
   dmabuf (HD_FRM3), its header naming the app's slot. 1 when the displays'
   pictures are owed a render: a placement held back for want of a frame
   can go now. */
int hd_appmsg(vec *cls, int tid, int fd, int t, const char *p, size_t n,
	      int *fds, int nf)
{
	struct hd_app *ap = hd_appof(fd);
	struct hd_aslot *a;
	/* A frame header is BF_HDR3 bytes at most, as the wire fixes it. */
	unsigned char h[BF_HDR3];
	unsigned slot;
	long long bytes;
	int k, first, kind = t == HD_FRAME ? 1 : 3;
	struct stat st;

	if (!ap || (t != HD_FRAME && t != HD_FRM3) ||
	    n != (t == HD_FRAME ? BF_HDR : BF_HDR3) || nf < 1 ||
	    memcmp(p, t == HD_FRAME ? "FRM1" : "FRM3", 4)) {
		for (k = 0; k < nf; k++)
			close(fds[k]);
		if (ap)
			hd_apprefuse(ap, 0, "not a frame");
		return 0;
	}
	memcpy(h, p, n);
	slot = (unsigned)hd_frmnum(h + 4, 4);
	bytes = kind == 1 ? (fstat(fds[0], &st) == 0 ? (long long)st.st_size : 0) :
			    (long long)hd_frmnum(h + 16, 4) * (long long)hd_frmnum(h + 20, 4) * 4;
	if (!slot || bytes > HD_APPBYTES || !hd_apprate(ap) ||
	    (kind == 3 && hd_f3count(fd) >= HD_APPBUFS)) {
		hd_apprefuse(ap, slot, !slot ? "slot 0" : bytes > HD_APPBYTES ?
			     "too large a frame" : kind == 3 &&
			     hd_f3count(fd) >= HD_APPBUFS ?
			     "too many frames not given back" : "too many frames a second");
		if (kind == 3)
			hd_appback(ap, h, fds, nf);
		else
			for (k = 0; k < nf; k++)
				close(fds[k]);
		return 0;
	}
	a = hd_aslotnew(ap->pid, slot);
	if (!a || (a->kind && a->kind != kind)) {
		hd_apprefuse(ap, slot, !a ? "too many pictures kept" :
				       "a frame of another kind on one image");
		if (kind == 3)
			hd_appback(ap, h, fds, nf);
		else
			for (k = 0; k < nf; k++)
				close(fds[k]);
		return 0;
	}
	if (a->f.fd >= 0 && kind == 1 &&
	    hd_frmnum(h + 8, 8) <= a->f.seq && a->f.seq) {
		close(fds[0]);
		hd_apprefuse(ap, slot, "a sequence not newer than the last");
		return 0;
	}
	a->kind = kind;
	ap->frames++;
	ap->bytes += (long)bytes;
	if (kind == 3 && !a->id) {
		ap->early++;
		hd_appback(ap, h, fds, nf);
		return 0;
	}
	if (kind == 3) {
		bf_put32(h + 4, a->id);
		first = !hd_f3has(a->id);
		hd_f3put(cls, tid, fd, h, fds, nf);
	} else {
		first = a->f.fd < 0;
		if (a->f.fd >= 0)
			close(a->f.fd);
		a->f.fd = fds[0];
		for (k = 1; k < nf; k++)
			close(fds[k]);
		a->f.seq = hd_frmnum(h + 8, 8);
		a->f.seen = 0;
		if (a->id)
			bf_put32(h + 4, a->id);
		memcpy(a->f.h, h, BF_HDR);
		a->f.id = a->id;
		hd_fgen++;
		if (!a->id)
			return 0;
		hd_frmpush(cls, tid, a->f.h);
	}
	if (!first)
		return 0;
	for (k = 0; k < (int)cls->n; k++)
		((struct hd_cli *)cls->p[k])->imgen = 0;
	return 1;
}

/* What the desktop says of its apps' pictures, on its own producer
   connection: `grant PID SLOT ID`, `ungrant PID SLOT`, `revoke PID`. 1 when
   the displays' pictures are owed a render. */
int hd_appctl(vec *cls, int tid, int fd, const char *p, size_t n)
{
	char *l = xm(n + 1), *sp;
	const char *vb;
	long pid = 0;
	unsigned long slot = 0, id = 0;
	struct hd_aslot *a, *o;
	size_t i;
	int got = 0, owed = 0;

	memcpy(l, p, n);
	l[n] = 0;
	sp = strchr(l, ' ');
	if (sp) {
		*sp = 0;
		got = 1 + sscanf(sp + 1, "%ld %lu %lu", &pid, &slot, &id);
	}
	vb = !strcmp(l, "grant") ? "grant" : !strcmp(l, "ungrant") ? "ungrant" :
		!strcmp(l, "revoke") ? "revoke" : "";
	free(l);
	if (hd_appis(fd) || got < 2 || pid <= 0) {
		lg(HIBR_LWRN, "hold: an app control line refused");
		return 0;
	}
	if (!strcmp(vb, "grant") && got == 4 && slot && id) {
		o = hd_aslotid((unsigned)id);
		if (o && (o->pid != pid || o->slot != slot)) {
			lg(HIBR_LWRN, "hold: id %lu is app %ld's already; not "
				      "granted to app %ld", id, o->pid, pid);
			return 0;
		}
		if (hd_frmget((unsigned)id) && !o) {
			lg(HIBR_LWRN, "hold: id %lu has frames of another "
				      "sender's; not granted to app %ld", id, pid);
			return 0;
		}
		a = hd_aslotof(pid, (unsigned)slot);
		if (!a) {
			hd_aslots = xr(hd_aslots, (hd_naslot + 1) * sizeof *hd_aslots);
			a = &hd_aslots[hd_naslot++];
			memset(a, 0, sizeof *a);
			a->pid = pid;
			a->slot = (unsigned)slot;
			a->f.fd = -1;
		}
		if (a->id && a->id != id)
			hd_f3gone(a->id);
		a->id = (unsigned)id;
		a->granter = fd;
		a->f.id = a->id;
		lg(HIBR_LDBG, "hold: app %ld slot %lu is image %lu", pid, slot, id);
		if (a->f.fd >= 0) {
			bf_put32(a->f.h + 4, a->id);
			hd_fgen++;
			hd_frmpush(cls, tid, a->f.h);
			for (i = 0; i < cls->n; i++)
				((struct hd_cli *)cls->p[i])->imgen = 0;
			owed = 1;
		}
		return owed;
	}
	if (!strcmp(vb, "ungrant") && got >= 3) {
		for (i = 0; i < hd_naslot; i++)
			if (hd_aslots[i].pid == pid && hd_aslots[i].slot == slot) {
				hd_aslotdrop(i);
				break;
			}
		return 0;
	}
	if (!strcmp(vb, "revoke")) {
		for (i = hd_naslot; i--;)
			if (hd_aslots[i].pid == pid)
				hd_aslotdrop(i);
		for (i = 0; i < hd_napp; i++)
			if (hd_apps[i].pid == pid)
				shutdown(hd_apps[i].fd, SHUT_RDWR);
		lg(HIBR_LDBG, "hold: app %ld's pictures revoked", pid);
		return 0;
	}
	lg(HIBR_LWRN, "hold: an app control line not understood");
	return 0;
}

/* A producer connection went. An app's ended: a picture granted an id keeps
   its last frame, since the desktop may keep its window showing it until
   the person closes it (ADR 0050, Decision 7) -- the desktop's ungrant or
   revoke ends it -- and one never granted goes. A granter's takes every
   grant it made. */
void hd_appgone(int fd)
{
	struct hd_app *ap = hd_appof(fd);
	size_t i;

	if (ap) {
		long pid = ap->pid;

		for (i = hd_naslot; i--;)
			if (hd_aslots[i].pid == pid && !hd_aslots[i].id)
				hd_aslotdrop(i);
		lg(HIBR_LDBG, "hold: app %ld closed its pictures' connection", pid);
		*ap = hd_apps[--hd_napp];
		return;
	}
	for (i = hd_naslot; i--;)
		if (hd_aslots[i].granter == fd)
			hd_aslotdrop(i);
}

/* Every app's pictures, for `hold apps`: pid, slots, frames taken, refused,
   bytes, and dmabuf frames sent before their slot was granted, a line an
   app -- and an app that has gone and still has pictures kept, its counts
   0. */
void hd_applist(int c)
{
	str o;
	size_t i, j, ns;

	s_init(&o);
	for (i = 0; i < hd_napp; i++) {
		ns = 0;
		for (j = 0; j < hd_naslot; j++)
			if (hd_aslots[j].pid == hd_apps[i].pid)
				ns++;
		if (o.n)
			s_ch(&o, '\n');
		s_fmt(&o, "%ld %ld %ld %ld %ld %ld", hd_apps[i].pid, (long)ns,
		      hd_apps[i].frames, hd_apps[i].refused, hd_apps[i].bytes,
		      hd_apps[i].early);
	}
	for (j = 0; j < hd_naslot; j++) {
		long pid = hd_aslots[j].pid;
		size_t k;

		for (i = 0; i < hd_napp && hd_apps[i].pid != pid; i++)
			;
		for (k = 0; k < j && hd_aslots[k].pid != pid; k++)
			;
		if (i < hd_napp || k < j)
			continue;
		ns = 0;
		for (k = j; k < hd_naslot; k++)
			if (hd_aslots[k].pid == pid)
				ns++;
		if (o.n)
			s_ch(&o, '\n');
		s_fmt(&o, "%ld %ld 0 0 0 0", pid, (long)ns);
	}
	hd_send(c, HD_APPLIST, o.p, o.n);
	s_free(&o);
}

/* Every app's kept frame closed: the session is ending. */
void hd_appclear(void)
{
	while (hd_naslot)
		hd_aslotdrop(hd_naslot - 1);
}
