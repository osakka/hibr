#ifndef HD_H
#define HD_H

#include "hibr.h"
#include "../display.h"
#include "../pty.h"
#include "../term.h"
#include "../gfxask.h"

/* Synchronized output's pair, which brackets every frame sent a client. */
#ifndef HD_SYNCH
#define HD_SYNCH "\033[?2026h"
#endif
#ifndef HD_SYNCL
#define HD_SYNCL "\033[?2026l"
#endif

enum {
	HD_ATTACH = 'a',
	HD_DATA = 'd',
	HD_SIZE = 'w',
	HD_DETACH = 'q',
	HD_EXIT = 'x',
	HD_KILL = 'k',
	HD_INFO = 'i',
	/* Join as an additional client rather than taking over: everyone
	   already attached stays attached. */
	HD_MATTACH = 'm',
	/* One line per attached client: name row col rows cols primary. */
	HD_CLIENTS = 'c',
	/* One line per attached client: name, then what its own terminal
	   draws -- kitty, sixel, none or unknown (Gitea #258). Its own request
	   rather than a seventh field of HD_CLIENTS, which ten readers take
	   with the primary flag last: a desktop still running beside an
	   upgraded server would have read the flag as "1 kitty" and lost its
	   primary. A server without it closes the request unanswered, at once,
	   which the reader takes as unknown. */
	HD_GFXLIST = 'l',
	/* Whether each display's own terminal reports the mouse in pixels
	   (1016), name and yes or no a line, as HD_MPIX said at attach --
	   what the desktop asks to decide where hovering is on by default
	   (Gitea #370). Its own request for the reason HD_GFXLIST is. */
	HD_PIXLIST = 'P',
	/* Reposition a named client: name row col, relative to where the
	   primary display is -- which every normalisation moves with the
	   rest, so the same request means the same place whenever it is
	   sent (Gitea #297). With no primary it is the union's own frame. */
	HD_MOVE = 'v',
	/* Where a named display was when it last went, as "row col rows cols
	   primary" in the union's frame now, or an empty reply when none is
	   remembered or one by that name is attached (Gitea #297). A server
	   that does not know it closes the request unanswered. */
	HD_SLOT = 'S',
	/* Detach a named client, server-side, by name rather than by fd. */
	HD_DROP = 'o',
	/* Make a named client the primary; every other client stops being it. */
	HD_PRIMARY = 'p',
	/* How far the origin has been normalised since this server started,
	   as "row col". A display put above or to the left of the others
	   needs a negative corner to say so, and hold answers by moving
	   every client together so the arrangement's own top-left is 0,0
	   (Gitea #257) -- which moves the program's idea of where each
	   display is, under content the program has already placed. Asking
	   the primary where it is cannot tell that apart from the primary
	   simply *changing*, and asking one client how far it moved cannot
	   tell a shift from that client having been dragged: both were
	   tried. This is the one unambiguous answer, so hold keeps it
	   rather than leaving the program to infer it. */
	HD_SHIFT = 'n',
	/* Re-exec the server on the hibr that is on disk now, taking the
	   session with it: the one thing a Restart Desktop cannot do, since
	   that re-execs the desktop *inside* this process (Gitea #198). */
	HD_UPGRADE = 'u',
	/* What this client's own terminal draws, as one int (GX_KITTY,
	   GX_SIXEL, GX_NONE or GX_UNKNOWN), asked by the client itself before
	   it joins (Gitea #258). A server that does not know the message
	   ignores it, and a client that never sends it is GX_UNKNOWN, which
	   is sent no pictures. */
	HD_GFX = 'g',
	/* From a client: its own terminal answered the kitty keyboard
	   protocol's query, asked with the picture questions before it
	   joined (Gitea #326). One int, 1. A server that does not know it
	   ignores it, and a client that never sends it is sent no flags. */
	HD_KBD = 'b',
	/* From a client: its own terminal said it copies a rectangle of cells
	   (DA1 feature 28, DECCRA), asked with the rest before it joined
	   (Gitea #337). One int, 1. Such a client is sent the copies the
	   program made, replayed where its view holds both ends of one,
	   rather than every cell of the destination again. A server that does
	   not know it ignores it, and a client that never sends it is diffed,
	   as every client was. */
	HD_COPY = 'y',
	/* From a client: its own terminal answered the mode request for
	   1016, SGR-Pixels, asked with the rest before it joined (Gitea
	   #370). One int, 1. Such a client is asked for pixels when the
	   program wants them; any other is turned into pixels at the middle
	   of a cell. A server that does not know it ignores it. */
	HD_MPIX = 'z',
	/* From a client: its own terminal answered the pointer-shape query,
	   OSC 22 `?none`, asked with the rest before it joined (Gitea #373).
	   One int, 1 when it can hide its pointer and 0 when it cannot; never
	   sent by a client whose terminal did not answer. Such a client is
	   sent the shape the program asks for, and `none` only when it said
	   1. A server that does not know it ignores it. */
	HD_PTR = 'h',
	/* Whether each display's own terminal can hide its pointer, name and
	   yes or no a line, as HD_PTR said -- what the desktop asks before
	   offering a window a hidden pointer. Its own request for the reason
	   HD_GFXLIST is. */
	HD_PTRLIST = 'H',
	/* What each display has been sent since it attached, name, bytes and
	   frames a line (Gitea #353). Its own request for the reason
	   HD_GFXLIST is. */
	HD_STATS = 's',
	/* From a client: its own terminal said, by XTGETTCAP, that an erase
	   paints the pen's background (bce), asked with the rest before it
	   joined (Gitea #353). One int, 1. Such a client is sent a run of
	   blank cells in a colour as one erase; any other only a run with no
	   colour of its own. A server that does not know it ignores it. */
	HD_BCE = 'e'
};

