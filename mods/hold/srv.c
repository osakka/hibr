#define _GNU_SOURCE

#include "hd.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

volatile sig_atomic_t hd_term;

void hd_onterm(int n)
{
	(void)n;
	hd_term = 1;
}

/* Ask the program to draw itself again.  A resize to the same size sends no
   SIGWINCH, and a terminal that has just attached has nothing on it, so the
   foreground of the held terminal is told directly. */
void hd_poke(int m)
{
	pid_t g;

	if (ioctl(m, TIOCGPGRP, &g) == 0 && g > 0)
		kill(-g, SIGWINCH);
}

/* Make the emulator a relay before it is fed anything: a question about
   the terminal -- kitty's `a=q`, the device attributes -- is passed on to
   whoever is attached rather than answered by the emulator as itself. It
   used to become one only at the first drain, which comes after the first
   feed, and a new desktop asks in its first output: the emulator answered
   "no sixel" and dropped kitty's query, the console took that for a real
   no, and a new session drew every picture as cells for its whole life
   (Gitea #278). Asked with nobody attached, the question reaches nobody,
   which leaves the console unsure rather than wrong, and an attach's own
   SIGWINCH asks again. */
void hd_relay(int tid)
{
	str o;

	s_init(&o);
	hd_tm->pass(tid, &o);
	s_free(&o);
}

/* A client that has said nothing about what it draws in HD_GFXWAITMS is
   from before the asking: unasked, sent what it was sent before (Gitea
   #279), and the program nudged to read the displays again. How long the
   loop may wait before the next such deadline, -1 when there is none. */
int hd_gfxdue(vec *cls, int m)
{
	size_t i;
	long now = gx_now(), left, best = -1;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (!cn->gfxwait)
			continue;
		left = cn->gfxwait - now;
		if (left <= 0) {
			cn->gfxwait = 0;
			cn->gfx = GX_UNASKED;
			cn->imgen = 0;
			lg(HIBR_LDBG, "hold: %s said nothing of pictures: unasked",
			   cn->name.p ? cn->name.p : "a client");
			hd_poke(m);
			continue;
		}
		if (best < 0 || left < best)
			best = left;
	}
	return (int)best;
}

/* Set by hd_cdrop, so the loop puts the arrangement right once a pass is
   over however a display went -- a hangup, its own detach, a send that
   failed while drawing -- where only Displays > Detach used to (Gitea
   #297): the program was left drawing for a display no longer there. */
int hd_gone;

/* Remove a client by fd, closing it, and remember where it was.  A no-op
   if it is not (or no longer) in the list: a poll() snapshot taken at the
   top of the loop can still name a client that an earlier part of the
   same pass already dropped. */
void hd_cdrop(vec *cls, int fd)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->fd == fd) {
			hd_slotkeep(cn);
			hd_gone = 1;
			close(fd);
			free(cn->front);
			free(cn->pid);
			free(cn->phash);
			s_free(&cn->mbuf);
			s_free(&cn->name);
			s_free(&cn->title);
			free(cn);
			cls->p[i] = cls->p[--cls->n];
			return;
		}
	}
}

/* The client with this fd, or NULL if it is not (or no longer) attached. */
struct hd_cli *hd_cfind(vec *cls, int fd)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->fd == fd)
			return cn;
	}
	return 0;
}

/* Whether a client with this fd is still in the list. */
int hd_chas(vec *cls, int fd)
{
	return hd_cfind(cls, fd) != 0;
}

/* The client named this, or NULL if none is. */
struct hd_cli *hd_cfindname(vec *cls, const char *name)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->name.p && !strcmp(cn->name.p, name))
			return cn;
	}
	return 0;
}

/* Whether any attached client is already marked primary. */
int hd_anyprimary(vec *cls)
{
	return hd_cprimary(cls) != 0;
}

/* The attached client marked primary, or NULL. */
struct hd_cli *hd_cprimary(vec *cls)
{
	size_t i;

	for (i = 0; i < cls->n; i++)
		if (((struct hd_cli *)cls->p[i])->primary)
			return cls->p[i];
	return 0;
}

/* Every display that has gone, by name: a display that joins again under
   the same name asks where it was (HD_SLOT) and lands there, rather than
   beside everything, where the windows it had are on no screen at all
   (Gitea #297). hd_autoprim says the primary now is only primary because
   the one that was went; that one takes it back when it returns, unless a
   person chose another in between. */
vec hd_slots;
int hd_autoprim;

/* The remembered place of the display a name belongs to: the part of it
   before any #, since a second display from one machine is n#2. */
struct hd_slot *hd_slotfind(const char *nm, size_t n)
{
	const char *h = memchr(nm, '#', n);
	size_t i;
	struct hd_slot *sl;

	if (h)
		n = (size_t)(h - nm);
	for (i = 0; i < hd_slots.n; i++) {
		sl = hd_slots.p[i];
		if (sl->name.n == n && !memcmp(sl->name.p, nm, n))
			return sl;
	}
	return 0;
}

/* Remember where a display is as it goes. A name hold made up from the
   descriptor (client-N) names nothing that can come back. */