/* How long an attaching client waits for its terminal to say what it
   draws. Nearly every terminal answers the device attributes at once; one
   that answers nothing costs this, once, at attach. */
#ifndef HD_GFXWAITMS
#define HD_GFXWAITMS 500
#endif
#ifndef HD_GFXMS
#define HD_GFXMS 1000
#endif

/* How long a request to a session may wait. A control request to a local
   socket that does not answer promptly is a session in trouble, never a
   caller waiting on purpose -- which is why these are defaults rather than
   opt-in, where net_conn's own bound is the other way about (a /dev/tcp
   script may mean to wait). HIBR_CONNTIMEO overrides both. The dial is the
   short one: an AF_UNIX connect completes at once unless the backlog is
   full, which means nobody is accepting. The reply is the generous one: the
   server answers between frames, and a frame under load is tens of
   milliseconds. */
#ifndef HD_DIALMS
#define HD_DIALMS 1000
#endif
#ifndef HD_REPLYMS
#define HD_REPLYMS 5000
#endif

extern const py_api *hd_pty;
extern const tm_api *hd_tm;

/* The running total of what hd_unorm has moved every client by. */
extern int hd_shiftr, hd_shiftc;

/* What a cell of the session measures, as the program was last told:
   the space a pointer in pixels is given in, whichever display it came
   from (Gitea #370). 0 when no display has said. */
extern int hd_scw, hd_sch;

/* One cell as this client was last sent it -- hold's own copy, not term's:
   hold must not include tm.h just to name tm_cell, and the two have no
   reason to stay the same shape. cp is a code point (0 is never a real one
   tm_api hands back, ~0u a cell this client has not been sent yet at all,
   so a fresh or just-resized front is invalid everywhere without a second
   fill pass). */
/* A cell as a client was last sent it. w is its width, 0 for the second
   half of a wide glyph, which is what lets a replayed rectangle copy send
   again only the cells it cut a wide glyph at (Gitea #337). */
struct hd_cell { unsigned cp, fg, bg, attr, link; unsigned char w; };

/* A run of blank cells inside a row this long or longer goes out as one
   erase of characters (ECH) rather than a space each; and a gap of
   unchanged cells this short or shorter is drawn over rather than moved
   past, when that is the cheaper (Gitea #353). */
#ifndef HD_ECHMIN
#define HD_ECHMIN 8
#endif
#ifndef HD_GAPMAX
#define HD_GAPMAX 6
#endif

/* One attached terminal: its connection, where it sits in the session's own
   virtual space -- row,col its top-left corner, rows,cols its own size --
   the mode state last sent to it, so a real terminal is only ever told to
   switch buffers, mice or paste mode on a genuine change, not every frame,
   and front: what it was last sent, cell by cell, so a settled frame sends
   only what actually changed. A lone attacher sits at 0,0 and the space is
   exactly its own size, which is today's behaviour exactly; several placed
   side by side is what a multi-monitor arrangement attaches through.
   mbuf/mst are a small state machine of their own: a client's own mouse
   report, in SGR form, has to be rewritten by this client's own offset
   before it reaches the pty, and a report can arrive split across more
   than one read. Shared between srv.c, which owns the list, render.c,
   which owns front and only ever reads one entry at a time, and mouse.c,
   which owns mbuf/mst. */
struct hd_cli {
	int fd, row, col, rows, cols;
	/* This client's own terminal in pixels, as its ws_xpixel/ws_ypixel
	   say: 0 when it does not know, and what hd_cellpx turns into the
	   cell size the program is told about (ADR 0037, Gitea #103). imgen
	   is the generation of the emulator's own picture set this client has
	   been sent, so a settled frame sends no picture twice and a repaint
	   sends them all. */
	int xpix, ypix;
	unsigned imgen;
	/* The kitty pictures this client's terminal is holding for us: each
	   one's id and a hash of the bytes last sent for it, so a frame that
	   changes one picture sends that one and not every other -- a
	   wallpaper is 860 kB, and re-placing it because a viewer's own
	   picture moved is what made pixels unusable over a slow link. An id
	   that is no longer in the session is deleted from the terminal,
	   since a kitty picture stays until it is told to go. A sixel is
	   paint and is simply painted again. */
	unsigned *pid, *phash;
	size_t pn;
	int primed, alt, mmode, msgr, bpaste, cshape;
	struct hd_cell *front;
	str mbuf;
	int mst;
	/* A display's own name, for the control panel: what a client asked to
	   be called at attach, or "client-<fd>" when it asked for nothing.
	   primary marks the one the desktop's own bar and menu anchor to --
	   the first client to attach, until the panel picks another. */
	str name;
	int primary;
	/* What this client's own terminal said it draws, which decides
	   whether a picture is sent to it at all: a bitmap sent to a terminal
	   that cannot draw one is a black rectangle nothing repaints, and once
	   any one display could show pictures the session sent them to every
	   display (Gitea #258). gfxwait is when a client that has not said
	   becomes unasked, 0 once it has (Gitea #279). */
	int gfx;
	long gfxwait;
	/* Whether this client's terminal speaks the kitty keyboard protocol,
	   and the flags last put on it, which hd_modes keeps in step with
	   what the program asked for. */
	int kbd, kset;
	/* Whether this client's terminal copies rectangles, and the last of
	   the program's copies it has been sent or no longer needs. */
	int copy;
	unsigned cseq;
	/* Whether this client's terminal reports the mouse in pixels (1016),
	   whether it has been asked to, and whether the program wants pixels
	   -- read again before each of the client's reads is passed on, so a
	   report is turned into what the program asked for at that moment. */
	int mpix, pixon, wantpix;
	/* What this client's terminal said about pointer shapes (Gitea #373):
	   0 nothing, 1 it knows them and cannot hide, 2 it can hide -- and the
	   shape last sent to it, so a change is sent once. */
	int ptrask;
	str ptrsh;
	/* What this client has been sent since it attached: bytes and frames,
	   for `hold stats` -- the number a leaner renderer is judged by, per
	   display, since each is drawn on its own (Gitea #353). */
	long long sent;
	long frames;
	/* Whether its terminal said an erase paints the pen's background. */
	int bce;
	/* The title last sent to this client's terminal (OSC 2), so it is
	   sent again only when the program changes it. */
	str title;
};

unsigned hd_hash(const char *p, size_t n);
unsigned hd_imgid(const char *p, size_t n);
int hd_canshow(const struct hd_cli *cn, const char *p, size_t n);
void hd_relay(int tid);
int hd_gfxdue(vec *cls, int m);
/* Where a named display was when it last went (Gitea #297): in the frame
   from before any normalisation, a position less hd_shiftr/hd_shiftc,
   which no later shift moves; by the name before any #. */
struct hd_slot {
	str name;
	int row, col, rows, cols, primary;
};

void hd_cdrop(vec *cls, int fd);
void hd_slotkeep(struct hd_cli *cn);
struct hd_slot *hd_slotfind(const char *nm, size_t n);
struct hd_cli *hd_cprimary(vec *cls);
struct hd_cli *hd_cfind(vec *cls, int fd);
struct hd_cli *hd_cfindname(vec *cls, const char *name);
int hd_chas(vec *cls, int fd);
int hd_anyprimary(vec *cls);
int hd_unorm(vec *cls);
void hd_ubox(vec *cls, int *rows, int *cols);
void hd_cellpx(vec *cls, int *w, int *h);
int hd_union(vec *cls, int id, int tid);
void hd_cclear(vec *cls, const char *why, size_t n);
void hd_rensend1(vec *cls, struct hd_cli *cn, int tid);
void hd_passsend(vec *cls, int tid);
void hd_rensend(vec *cls, int tid);
int hd_mpixel(struct hd_cli *cn, long *x, long *y);
void hd_mtrans(struct hd_cli *cn, const char *p, size_t n, str *out);

int hd_dir(str *out);
int hd_path(const char *name, str *out);
int hd_nameok(const char *name);
int hd_wall(int fd, const char *p, size_t n);
int hd_rall(int fd, char *p, size_t n);
int hd_send(int fd, int type, const char *p, size_t n);
int hd_recv(int fd, int *type, str *out);
long hd_timeo(long dflt);
int hd_dial(const char *path);
int hd_askfd(int fd, int type, const char *p, size_t n, str *reply);
int hd_ask(const char *path, int type, str *reply);
int hd_askp(const char *path, int type, const char *p, size_t n, str *reply);
void hd_size(int *sz);
void hd_sizepx(int *sz);
void hd_selftitle(const char *what, const char *path);
void hd_cloexec(int fd);

void hd_serve(sh *s, const char *path, int rows, int cols, char **av,
	      int ready);
void hd_loop(sh *s, const char *path, int l, int m, int id, int tid,
	     vec *clsp);
void hd_keepexec(int fd);
int hd_selfmod(const char *name, str *out);
int hd_resume(sh *s);
int hd_upgrade(sh *s, const char *path, vec *cls, int id, int tid, int m,
	       int l);
int hd_attach(const char *name, const char *path, int multi, int row,
	      int col, const char *dname);

#endif