void hd_slotkeep(struct hd_cli *cn)
{
	struct hd_slot *sl;
	const char *h;
	size_t n;

	if (!cn->name.n || !strncmp(cn->name.p, "client-", 7))
		return;
	sl = hd_slotfind(cn->name.p, cn->name.n);
	if (!sl) {
		h = memchr(cn->name.p, '#', cn->name.n);
		n = h ? (size_t)(h - cn->name.p) : cn->name.n;
		sl = xm(sizeof *sl);
		memset(sl, 0, sizeof *sl);
		s_init(&sl->name);
		s_add(&sl->name, cn->name.p, n);
		v_add(&hd_slots, sl);
	}
	sl->row = cn->row - hd_shiftr;
	sl->col = cn->col - hd_shiftc;
	sl->rows = cn->rows;
	sl->cols = cn->cols;
	sl->primary = cn->primary;
}

/* The furthest row and column any attached client's viewport reaches --
   the union's own size, queried without resizing anything. 0,0 with
   nobody attached. */
void hd_ubox(vec *cls, int *rows, int *cols)
{
	size_t i;
	struct hd_cli *cn;

	*rows = 0;
	*cols = 0;
	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->row + cn->rows > *rows)
			*rows = cn->row + cn->rows;
		if (cn->col + cn->cols > *cols)
			*cols = cn->col + cn->cols;
	}
}

/* What a cell measures on the clients' own terminals, which is what decides
   whether the program can put pixels on screen at all (ADR 0037): the
   primary client's, or the first one that knows. A bitmap is drawn 1:1, so
   there is no second size to draw it at -- clients that disagree all get
   the primary's, and one whose cells are a different size sees a picture
   the wrong size rather than nothing, which is the honest trade. A client
   whose terminal draws no pictures at all is the case that trade did not
   price -- it saw a black rectangle -- and it is no longer sent one: each
   client says what its own terminal draws (HD_GFX), the picture loop sends
   only what that terminal draws, and the desktop draws half blocks there.
   So borrowing a capable client's size is safe, and keeps the pixels on
   the display that can show them (Gitea #258). */
void hd_cellpx(vec *cls, int *w, int *h)
{
	size_t i;
	struct hd_cli *cn;

	*w = 0;
	*h = 0;
	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->xpix < cn->cols || cn->ypix < cn->rows)
			continue;
		if (*w && !cn->primary)
			continue;
		*w = cn->xpix / cn->cols;
		*h = cn->ypix / cn->rows;
		if (cn->primary)
			return;
	}
}

/* Shift every client together so the top-left corner of the arrangement
   is the union's own 0,0, and say whether anything moved.

   A display placed above or to the left of whoever happens to sit at
   hold's origin can only say so with a negative row or column, and the
   control panel sends exactly that -- deliberately, since clamping it
   to zero was its own bug. What was never done is the other half: the
   union was measured from 0,0 to the furthest edge any client reached,
   so a client at a negative row contributed *nothing* to it and its
   whole viewport then read rows `tm_at` answers null for. Nothing
   crashes and nothing is logged; the display simply shows an empty
   screen, no window can be moved onto it, and the control panel's
   picture -- which shifts its own drawing to fit, and so looks right --
   disagrees with where hold actually put things. Measured live on the
   owner's own two-display session as `client-6 -68 3 64 213 0` beside
   a union of 64x216 (Gitea #257).

   Normalising keeps the arrangement a person made -- every client moves
   by the same amount, so none of them moves relative to another -- and
   gives the program one screen big enough for all of it. A shifted
   client's front buffer is dropped, because what it is looking at has
   changed under it; the program's own content does not move with the
   shift, which is the half the desktop has to handle (dt_size).

   Only a **negative** minimum is taken out, never a positive one, and
   that is a decision rather than a shortcut. Attaching at a positive
   offset with nothing to the left of or above it is how space is
   *reserved* for a display that has not joined yet -- `hold attach -n
   lower vt 10 0` is how tests/desktop.py stacks a union, and
   `desktop --join` does the same -- so pulling the arrangement up to
   the corner would land the next client straight on top of the first.
   Leading space a person asked for is theirs; a negative is not space
   at all, it is a corner the union has no room for. */
int hd_shiftr;
int hd_shiftc;

int hd_unorm(vec *cls)
{
	size_t i;
	struct hd_cli *cn;
	int mr = 0, mc = 0;

	if (!cls->n)
		return 0;
	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		if (cn->row < mr)
			mr = cn->row;
		if (cn->col < mc)
			mc = cn->col;
	}
	if (!mr && !mc)
		return 0;
	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		cn->row -= mr;
		cn->col -= mc;
		free(cn->front);
		cn->front = 0;
		cn->imgen = 0;
	}
	hd_shiftr -= mr;
	hd_shiftc -= mc;
	lg(HIBR_LDBG, "hold: origin normalised, every display moved %d,%d"
	   " (%d,%d in all)", -mr, -mc, hd_shiftr, hd_shiftc);
	return 1;
}

/* The pty's own size, and the emulator's: sized to the union, so the
   program always sees one screen big enough for everyone at their own
   place, not just the largest single client. The pixel size goes with it,
   or a program on this pty cannot learn what a cell measures and draws
   every picture as blocks -- which is what every held desktop did until
   0.99.77 (Gitea #103). Left alone with nobody attached -- there is
   nothing to size it from. */
int hd_scw, hd_sch;

int hd_union(vec *cls, int id, int tid)
{
	int rows, cols, cw = 0, ch = 0, moved;

	if (!cls->n)
		return 0;
	moved = hd_unorm(cls);
	hd_ubox(cls, &rows, &cols);
	hd_cellpx(cls, &cw, &ch);
	if (hd_pty->resizepx)
		hd_pty->resizepx(id, rows, cols, cw * cols, ch * rows);
	else
		hd_pty->resize(id, rows, cols);
	hd_tm->resize(tid, rows, cols);
	if (hd_tm->cellpx)
		hd_tm->cellpx(tid, cw, ch);
	hd_scw = cw;
	hd_sch = ch;
	lg(HIBR_LDBG, "hold: union %dx%d cells, a cell %dx%d pixels", rows,
	   cols, cw, ch);
	return moved;
}

/* Detach every attached client, telling each one why, and empty the list. */
void hd_cclear(vec *cls, const char *why, size_t n)
{
	size_t i;
	struct hd_cli *cn;

	for (i = 0; i < cls->n; i++) {
		cn = cls->p[i];
		hd_send(cn->fd, HD_DETACH, why, n);
		close(cn->fd);
		free(cn->front);
		free(cn->pid);
		free(cn->phash);
		s_free(&cn->mbuf);
		s_free(&cn->name);
		s_free(&cn->title);
		free(cn);
	}
	cls->n = 0;
}

/* Handle one new connection: a plain attach takes the session over,
   detaching whoever was there; HD_MATTACH joins alongside them instead.
   Anything else is a one-shot request, answered and closed.

   The attach payload is either 2 ints (rows, cols -- an older client, or one
   with no offset of its own: it sits at 0,0) or 4 (rows, cols, row, col).
   Accepting both means an old client and a new server, or the reverse, still
   attach -- just without a viewport, until whichever side is stale gets
   reinstalled. */
void hd_conn(int c, vec *cls, int id, int tid, int m, int *quit, int *upg)
{
	struct pollfd q;
	str in, o;
	int t = 0, v[4];
	struct hd_cli *cn;

	q.fd = c;
	q.events = POLLIN;
	s_init(&in);
	if (poll(&q, 1, 2000) <= 0 || hd_recv(c, &t, &in) <= 0) {
		close(c);
		s_free(&in);
		return;
	}
	switch (t) {
	case HD_ATTACH:
		hd_cclear(cls, "attached elsewhere", 18);
		/* fall through */
	case HD_MATTACH:
		cn = xm(sizeof *cn);
		memset(cn, 0, sizeof *cn);
		/* Unknown -- sent no picture -- while it has HD_GFXWAITMS to
		   say what it draws, and unasked after that (hd_gfxdue): a
		   current client's HD_GFX follows on the same socket within
		   milliseconds, while one older than the asking never sends
		   one, and was unknown for the whole of its attach where it
		   had been sent pictures before (Gitea #279). Not unasked
		   from the start, because this client is rendered below
		   before its HD_GFX is read. */
		cn->gfxwait = gx_now() + HD_GFXWAITMS;
		s_init(&cn->title);
		cn->fd = c;
		if (in.n >= 4 * sizeof(int)) {
			memcpy(v, in.p, sizeof v);
			cn->rows = v[0];
			cn->cols = v[1];
			cn->row = v[2];
			cn->col = v[3];
			if (in.n > 4 * sizeof(int))
				s_add(&cn->name, in.p + 4 * sizeof(int),
				      in.n - 4 * sizeof(int));
		} else if (in.n == 2 * sizeof(int)) {
			memcpy(v, in.p, 2 * sizeof(int));
			cn->rows = v[0];
			cn->cols = v[1];
		}
		if (!cn->name.n) {
			s_cat(&cn->name, "client-");
			s_num(&cn->name, cn->fd);
		}
		{
			struct hd_slot *sl = hd_slotfind(cn->name.p, cn->name.n);
			struct hd_cli *pc = hd_cprimary(cls);

			/* The primary that went, coming back to a primary
			   that is only primary because it went. */
			if (sl && sl->primary && pc && hd_autoprim) {
				pc->primary = 0;
				cn->primary = 1;
				hd_autoprim = 0;
			} else if (!pc) {
				cn->primary = 1;
			}
		}
		v_add(cls, cn);
		/* A normalised origin moves everyone, so everyone is
		   repainted rather than only the client that arrived. */
		if (hd_union(cls, id, tid)) {
			lg(HIBR_LDBG, "hold: attached at %d,%d size %dx%d (%lu now)",
			   cn->row, cn->col, cn->rows, cn->cols,
			   (unsigned long)cls->n);
			hd_rensend(cls, tid);
			hd_poke(m);
			s_free(&in);
			return;
		}
		lg(HIBR_LDBG, "hold: attached at %d,%d size %dx%d (%lu now)",
		   cn->row, cn->col, cn->rows, cn->cols,
		   (unsigned long)cls->n);
		/* cn may be freed by a failed send below -- nothing after
		   this point may still read it. */
		hd_rensend1(cls, cn, tid);
		hd_poke(m);
		s_free(&in);
		return;
	case HD_DETACH:
		hd_cclear(cls, "detached", 8);
		hd_send(c, HD_DETACH, 0, 0);
		break;
	case HD_KILL:
		*quit = 1;
		hd_send(c, HD_KILL, 0, 0);
		break;
	/* Answered before the exec rather than after it, because after it
	   there is nobody here to answer: the reply is "this is going to
	   happen", and a client that wants to know whether it worked asks
	   the session again afterwards. */
	case HD_UPGRADE:
		*upg = 1;
		hd_send(c, HD_UPGRADE, 0, 0);
		break;
	case HD_INFO: {
		int urows, ucols;

		hd_ubox(cls, &urows, &ucols);
		s_init(&o);
		s_cat(&o, cls->n ? "attached" : "detached");
		s_ch(&o, ' ');
		s_num(&o, hd_pty->pid(id));
		s_ch(&o, ' ');
		s_num(&o, urows);
		s_ch(&o, 'x');
		s_num(&o, ucols);
		hd_send(c, HD_INFO, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_CLIENTS: {
		size_t ci;

		s_init(&o);
		for (ci = 0; ci < cls->n; ci++) {
			struct hd_cli *cn2 = cls->p[ci];

			if (o.n)
				s_ch(&o, '\n');
			s_cat(&o, cn2->name.p ? cn2->name.p : "");
			s_ch(&o, ' ');
			s_num(&o, cn2->row);
			s_ch(&o, ' ');
			s_num(&o, cn2->col);
			s_ch(&o, ' ');
			s_num(&o, cn2->rows);
			s_ch(&o, ' ');
			s_num(&o, cn2->cols);
			s_ch(&o, ' ');
			s_num(&o, cn2->primary);
		}
		hd_send(c, HD_CLIENTS, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_GFXLIST: {
		size_t ci;

		s_init(&o);
		for (ci = 0; ci < cls->n; ci++) {
			struct hd_cli *cn2 = cls->p[ci];

			if (o.n)
				s_ch(&o, '\n');
			s_cat(&o, cn2->name.p ? cn2->name.p : "");
			s_ch(&o, ' ');
			s_cat(&o, gx_name(cn2->gfx));
		}
		hd_send(c, HD_GFXLIST, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_PIXLIST: {
		size_t ci;

		s_init(&o);
		for (ci = 0; ci < cls->n; ci++) {
			struct hd_cli *cn2 = cls->p[ci];

			if (o.n)
				s_ch(&o, '\n');
			s_cat(&o, cn2->name.p ? cn2->name.p : "");
			s_cat(&o, cn2->mpix ? " yes" : " no");
		}
		hd_send(c, HD_PIXLIST, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_MOVE: {
		struct hd_cli *cn2 = 0;
		char *sp1, *sp2;
		int nrow = 0, ncol = 0;
		const char *rep;

		sp1 = in.n ? memchr(in.p, ' ', in.n) : 0;
		if (sp1) {
			*sp1 = 0;
			sp2 = strchr(sp1 + 1, ' ');
			if (sp2) {
				*sp2 = 0;
				nrow = atoi(sp1 + 1);
				ncol = atoi(sp2 + 1);
				cn2 = hd_cfindname(cls, in.p);
			}
		}
		if (cn2) {
			struct hd_cli *pc = hd_cprimary(cls);

			if (pc) {
				nrow += pc->row;
				ncol += pc->col;
			}
			cn2->row = nrow;
			cn2->col = ncol;
			free(cn2->front);
			cn2->front = 0;
			if (hd_union(cls, id, tid))
				hd_rensend(cls, tid);
			else
				/* cn2 may be freed by a failed send. */
				hd_rensend1(cls, cn2, tid);
			/* A moved display may be the primary one: nudge the
			   program to notice, same as a fresh attach does. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_MOVE, rep, strlen(rep));
		break;
	}
	case HD_SLOT: {
		struct hd_slot *sl = in.n ? hd_slotfind(in.p, in.n) : 0;
		struct hd_cli *ci;
		size_t si;
		int sr, sc2;

		/* Not while one by that name is here, and not a place another
		   display has taken since: a -r drops everyone, and two
		   displays on one place would both show the same thing. */
		for (si = 0; sl && si < cls->n; si++) {
			ci = cls->p[si];
			sr = sl->row + hd_shiftr;
			sc2 = sl->col + hd_shiftc;
			if (hd_slotfind(ci->name.p, ci->name.n) == sl ||
			    (sr < ci->row + ci->rows && ci->row < sr + sl->rows &&
			     sc2 < ci->col + ci->cols && ci->col < sc2 + sl->cols))
				sl = 0;
		}
		s_init(&o);
		if (sl) {
			s_num(&o, sl->row + hd_shiftr);
			s_ch(&o, ' ');
			s_num(&o, sl->col + hd_shiftc);
			s_ch(&o, ' ');
			s_num(&o, sl->rows);
			s_ch(&o, ' ');
			s_num(&o, sl->cols);
			s_ch(&o, ' ');
			s_num(&o, sl->primary);
		}
		hd_send(c, HD_SLOT, o.p, o.n);
		s_free(&o);
		break;
	}
	case HD_SHIFT:
		s_init(&o);
		s_num(&o, hd_shiftr);
		s_ch(&o, ' ');
		s_num(&o, hd_shiftc);
		hd_send(c, HD_SHIFT, o.p, o.n);
		s_free(&o);
		break;
	case HD_DROP: {
		struct hd_cli *cn2 = in.n ? hd_cfindname(cls, in.p) : 0;
		int wasp = cn2 ? cn2->primary : 0;
		const char *rep;

		if (cn2) {
			hd_send(cn2->fd, HD_DETACH, "switched off", 12);
			hd_cdrop(cls, cn2->fd);
			hd_union(cls, id, tid);
			if (wasp && cls->n) {
				((struct hd_cli *)cls->p[0])->primary = 1;
				hd_autoprim = 1;
			}
			/* The union shrank, or the primary changed, or both. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_DROP, rep, strlen(rep));
		break;
	}
	case HD_PRIMARY: {
		struct hd_cli *cn2 = in.n ? hd_cfindname(cls, in.p) : 0;
		const char *rep;
		size_t pi;

		if (cn2) {
			for (pi = 0; pi < cls->n; pi++)
				((struct hd_cli *)cls->p[pi])->primary = 0;
			cn2->primary = 1;
			hd_autoprim = 0;
			/* Nudge the program to re-read who is primary now. */
			hd_poke(m);
		}
		rep = cn2 ? "ok" : "no such display";
		hd_send(c, HD_PRIMARY, rep, strlen(rep));
		break;
	}
	}
	close(c);
	s_free(&in);
}

/* The session itself: a program on a terminal, and whoever is attached --
   any number at once, all fed the same bytes and all able to type into it.

   This runs in a process of its own that has left the shell's session, so
   a hangup -- a closed terminal, a logout -- reaches only that one client,
   which is dropped from the list, and the server carries on with whoever
   else (if anyone) is still attached.  While nobody is, what the program
   writes is read and dropped, or it would fill the pty and stop; the next
   attach asks it to draw everything again anyway. */
void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready)
{
	struct sockaddr_un a;
	struct sigaction sa;
	int l, m, id, tid;

	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = hd_onterm;
	sigaction(SIGTERM, &sa, 0);
	hd_selftitle("hold", path);

	l = socket(AF_UNIX, SOCK_STREAM, 0);
	hd_cloexec(l);
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	strncpy(a.sun_path, path, sizeof a.sun_path - 1);
	unlink(path);
	if (l < 0 || bind(l, (struct sockaddr *)&a, sizeof a) < 0 ||
	    listen(l, 8) < 0) {
		hd_wall(ready, "0", 1);
		_exit(1);
	}
	chmod(path, 0600);
	setenv("HIBR_HOLD", path, 1);
	/* This server's emulator applies a rectangle copy and replays it, or
	   diffs, for every client (Gitea #337) -- said here, since the
	   program's own questions to it are answered by no one: it has no pty
	   of its own to answer on. A program under an older server is never
	   told, so it never copies, which is what keeps that one correct. */
	setenv("HIBR_HOLDCOPY", "1", 1);
	id = hd_pty->spawn(s, rows, cols, av);
	hd_wall(ready, id ? "1" : "0", 1);
	close(ready);
	if (!id) {
		unlink(path);
		_exit(1);
	}
	m = hd_pty->fd(id);
	tid = hd_tm->new(rows, cols);
	hd_relay(tid);
	{
		vec cls;

		memset(&cls, 0, sizeof cls);
		hd_loop(s, path, l, m, id, tid, &cls);
	}
}

/* The serving loop itself, entered either by hd_serve with a session it has
   just made or by hd_resume with one taken back after an exec (Gitea #198).
   It owns the client list, and never returns: the session ends here. */
void hd_loop(sh *s, const char *path, int l, int m, int id, int tid,
	     vec *clsp)
{
	struct pollfd *q = 0, pm;
	vec cls;
	int quit = 0, st, fed, upg = 0, owed = 0, due;
	long left;
	size_t i, j, nc;
	ssize_t k;
	str rb, in, mo, rq;

	cls = *clsp;
	s_init(&rb);
	s_init(&rq);
	s_init(&in);
	s_init(&mo);
	s_grow(&rb, HIBR_IOCH);
	while (!quit && !hd_term) {
		fed = 0;
		nc = cls.n;
		q = xr(q, sizeof *q * (2 + nc));
		q[0].fd = l;
		q[0].events = POLLIN;
		q[1].fd = m;
		q[1].events = POLLIN;
		for (i = 0; i < nc; i++) {
			q[2 + i].fd = ((struct hd_cli *)cls.p[i])->fd;
			q[2 + i].events = POLLIN;
		}
		/* A render held back for an open frame wakes the loop when
		   that frame's guard runs out, if nothing else does first. */
		due = hd_gfxdue(&cls, m);
		if (owed && (left = hd_tm->syncleft(tid)) >= 0 &&
		    (due < 0 || left + 1 < due))
			due = (int)left + 1;
		if (poll(q, (nfds_t)(2 + nc), due) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (q[1].revents) {
			k = read(m, rb.p, rb.cap - 1);
			if (k > 0) {
				hd_tm->feed(tid, rb.p, (size_t)k);
				rq.n = 0;
				if (hd_tm->reply(tid, &rq))
					hd_wall(m, rq.p, rq.n);
				hd_passsend(&cls, tid);
				fed = 1;
			} else if (!(k < 0 && (errno == EAGAIN ||
					       errno == EINTR))) {
				break;
			}
		}
		for (j = 0; j < nc; j++) {
			int fd = q[2 + j].fd, t, r;

			if (!q[2 + j].revents || !hd_chas(&cls, fd))
				continue;
			r = hd_recv(fd, &t, &in);
			if (r <= 0) {
				hd_cdrop(&cls, fd);
			} else if (t == HD_DATA) {
				struct hd_cli *cn = hd_cfind(&cls, fd);

				if (cn) {
					mo.n = 0;
					cn->wantpix = hd_tm->mpix(tid);
					hd_mtrans(cn, in.p, in.n, &mo);
					hd_wall(m, mo.p, mo.n);
				}
			} else if (t == HD_SIZE &&
				   (in.n == 2 * sizeof(int) ||
				    in.n == 4 * sizeof(int))) {
				struct hd_cli *cn = hd_cfind(&cls, fd);
				int sz[4];

				/* Two ints is a client from before 0.99.77,
				   which did not say what a cell measures. */
				memset(sz, 0, sizeof sz);
				memcpy(sz, in.p, in.n);
				if (cn) {
					/* Only a real change throws the
					   client's own front away: this
					   arrives once at attach as well,
					   carrying the pixels, and a
					   needless full repaint there swallows
					   whatever small diff came next. */
					int same = cn->rows == sz[0] &&
						   cn->cols == sz[1];

					cn->rows = sz[0];
					cn->cols = sz[1];
					if (in.n == 4 * sizeof(int)) {
						if (cn->xpix != sz[2] ||
						    cn->ypix != sz[3])
							cn->imgen = 0;
						cn->xpix = sz[2];
						cn->ypix = sz[3];
					}
					if (!same) {
						free(cn->front);
						cn->front = 0;
						cn->imgen = 0;
					}
					hd_union(&cls, id, tid);
				}
			} else if (t == HD_GFX && in.n == sizeof(int)) {
				struct hd_cli *cn = hd_cfind(&cls, fd);
				int g;

				memcpy(&g, in.p, sizeof g);
				if (g < GX_UNKNOWN || g > GX_KITTY)
					g = GX_UNKNOWN;
				/* A changed answer may change what this
				   display is sent, and what the program
				   draws for it: send its pictures afresh
				   and have the program read the displays
				   again. */
				if (cn)
					cn->gfxwait = 0;
				if (cn && cn->gfx != g) {
					cn->gfx = g;
					cn->imgen = 0;
					hd_poke(m);
				}
			} else if (t == HD_KBD && in.n == sizeof(int)) {
				struct hd_cli *cn = hd_cfind(&cls, fd);

				if (cn)
					cn->kbd = 1;
			} else if (t == HD_COPY && in.n == sizeof(int)) {
				struct hd_cli *cn = hd_cfind(&cls, fd);

				if (cn)
					cn->copy = 1;
			} else if (t == HD_MPIX && in.n == sizeof(int)) {
				struct hd_cli *cn = hd_cfind(&cls, fd);

				if (cn)
					cn->mpix = 1;
			} else if (t == HD_DETACH) {
				hd_cdrop(&cls, fd);
			}
		}
		/* A display went this pass, by whatever way: what Displays >
		   Detach has always done, for every way (Gitea #297). */
		if (hd_gone) {
			hd_gone = 0;
			if (cls.n && !hd_anyprimary(&cls)) {
				((struct hd_cli *)cls.p[0])->primary = 1;
				hd_autoprim = 1;
			}
			if (cls.n && hd_union(&cls, id, tid))
				hd_rensend(&cls, tid);
			hd_poke(m);
		}
		if (q[0].revents & POLLIN) {
			int c = accept(l, 0, 0);
			if (c >= 0) {
				hd_cloexec(c);
				hd_conn(c, &cls, id, tid, m, &quit, &upg);
			}
			if (upg) {
				/* Only returns when the exec did not
				   happen: nothing was closed and the
				   session is still here, with the pty
				   taken back under the id it answers. */
				id = hd_upgrade(s, path, &cls, id, tid, m,
						l);
				upg = 0;
			}
		}
		/* Render once the pty is drained, and not while the program is
		   inside a synchronized frame: the emulator holds the live grid,
		   so a frame larger than one read would otherwise reach every
		   client half drawn (Gitea #338). It is owed until the frame
		   closes, or until the emulator's one-second guard says the
		   program is not going to close it. */
		if (fed)
			owed = 1;
		if (owed) {
			pm.fd = m;
			pm.events = POLLIN;
			if ((poll(&pm, 1, 0) <= 0 || !(pm.revents & POLLIN)) &&
			    !hd_tm->syncleft(tid)) {
				hd_rensend(&cls, tid);
				owed = 0;
			}
		}
	}
	unlink(path);
	close(l);
	free(q);
	if (quit || hd_term)
		hd_pty->drop(id);
	for (i = 0; i < 300 && hd_pty->alive(id); i++)
		usleep(10000);
	st = hd_pty->status(id);
	for (i = 0; i < cls.n; i++) {
		struct hd_cli *cn = cls.p[i];

		hd_send(cn->fd, HD_EXIT, (const char *)&st, sizeof st);
		close(cn->fd);
		free(cn->front);
		free(cn->pid);
		free(cn->phash);
		s_free(&cn->mbuf);
		s_free(&cn->name);
		s_free(&cn->title);
		free(cn);
	}
	v_free(&cls);
	hd_pty->drop(id);
	hd_tm->free(tid);
	s_free(&rb);
	s_free(&rq);
	s_free(&in);
	s_free(&mo);
	_exit(0);
}

/* Let a descriptor through an exec, which is the inverse of hd_cloexec and
   the whole trick behind taking a session back: a pty master, a listening
   socket and a client's connection are all just descriptors, and a process
   that execs keeps the ones it has not marked to close. */
void hd_keepexec(int fd)
{
	int f;

	if (fd < 0)
		return;
	f = fcntl(fd, F_GETFD);
	if (f >= 0)
		fcntl(fd, F_SETFD, f & ~FD_CLOEXEC);
}

/* Where this module's own file is, from our own mappings: the exec has to
   load it again by name, and `need hold` only works when the module path
   happens to name it -- a session started by `mod load ./build/mods/hold.so`
   has no module path at all, and the resumed server would then find no hold
   module and the session would be lost. The same path is deliberate rather
   than merely convenient: after an upgrade it is the new file, which is the
   whole point. The " (deleted)" the kernel appends to a replaced inode is
   dropped, since what is wanted is the name, not the inode. */
int hd_selfmod(const char *name, str *out)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char *line = 0;
	size_t cap = 0;
	int got = 0;

	if (!f)
		return 0;
	while (!got && getline(&line, &cap, f) > 0) {
		char *p = strstr(line, " /");
		char *d;

		if (!p || !strstr(line, name))
			continue;
		p++;
		d = strstr(p, " (deleted)");
		if (d)
			*d = 0;
		for (d = p; *d && *d != '\n'; d++)
			;
		*d = 0;
		s_cat(out, p);
		got = 1;
	}
	free(line);
	fclose(f);
	return got;
}

/* Re-exec this server on the hibr that is on disk now, taking the session
   with it (Gitea #198): a restart re-execs the *desktop* inside this
   process, so it can never replace this process's own modules, and the
   emulator every client is rendered from lives here. What is carried is
   small -- the descriptors, the program's pid, and the emulator written out
   by term's own save, whose format is appended to rather than rearranged so
   a new build reads an old file.
   Returns only when the exec did not happen, and then the session is
   untouched and still serving: nothing here closes anything or ends the
   program. It answers the id to carry on with, which is a new one when the
   exec did not happen and the terminal had to be taken back. */
int hd_upgrade(sh *s, const char *path, vec *cls, int id, int tid, int m,
	       int l)
{
	const char *hibr = hibr_get(s, "HIBR");
	long pid = hd_pty->pid(id);
	int rows = hd_tm->rows(tid), cols = hd_tm->cols(tid), nid;
	str st, rp, cmd, mp;
	FILE *f;
	size_t i;

	if (!hibr || !*hibr) {
		lg(HIBR_LERR, "hold upgrade: $HIBR names no shell to exec");
		return id;
	}
	s_init(&mp);
	s_init(&st);
	s_cat(&st, path);
	s_cat(&st, ".state");
	s_init(&rp);
	s_cat(&rp, path);
	s_cat(&rp, ".resume");
	if (!hd_tm->save(tid, st.p)) {
		lg(HIBR_LERR, "hold upgrade: cannot write %s", st.p);
		goto out;
	}
	f = fopen(rp.p, "w");
	if (!f) {
		lg(HIBR_LERR, "hold upgrade: cannot write %s", rp.p);
		goto out;
	}
	fprintf(f, "listen %d\n", l);
	fprintf(f, "pty %d %ld %d %d\n", m, pid, rows, cols);
	fprintf(f, "state %s\n", st.p);
	/* How far the origin has been normalised, carried across the exec.
	   It is what the program translates its own content by, and the
	   counter is this process's -- so without this line an upgrade
	   answers 0 to a program that was last told 12, and the program
	   moves every window back by twelve rows for a shift that never
	   came undone. A line the older reader does not know is skipped
	   by it, so a new server can still resume an old file and the
	   other way about. */
	fprintf(f, "shift %d %d\n", hd_shiftr, hd_shiftc);
	for (i = 0; i < cls->n; i++) {
		struct hd_cli *cn = cls->p[i];

		/* front is not carried: a client that comes back is sent the
		   whole screen, which is what an attach already does, and a
		   stale front is the one thing that would leave it wrong. */
		fprintf(f, "client %d %d %d %d %d %d %d %d %s\n", cn->fd,
			cn->row, cn->col, cn->rows, cn->cols, cn->primary,
			cn->xpix, cn->ypix, cn->name.p ? cn->name.p : "");
		/* Its own line, after the client it belongs to: an older
		   reader skips a line it does not know, so both directions of
		   an upgrade still read. A display carried from a server older
		   than this has no line and is GX_UNKNOWN until it attaches
		   again, which draws half blocks rather than black. */
		fprintf(f, "gfx %d %d\n", cn->fd, cn->gfx);
		fprintf(f, "caps %d %d %d %d\n", cn->fd, cn->kbd, cn->copy,
			cn->mpix);
		hd_keepexec(cn->fd);
	}
	fclose(f);
	hd_keepexec(l);
	hd_keepexec(m);
	/* Let the pty go without ending the program or closing the master:
	   the exec must not take the desktop with it. */
	hd_pty->release(id);
	setenv("HIBR_HOLD_RESUME", rp.p, 1);
	/* All three, by the paths they were loaded from: hold asks for pty
	   and term through hibr_require, which falls back to walking the
	   module path -- and a session started with `mod load ./build/mods/
	   hold.so` has no module path at all, so hold's own init would fail
	   and the session would be lost at the exec. */
	s_init(&cmd);
	for (i = 0; i < 3; i++) {
		static const char *mods[] = { "pty.so", "term.so", "hold.so" };

		mp.n = 0;
		if (mp.p)
			mp.p[0] = 0;
		if (!hd_selfmod(mods[i], &mp))
			continue;
		s_cat(&cmd, "mod load ");
		s_cat(&cmd, mp.p);
		s_cat(&cmd, " || exit 1; ");
	}
	if (!cmd.n)
		s_cat(&cmd, "need hold || exit 1; ");
	s_cat(&cmd, "hold resume");
	execl(hibr, "hibr", "-c", cmd.p, (char *)0);
	lg(HIBR_LERR, "hold upgrade: cannot exec %s: carrying on", hibr);
	unsetenv("HIBR_HOLD_RESUME");
	unlink(st.p);
	unlink(rp.p);
	s_free(&cmd);
	/* Take the terminal back, or carrying on means carrying on without
	   one: the release above has already happened, so this process would
	   go on drawing and relaying a session whose pty it no longer has a
	   record of -- `hold list` answering a pid of -1, no client's size
	   ever reaching the program again, and its exit never noticed. That
	   is worse than either working or stopping, and it is what a $HIBR
	   that is there but not executable used to leave behind. */
	nid = hd_pty->adopt(m, pid, rows, cols);
	if (nid)
		id = nid;
	else
		lg(HIBR_LERR, "hold upgrade: cannot take the terminal back: "
			      "this session can no longer be resized");
out:
	s_free(&st);
	s_free(&rp);
	s_free(&mp);
	return id;
}

/* The other side of it: a server that has just been exec'd takes the
   session back. Everything it needs is in the file HIBR_HOLD_RESUME names
   and in the descriptors the exec kept open. */
int hd_resume(sh *s)
{
	const char *rp = hibr_get(s, "HIBR_HOLD_RESUME");
	char *line = 0;
	size_t lcap = 0;
	str name, stpath;
	int l = -1, m = -1, rows = 0, cols = 0, id = 0, tid = 0, at;
	long pid = 0;
	struct sigaction sa;
	vec cls;
	FILE *f;

	if (!rp || !*rp)
		return HIBR_FAIL;
	f = fopen(rp, "r");
	if (!f) {
		lg(HIBR_LERR, "hold resume: cannot read %s", rp);
		return HIBR_FAIL;
	}
	memset(&cls, 0, sizeof cls);
	s_init(&name);
	s_init(&stpath);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = hd_onterm;
	sigaction(SIGTERM, &sa, 0);
	while (getline(&line, &lcap, f) > 0) {
		int fd, row, col, crows, ccols, prim, xp, yp, shr, shc, a, b, c;

		if (sscanf(line, "listen %d", &l) == 1)
			continue;
		if (sscanf(line, "pty %d %ld %d %d", &m, &pid, &rows,
			   &cols) == 4)
			continue;
		if (!strncmp(line, "state ", 6)) {
			stpath.n = 0;
			s_add(&stpath, line + 6, strcspn(line + 6, " \t\r\n"));
			continue;
		}
		if (sscanf(line, "shift %d %d", &shr, &shc) == 2) {
			hd_shiftr = shr;
			hd_shiftc = shc;
			continue;
		}
		if (sscanf(line, "caps %d %d %d %d", &fd, &a, &b, &c) == 4) {
			struct hd_cli *ck = hd_cfind(&cls, fd);

			if (ck) {
				ck->kbd = !!a;
				ck->copy = !!b;
				ck->mpix = !!c;
			}
			continue;
		}
		if (sscanf(line, "gfx %d %d", &fd, &prim) == 2) {
			struct hd_cli *cg = hd_cfind(&cls, fd);

			if (cg && prim >= GX_UNKNOWN && prim <= GX_UNASKED)
				cg->gfx = prim;
			continue;
		}
		at = 0;
		if (sscanf(line, "client %d %d %d %d %d %d %d %d%n", &fd,
			   &row, &col, &crows, &ccols, &prim, &xp, &yp,
			   &at) == 8 && at > 0) {
			struct hd_cli *cn = xm(sizeof *cn);

			memset(cn, 0, sizeof *cn);
			s_init(&cn->title);
			s_init(&cn->name);
			s_init(&cn->mbuf);
			cn->fd = fd;
			cn->row = row;
			cn->col = col;
			cn->rows = crows;
			cn->cols = ccols;
			cn->primary = prim;
			cn->xpix = xp;
			cn->ypix = yp;
			/* Unasked until a gfx line below says otherwise: a file
			   from a server older than the asking has none, and its
			   clients go on being sent what they were (Gitea #274). */
			cn->gfx = GX_UNASKED;
			name.n = 0;
			at += (int)strspn(line + at, " \t");
			s_add(&name, line + at, strcspn(line + at, " \t\r\n"));
			s_cat(&cn->name, name.p ? name.p : "");
			v_add(&cls, cn);
		}
	}
	free(line);
	s_free(&name);
	fclose(f);
	unlink(rp);
	unsetenv("HIBR_HOLD_RESUME");
	if (l < 0 || m < 0 || rows <= 0 || cols <= 0) {
		lg(HIBR_LERR, "hold resume: %s does not describe a session",
		   rp);
		s_free(&stpath);
		return HIBR_FAIL;
	}
	hd_selftitle("hold", hibr_get(s, "HIBR_HOLD"));
	id = hd_pty->adopt(m, pid, rows, cols);
	if (!id) {
		lg(HIBR_LERR, "hold resume: cannot take the terminal back");
		s_free(&stpath);
		return HIBR_FAIL;
	}
	tid = hd_tm->new(rows, cols);
	hd_relay(tid);
	if (stpath.n && !hd_tm->load(tid, stpath.p))
		lg(HIBR_LINF, "hold resume: starting from a blank screen");
	if (stpath.n)
		unlink(stpath.p);
	s_free(&stpath);
	/* The arrangement came back out of a file, so it is the first
	   thing this process has that nothing has checked. Normalising
	   runs from hd_union, and hd_union runs when a client attaches,
	   moves, drops or resizes -- none of which an upgrade is. So a
	   session that was already in a negative arrangement when its
	   server was replaced kept it, with the new code sitting there
	   unreached: `hold shift` answered 0 0 while `hold clients` still
	   read `client-6 -66 3`, and the display above the primary went
	   on drawing nothing. The whole point of upgrading a session is
	   to get the fix, so this is where it has to be applied
	   (Gitea #257, found live right after 0.99.220 shipped). */
	hd_union(&cls, id, tid);
	/* Every client is sent the whole screen again rather than a
	   difference from a front this process never had. */
	hd_rensend(&cls, tid);
	hd_loop(s, hibr_get(s, "HIBR_HOLD"), l, m, id, tid, &cls);
	return HIBR_OK;
}
