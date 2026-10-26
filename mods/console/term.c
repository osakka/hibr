#define _GNU_SOURCE

#include "cn.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int cn_fd = -1;
int cn_own;
int cn_on;
pid_t cn_pid;
struct termios cn_sv;
volatile sig_atomic_t cn_winch;
volatile sig_atomic_t cn_wgen;
volatile sig_atomic_t cn_fatal;
struct sigaction cn_oint, cn_oterm, cn_ohup, cn_owin;

/* The signals that end a process without asking it, each of which must
   put the terminal back first, and what was installed for them before. */
static const int cn_fatals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
struct sigaction cn_ofatal[sizeof cn_fatals / sizeof cn_fatals[0]];

/* Whether ctrl-c, ctrl-\ and ctrl-z raise signals, as they do by default,
   or arrive as keys for the program to use. */
int cn_isig = 1;

/* What the terminal says its own colours are, asked for with OSC 11 and
   OSC 4 and answered into cn_gotcolour by the key decoder.

   Why the display wants to know: a terminal compositing a picture drawn
   under the text decides per cell whether that cell has a background of
   its own, and kitty decides it by comparing the cell's *resolved* value
   with the window's own background colour --
   `cell_has_default_bg = 1 - step(1, abs(bg_as_uint - bg_colors0))` in its
   own shader. So a cell a program painted an explicit black, on a terminal
   whose background is black, counts as having no background and the
   picture is let through it: an ncurses program in a window over a picture
   wallpaper was see-through, where every window drawn in a theme's own RGB
   was not (Gitea #215). Nothing hibr computes is wrong there, so the only
   way to keep such a cell opaque is to paint it a colour that is not the
   terminal's own -- which means knowing what that is. */
void cn_wr(int fd, const char *p, size_t n);

static int cn_askgen = -1;
/* The picture protocol in force, and what the terminal has said it can
   draw: -1 is "not decided" and "has not said", which are different
   questions with the same spelling and both have to stay askable. */
static int cn_gfxk = -1;
static int cn_capkit = -1;
static int cn_capsix = -1;
/* And whether each has been answered since the last ask, which is what
   makes a *negative* possible for a protocol that can only ever answer
   yes: the kitty query goes out before the device attributes request, and
   a terminal answers in the order it was asked, so an attributes reply
   with no `OK` ahead of it is a terminal that does not speak kitty's
   protocol. Positives are kept across an ask rather than forgotten, or a
   plain window resize would drop to the name list for a frame and draw
   the wrong thing on the way back. */
static int cn_saidkit;
static int cn_saidsix;
static unsigned cn_tbgv;
static int cn_tbgok;
static unsigned cn_tpal[16];
static int cn_tpalok[16];

/* The queries, asked once at open and again on every resize -- a resize is
   also how a session reattached from somewhere else arrives, and that
   somewhere else is a different terminal with its own colours. Sent with
   ST rather than BEL, and never answered by anything that has colours of
   its own to report (a hibr terminal window answers for its own window);
   an emulator that cannot answer passes them on to whoever is watching,
   which is what carries them out of a held session. */
static const char cn_askbg[] = "\033]11;?\033\\";

/* And what it can *draw*, asked rather than guessed from its name (Gitea
   #216). A name list answers what a family usually does, where the
   question is what this terminal, right now, actually does -- and inside
   `hold` the name is the session's own, frozen when it was started, so a
   client reattaching from another machine could never change it: a session
   begun in kitty went on sending kitty escapes to a terminal that drops
   them, and one begun anywhere nameless went on drawing half blocks to a
   terminal that can place pixels.

   Both capabilities answer for themselves. The kitty protocol has a query
   of its own -- one pixel, `a=q`, no `q=` so the answer is not suppressed
   -- which kitty replies `OK` to and a terminal that does not speak it
   replies nothing to. Sixel is in the primary device attributes: a reply
   carrying 4 among its parameters says so, and one without it says the
   opposite, which is the one *negative* either query can give. The two
   questions and the reading of their answers are mods/gfxask.c's, shared
   with hold's attaching client (Gitea #258). */

/* The sixteen colours a program may name instead of an RGB value, as the
   standard palette has them -- what an index resolves to while the
   terminal has not said otherwise. Only these are themeable in practice;
   the cube and the greys above 15 are computed. */
static const unsigned cn_palstd[16] = {
	0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
	0xc0c0c0, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff,
	0x00ffff, 0xffffff
};

/* Ask the terminal what its background and its sixteen palette entries
   are. The replies arrive as input and are swallowed by the decoder, so
   nothing a program reads is disturbed; a terminal that answers neither
   leaves the assumptions in place. */
void cn_askcolours(void)
{
	str q;
	int i;

	if (cn_fd < 0)
		return;
	cn_askgen = (int)cn_wgen;
	cn_saidkit = 0;
	/* The terminal answering may not be the one that said it reports
	   pixels: back to cells until it says so again, so one that does not
	   is never read as if it did. */
	cn_pixok = -1;
	cn_pixapply();
	cn_saidsix = 0;
	s_init(&q);
	s_cat(&q, cn_askbg);
	for (i = 0; i < 16; i++) {
		s_cat(&q, "\033]4;");
		s_num(&q, i);
		s_cat(&q, ";?\033\\");
	}
	/* Who is answering, before the device attributes: an emulator of
	   hibr's names itself, so its feature 28 can be told from a real
	   terminal's relayed through one (Gitea #337). A terminal answers in
	   the order it is asked. */
	s_cat(&q, "\033[>q");
	s_cat(&q, gx_ask);
	cn_wr(cn_fd, q.p, q.n);
	s_free(&q);
	lg(HIBR_LDBG, "console: asked the terminal what it is and what it draws");
}

/* What the terminal answered about pictures: 1 yes, 0 no, -1 it has not
   said. Silence is never a no -- a query can be eaten by a terminal in the
   middle (GNU screen forwards no APC) and a held program asks before any
   client exists at all, which is the trap cn_gfx's own comment already
   records about a cell size (Gitea #103). Only a device attributes reply
   that came *without* 4 is a real no, and only about sixel. */
void cn_gotcap(int kind, int yes)
{
	int *p = kind == CN_GFX_KITTY ? &cn_capkit : &cn_capsix;

	if (kind == CN_GFX_KITTY) {
		cn_saidkit = 1;
	} else {
		cn_saidsix = 1;
		if (!cn_saidkit && cn_capkit != 0) {
			cn_capkit = 0;
			cn_gfxk = -1;
			lg(HIBR_LDBG, "console: the terminal answered what it "
				      "is without answering kitty's query, so "
				      "it does not speak it");
		}
	}
	if (*p == yes)
		return;
	*p = yes;
	cn_gfxk = -1;
	lg(HIBR_LDBG, "console: the terminal %s %s", yes ? "draws" : "does not draw",
	   cn_gfxname(kind));
}

/* The kitty keyboard protocol's disambiguate flag, pushed once the terminal
   has answered its query, and the pop that undoes it (Gitea #326). Flag 1
   alone: every key that types text still arrives as text, and what changes
   is that escape, alt and ctrl chords arrive as `CSI ... u` -- which is the
   only way a terminal can say super at all. Pushed at most once a display,
   since the question is asked again on every resize. */
static const char cn_kbdpush[] = "\033[>1u";
static const char cn_kbdpop[] = "\033[<u";
int cn_kbdon;

/* The terminal answered the keyboard protocol's query: push the
   disambiguate flag, once, and only from the process that holds the
   screen. */
void cn_gotkbd(void)
{
	if (cn_kbdon || !cn_on || cn_fd < 0 || getpid() != cn_pid)
		return;
	cn_wr(cn_fd, cn_kbdpush, sizeof cn_kbdpush - 1);
	cn_kbdon = 1;
	lg(HIBR_LDBG, "console: the terminal speaks the kitty keyboard "
		      "protocol; asked it to tell chords apart");
}

/* Ask again when the terminal may not be the one that was asked: a resize
   is also how a session reattached from somewhere else arrives, and the
   first ask of all reaches nobody when a held program opens its display
   before any client has attached. Called from every flush rather than left
   to the program: a bare program never calls cn_reassert, and the one that
   needs this most -- anything inside `hold` -- is exactly the one whose
   first ask was thrown away. */
void cn_askmaybe(void)
{
	if (cn_askgen != (int)cn_wgen)
		cn_askcolours();
}

/* One answer: which is 11 for the background, 4 for a palette entry, and
   idx is the entry. A later answer replaces an earlier one, which is what
   several attached displays each answering comes to -- the last one to say
   is the one believed, the same choice hd_cellpx makes about a cell's
   size. */
void cn_gotcolour(int which, int idx, unsigned rgb)
{
	if (which == 11) {
		cn_tbgv = rgb & 0xFFFFFF;
		cn_tbgok = 1;
		lg(HIBR_LDBG, "console: the terminal's background is #%06x",
		   cn_tbgv);
		return;
	}
	if (which == 4 && idx >= 0 && idx < 16) {
		cn_tpal[idx] = rgb & 0xFFFFFF;
		cn_tpalok[idx] = 1;
		lg(HIBR_LDBG, "console: the terminal's colour %d is #%06x",
		   idx, cn_tpal[idx]);
	}
}

/* The terminal's own background. Black while it has not said: every
   terminal anybody has reported this from has a dark one, and the cost of
   assuming wrong is one cell drawn a shade off rather than a cell that
   disappears. cn_termbgok says which of the two it is. */
unsigned cn_termbg(void)
{
	return cn_tbgok ? cn_tbgv : 0;
}

/* Whether that was answered or assumed. */
int cn_termbgok(void)
{
	return cn_tbgok;
}

/* What a colour resolves to on this terminal, as 24 bits: an RGB value is
   itself, an index below sixteen is what the terminal said it is (or the
   standard palette, which is what it almost certainly is), and the rest is
   the cube and the greys, which are defined rather than themed. */
unsigned cn_rgbof(unsigned v)
{
	unsigned n, r, g, b;

	if (v & DP_RGB)
		return v & 0xFFFFFF;
	n = v & 0xFF;
	if (n < 16)
		return cn_tpalok[n] ? cn_tpal[n] : cn_palstd[n];
	if (n < 232) {
		n -= 16;
		r = n / 36;
		g = (n / 6) % 6;
		b = n % 6;
		r = r ? 55 + r * 40 : 0;
		g = g ? 55 + g * 40 : 0;
		b = b ? 55 + b * 40 : 0;
		return (r << 16) | (g << 8) | b;
	}
	n = 8 + (n - 232) * 10;
	return (n << 16) | (n << 8) | n;
}

static const char cn_leave[] = "\033[?25h\033[0m\033[?1016l\033[?1002l\033[?1006l"
			       "\033[?2004l\033[?1049l";
/* The pen goes back to the terminal's own before the clear: a terminal that
   erases in the pen's background (bce) would otherwise clear the screen in
   whatever colour the last program there left, and this console's front grid
   would believe every cell blank (Gitea #387). */
static const char cn_enter[] = "\033[?1049h\033[?25l\033[?2004h\033[0m\033[2J";

/* Named, so their lengths come from the compiler rather than from counting.
   Counting by hand is what truncated the clear in cn_enter once already. */
static const char cn_moff[] = "\033[?1016l\033[?1003l\033[?1002l\033[?1000l"
			      "\033[?1006l";
static const char cn_pxon[] = "\033[?1016h";
static const char cn_pxoff[] = "\033[?1016l\033[?1006h";
static const char cn_mclick[] = "\033[?1000h\033[?1006h";
static const char cn_mdrag[] = "\033[?1002h\033[?1006h";
static const char cn_mmotion[] = "\033[?1003h\033[?1006h";

/* Mouse reporting is off until something asks. Turning it on takes click and
   drag text selection away from whoever is watching, which is too rude to do
   to every full-screen program. 1006 is the SGR form, the only one that can
   report a column past 223. */
int cn_mousemode;
int cn_pixwant, cn_pixok = -1, cn_pixon;

/* Whether a picture under the text -- the wallpaper -- is sent at half the
   pixels in each direction for the terminal to scale back up. On by default:
   it is four times fewer bytes to every client that attaches, and nobody
   looks at a background. `console imgdetail full` turns it off. */
/* How much resolution a picture is sent at: CN_DETAIL_AUTO halves one drawn
   *under* the text and sends every other at the cell's own resolution,
   CN_DETAIL_FULL sends everything whole, CN_DETAIL_HALF halves everything.
   Auto is the default because the two kinds want opposite answers: a
   wallpaper sits behind text as a background, is re-sent to every client
   that attaches and is never looked at closely, where a film's frame and a
   page are the thing being looked at and halving them is what made a video
   read as blocks (Gitea #228). The knob reaches both so a slow link has a
   way back. */
int cn_imghalf = CN_DETAIL_AUTO;

/* And whether its payload is compressed before it goes out, which is worth
   more than the halving and composes with it: the owner's own wallpaper,
   already halved, is 2.11 MB of base64 raw, 0.62 MB as a zlib stream and
   0.45 MB as a PNG. Zlib by default -- see cn.h for why the PNG is not, and
   `console imgcomp` to ask for it anyway. Only a still is compressed, which
   image.c decides: a film's frame is a different picture every frame, so
   there its cost would be encoding rather than bytes. */
int cn_imgcomp = CN_COMP_ZLIB;

void cn_wr(int fd, const char *p, size_t n);

/* Ask the terminal for mouse reports, or stop asking. */
void cn_mouseon(int mode)
{
	if (!cn_on) {
		cn_mousemode = mode;
		return;
	}
	if (cn_mousemode)
		cn_wr(cn_fd, cn_moff, sizeof cn_moff - 1);
	cn_mousemode = mode;
	cn_pixon = 0;
	if (mode == 1)
		cn_wr(cn_fd, cn_mclick, sizeof cn_mclick - 1);
	else if (mode == 2)
		cn_wr(cn_fd, cn_mdrag, sizeof cn_mdrag - 1);
	else if (mode == 3)
		cn_wr(cn_fd, cn_mmotion, sizeof cn_mmotion - 1);
	lg(HIBR_LDBG, "mouse reporting mode %d", mode);
	cn_pixapply();
}

/* Pixels only once the terminal has said it reports them, a program wants
   them, the mouse is on and a cell's size is known -- without the last, a
   pixel cannot be put in a cell. 1016 is set after 1006 because a terminal
   that keeps one encoding takes the last one set, and turned off with 1006
   set again so the reports stay SGR. Reports and modes cross on the wire,
   so a report already sent may arrive in the form before; that is one
   report, once. */
void cn_pixapply(void)
{
	int cw, ch, on;

	if (!cn_on || cn_fd < 0)
		return;
	cn_cellpx(&cw, &ch);
	on = cn_pixwant && cn_mousemode && cn_pixok == 1 && cw > 1 && ch > 1;
	if (on == cn_pixon)
		return;
	if (on)
		cn_wr(cn_fd, cn_pxon, sizeof cn_pxon - 1);
	else
		cn_wr(cn_fd, cn_pxoff, sizeof cn_pxoff - 1);
	cn_pixon = on;
	lg(HIBR_LDBG, "console: the mouse is reported in %s",
	   on ? "pixels" : "cells");
}

/* The terminal answered the mode request for 1016: yes or no. An answer
   to the device attributes with none before it is a no as well, which
   cn_report says. */
void cn_gotmpix(int yes)
{
	if (cn_pixok == yes)
		return;
	cn_pixok = yes;
	lg(HIBR_LDBG, "console: the terminal %s the mouse in pixels",
	   yes ? "reports" : "does not report");
	cn_pixapply();
}

/* Write a whole buffer, retrying a short or interrupted write. */
void cn_wr(int fd, const char *p, size_t n)
{
	ssize_t k;

	while (n) {
		k = write(fd, p, n);
		if (k > 0) {
			p += k;
			n -= (size_t)k;
			continue;
		}
		if (k < 0 && errno == EINTR)
			continue;
		break;
	}
}

/* Note a terminal resize; the grids are rebuilt outside the handler. */
void cn_onwinch(int n)
{
	(void)n;
	cn_winch = 1;
	cn_wgen++;
}

/* The pointer (Gitea #373): what the terminal said about hiding it -- -1
   nothing, 0 it knows shapes and cannot hide, 1 it can -- the shape last
   asked for, and whether one is in force, which is what decides whether
   leaving has a pointer to give back. A shape is OSC 22 as kitty documents
   it; `none` hides it on a terminal that says it can. */
int cn_ptrok = -1, cn_ptrset;
str cn_ptrnm;
static const char cn_ptroff[] = "\033]22;\033\\";

/* The terminal answered OSC 22's `?none`. */
void cn_gotptr(int yes)
{
	if (cn_ptrok != yes)
		lg(HIBR_LDBG, "console: the terminal %s hide its pointer",
		   yes ? "can" : "cannot");
	cn_ptrok = yes;
}

/* Ask for a pointer shape, `none` to hide it, empty for the terminal's own;
   nothing is sent for the shape already in force. */
void cn_pointer(const char *nm)
{
	str o;

	if ((cn_ptrnm.p ? !strcmp(cn_ptrnm.p, nm) : !*nm) || cn_fd < 0)
		return;
	cn_ptrnm.n = 0;
	s_cat(&cn_ptrnm, nm);
	s_init(&o);
	s_cat(&o, "\033]22;");
	s_cat(&o, nm);
	s_cat(&o, "\033\\");
	cn_wr(cn_fd, o.p, o.n);
	s_free(&o);
	cn_ptrset = *nm != 0;
}

/* Put the terminal back before a fatal signal is allowed to finish us -- in
   the process that took it, and nowhere else. */
void cn_onfatal(int n)
{
	if (cn_on && cn_fd >= 0 && getpid() == cn_pid) {
		tcsetattr(cn_fd, TCSANOW, &cn_sv);
		if (cn_kbdon)
			cn_wr(cn_fd, cn_kbdpop, sizeof cn_kbdpop - 1);
		cn_kbdon = 0;
		if (cn_ptrset)
			cn_wr(cn_fd, cn_ptroff, sizeof cn_ptroff - 1);
		cn_ptrset = 0;
		cn_wr(cn_fd, cn_leave, sizeof cn_leave - 1);
		cn_on = 0;
	}
	cn_fatal = n;
	signal(n, SIG_DFL);
	raise(n);
}

/* Say whether the screen is currently held. */
int cn_isopen(void)
{
	return cn_on;
}

/* Report and clear the pending resize flag, with none of cn_reassert's
   side effects -- a caller that wants to debounce a burst of resizes into
   one redraw asks this every tick and acts only once it goes quiet, rather
   than paying cn_reassert's clear-and-repaint on every intermediate size. */
int cn_pending(void)
{
	int r = cn_winch;

	cn_winch = 0;
	return r;
}

/* Assert terminal modes again and invalidate the front buffer, so the next
   flush repaints everything.  Unconditional: the caller has already decided
   a resize happened and it is time to act on it, not asked whether one is
   still pending.

   A resize also says the terminal on the other end may not be the one the
   screen was opened on: a session reattached from somewhere else arrives
   as a SIGWINCH on a terminal that has never seen the alternate screen,
   the hidden cursor or the mouse mode.  So every resize asserts them
   again and repaints everything, which costs one full frame on an event
   that is rare anyway. */
void cn_reassert(void)
{
	if (!cn_on)
		return;
	cn_wr(cn_fd, cn_enter, sizeof cn_enter - 1);
	if (cn_mousemode)
		cn_mouseon(cn_mousemode);
	cn_askcolours();
	cn_inval();
	lg(HIBR_LDBG, "resized: terminal modes asserted again");
}

/* Report and clear the pending resize flag, asserting terminal modes and
   invalidating the buffer when one was pending.  What a caller that redraws
   on every resize, rather than debouncing a burst of them, uses. */
int cn_resized(void)
{
	int r = cn_pending();

	if (r)
		cn_reassert();
	return r;
}

/* Let ctrl-c and its kind raise signals, or hand them over as keys. */
void cn_signals(int on)
{
	struct termios r;

	cn_isig = !!on;
	if (!cn_on || tcgetattr(cn_fd, &r) != 0)
		return;
	if (on)
		r.c_lflag |= (unsigned)ISIG;
	else
		r.c_lflag &= ~(unsigned)ISIG;
	tcsetattr(cn_fd, TCSADRAIN, &r);
}

/* Ask the terminal how large it is, falling back to a sane default.

   Some terminals answer a fresh window's TIOCGWINSZ with 0x0 for a moment,
   before they have settled on a size -- asked right at open, that reads as
   no terminal at all and falls back to the default, which then only ever
   changes on an actual resize. Retried a few times, milliseconds apart,
   before giving up: a terminal that already knows its size answers on the
   first try and never sees the wait.

   The size asked for is also checked for being merely small rather than
   exactly zero -- a terminal mid-transition (entering the alternate screen
   counts) can answer a real ioctl with a real but degenerate size, a 1x1
   or thereabouts, which passes a bare truthiness check and then builds a
   grid nothing can be seen in, rather than falling back or retrying. No
   real terminal is legitimately this small.

   The budget below (50 tries, 20ms apart -- 1s worst case) is wider than
   the 200ms this shipped with: some terminal apps settle their real size
   noticeably later than a plain local pty does, and unlike a resize this
   one has no SIGWINCH to fall back on if the window never sends one of
   its own on open -- once this gives up, only a real, later resize event
   corrects it. Widening the budget only costs time on the rare terminal
   that needs it; one that already knows its size still answers first try. */
void cn_size(int *rows, int *cols)
{
	struct winsize w;
	int i;

	for (i = 0; i < 50; i++) {
		if (cn_fd >= 0 && ioctl(cn_fd, TIOCGWINSZ, &w) == 0 &&
		    w.ws_row > 2 && w.ws_col > 2) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		if (ioctl(2, TIOCGWINSZ, &w) == 0 &&
		    w.ws_row > 2 && w.ws_col > 2) {
			*rows = w.ws_row;
			*cols = w.ws_col;
			return;
		}
		usleep(20000);
	}
	*rows = 24;
	*cols = ed_cols();
}

/* The pixel size of a cell, from whichever end of the terminal answers:
   kitty, foot and others fill ws_xpixel and ws_ypixel, and a plain pty
   leaves them zero, which is the signal that nothing here can place pixels
   at all. Asked on every call rather than cached, since a resize that keeps
   the cell count can still change the cell's own size (a font change does
   exactly that), and the call is one ioctl. */
void cn_cellpx(int *w, int *h)
{
	struct winsize ws;
	int i;

	*w = 0;
	*h = 0;
	for (i = 0; i < 2; i++) {
		int fd = i == 0 ? cn_fd : 2;

		if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) != 0)
			continue;
		if (ws.ws_row < 1 || ws.ws_col < 1)
			continue;
		if (ws.ws_xpixel < ws.ws_col || ws.ws_ypixel < ws.ws_row)
			continue;
		*w = ws.ws_xpixel / ws.ws_col;
		*h = ws.ws_ypixel / ws.ws_row;
		return;
	}
}

/* What a kind of pixel drawing is called, for a log line and `console gfx`. */
const char *cn_gfxname(int k)
{
	if (k == CN_GFX_SIXEL)
		return "sixel";
	if (k == CN_GFX_KITTY)
		return "kitty";
	return "none";
}

/* Whether this terminal takes a picture as pixels, and how.

   Two things have to be true: it must say what a cell measures in pixels
   (ws_xpixel), since sixel paints 1:1 and a wrong cell size spills the
   bitmap into its neighbours -- the kitty protocol scales to a rectangle of
   cells itself and does not need it, but every caller sizes its bitmap from
   it, so it is asked for either way; and it must understand one of the two
   protocols. The second is taken from what the terminal calls itself rather
   than from a probe, which would mean reading the terminal's reply out of
   the same stream the key decoder owns, with a keypress possibly racing it:
   that stream already has a trap record of its own (the Alt/Escape window),
   and a name plus a setting costs nobody a lost keystroke.

   Which protocol is not a matter of taste. kitty has never drawn a sixel and
   says so; its own protocol is the only way to put pixels in it, and this
   file said sixel for it from 0.99.72 to 0.99.75, so every picture on it was
   a blank rectangle. WezTerm and konsole do both, and the kitty protocol is
   the better of the two there: true colour rather than a palette, and a
   picture the terminal keeps rather than one repainted. foot, mlterm, yaft,
   contour, iTerm2 and mintty do sixel only. HIBR_GFX says outright:
   kitty, sixel, or off. */
/* Whether the protocol cn_gfx names is *known* rather than guessed. A
   name list is enough to decide how to draw text; it is not enough to
   commit a bitmap, and that difference is the whole of Gitea #249.

   A held session's TERM is the one the shell that ran `hold new` had,
   frozen for the session's life -- so a desktop started under kitty and
   resumed on a plain xterm still says `xterm-kitty`, the name list reads
   "kitty" out of it, and every picture placed before the real terminal's
   answer arrives is a payload that terminal drops. The cells are claimed
   by the region all the same and skipped in the diff, so what is left is
   a black rectangle that nothing repaints: a wallpaper keeps placing the
   same picture, so the region is never retired either.

   Measured: TERM=xterm-kitty on a terminal answering device attributes
   without kitty's query gives `gfx at once: kitty 6 13` and
   `gfx later: none 6 13`, with one kitty payload already sent in between.

   An explicit HIBR_GFX is not a guess -- somebody said so on purpose --
   and an answer of either kind is not a guess. Everything else is, and
   waits. A frame or two of half blocks at startup costs nothing; a
   wallpaper that is black until the session ends costs everything. */
int cn_gfxsure(sh *s)
{
	const char *v = hibr_get(s, "HIBR_GFX");

	if (v && (!strcmp(v, "sixel") || !strcmp(v, "kitty")))
		return 1;
	return cn_capkit == 1 || cn_capsix == 1;
}

int cn_gfx(sh *s)
{
	const char *v, *t;
	int w = 0, h = 0;

	if (cn_gfxk >= 0)
		return cn_gfxk;
	cn_gfxk = CN_GFX_NONE;
	v = hibr_get(s, "HIBR_GFX");
	if (v && (!strcmp(v, "off") || !strcmp(v, "none")))
		return cn_gfxk;
	cn_cellpx(&w, &h);
	if (w < 2 || h < 2) {
		/* Not remembered, deliberately: a program `hold` started has
		   no cell size at all until a client attaches and says what
		   its own terminal measures (Gitea #103), and a decision
		   cached in that moment would last the whole session -- every
		   picture blocks for ever, on a terminal that can draw them.
		   The cost of asking again is the ioctl cn_cellpx already
		   does, once per picture placed. */
		cn_gfxk = -1;
		lg(HIBR_LDBG, "gfx: the terminal does not say what a cell measures");
		return CN_GFX_NONE;
	}
	if (v && (!strcmp(v, "sixel") || !strcmp(v, "kitty"))) {
		cn_gfxk = !strcmp(v, "kitty") ? CN_GFX_KITTY : CN_GFX_SIXEL;
		lg(HIBR_LDBG, "gfx: %s, as HIBR_GFX asks; cell %dx%d",
		   cn_gfxname(cn_gfxk), w, h);
		return cn_gfxk;
	}
	/* What the terminal said about itself beats what its name suggests,
	   and kitty's own protocol is preferred where both are offered --
	   the same choice the name list made for the terminals that do both
	   (Gitea #216). An answer is only ever positive except for sixel,
	   where a device attributes reply without 4 is a real no and is
	   what stops the name list claiming it. */
	if (cn_capkit == 1 || cn_capsix == 1) {
		cn_gfxk = cn_capkit == 1 ? CN_GFX_KITTY : CN_GFX_SIXEL;
		lg(HIBR_LDBG, "gfx: %s, as the terminal answered; cell %dx%d",
		   cn_gfxname(cn_gfxk), w, h);
		return cn_gfxk;
	}
	t = hibr_get(s, "TERM");
	if (t && (strstr(t, "foot") || strstr(t, "mlterm") ||
		  strstr(t, "contour") || strstr(t, "yaft")))
		cn_gfxk = CN_GFX_SIXEL;
	if (t && (strstr(t, "kitty") || strstr(t, "ghostty") || strstr(t, "wezterm")))
		cn_gfxk = CN_GFX_KITTY;
	t = hibr_get(s, "TERM_PROGRAM");
	if (t && (!strcmp(t, "iTerm.app") || !strcmp(t, "mintty")))
		cn_gfxk = CN_GFX_SIXEL;
	if (t && (!strcmp(t, "WezTerm") || !strcmp(t, "ghostty")))
		cn_gfxk = CN_GFX_KITTY;
	if (cn_gfxk == CN_GFX_NONE && hibr_get(s, "KITTY_WINDOW_ID"))
		cn_gfxk = CN_GFX_KITTY;
	/* A name contradicted by the terminal's own answer loses: this is
	   the half a name list could never give, and the half that stops a
	   session following the terminal it was *started* from into one
	   that draws nothing at all. */
	if ((cn_gfxk == CN_GFX_SIXEL && cn_capsix == 0) ||
	    (cn_gfxk == CN_GFX_KITTY && cn_capkit == 0)) {
		lg(HIBR_LDBG, "gfx: the name says %s and the terminal says it "
			      "draws none", cn_gfxname(cn_gfxk));
		cn_gfxk = CN_GFX_NONE;
		return cn_gfxk;
	}
	/* Not remembered while the terminal has said nothing yet, for the
	   same reason an absent cell size is not: a held program asks before
	   any client exists, and an answer may still be on its way. */
	if (cn_capkit < 0 && cn_capsix < 0) {
		int k = cn_gfxk;

		cn_gfxk = -1;
		lg(HIBR_LDBG, "gfx: %s by name; cell %dx%d, nothing answered "
			      "yet", cn_gfxname(k), w, h);
		return k;
	}
	lg(HIBR_LDBG, "gfx: %s; cell %dx%d", cn_gfxname(cn_gfxk), w, h);
	return cn_gfxk;
}

/* Install the handlers that keep the terminal recoverable. */
void cn_hook(void)
{
	struct sigaction a;
	size_t i;

	memset(&a, 0, sizeof a);
	a.sa_handler = cn_onwinch;
	sigaction(SIGWINCH, &a, &cn_owin);
	a.sa_handler = cn_onfatal;
	sigaction(SIGINT, &a, &cn_oint);
	sigaction(SIGTERM, &a, &cn_oterm);
	sigaction(SIGHUP, &a, &cn_ohup);
	for (i = 0; i < sizeof cn_fatals / sizeof cn_fatals[0]; i++)
		sigaction(cn_fatals[i], &a, &cn_ofatal[i]);
}

/* Put back the handlers that were there before. */
void cn_unhook(void)
{
	size_t i;

	for (i = 0; i < sizeof cn_fatals / sizeof cn_fatals[0]; i++)
		sigaction(cn_fatals[i], &cn_ofatal[i], 0);
	sigaction(SIGWINCH, &cn_owin, 0);
	sigaction(SIGINT, &cn_oint, 0);
	sigaction(SIGTERM, &cn_oterm, 0);
	sigaction(SIGHUP, &cn_ohup, 0);
}

/* Take the terminal: raw mode, alternate screen, no cursor. */
/* A descriptor of the console's own, so a redirection on a `console` command
   cannot reach the display. The display used to draw on, and select on, the
   number stdout happens to have -- and a redirection changes what that
   number points at for as long as the command runs, so `console flush >
   /dev/null` sent a whole frame to the void and `console key 1000 >
   /dev/null` returned at once (a regular file is always ready to read).
   Both looked like the console being broken and were a shell redirection
   doing exactly what it says. Dup'ed high, out of reach of 0-9 and of a
   {var} descriptor, and close-on-exec so a restart does not inherit it. */
int cn_dupfd(int fd)
{
	int n = fcntl(fd, F_DUPFD_CLOEXEC, CN_FDBASE);

	if (n < 0) {
		n = fcntl(fd, F_DUPFD, CN_FDBASE);
		if (n >= 0)
			fcntl(n, F_SETFD, FD_CLOEXEC);
	}
	return n;
}

int cn_open(sh *s)
{
	struct termios r;
	int rows, cols, fd, mine = 0;

	(void)s;
	if (cn_on)
		return HIBR_OK;
	fd = isatty(1) ? 1 : (isatty(0) ? 0 : -1);
	if (fd < 0) {
		fd = open("/dev/tty", O_RDWR);
		if (fd < 0) {
			lg(HIBR_LERR, "console: no terminal to draw on");
			return HIBR_FAIL;
		}
		mine = 1;
	}
	cn_fd = cn_dupfd(fd);
	if (cn_fd < 0) {
		cn_fd = fd;
	} else {
		cn_own = 1;
		if (mine)
			close(fd);
	}
	if (tcgetattr(cn_fd, &cn_sv) != 0) {
		lg(HIBR_LERR, "console: %s", strerror(errno));
		return HIBR_FAIL;
	}
	r = cn_sv;
	r.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN);
	if (!cn_isig)
		r.c_lflag &= ~(unsigned)ISIG;
	r.c_iflag &= ~(unsigned)(IXON | ICRNL | INLCR | ISTRIP);
	r.c_oflag &= ~(unsigned)OPOST;
	r.c_cc[VMIN] = 0;
	r.c_cc[VTIME] = 0;
	if (tcsetattr(cn_fd, TCSADRAIN, &r) != 0) {
		lg(HIBR_LERR, "console: %s", strerror(errno));
		return HIBR_FAIL;
	}
	fflush(0);
#ifdef __APPLE__
	/* A terminal freshly opened by some Mac terminal apps stays black
	   until an actual resize, no matter how long a script waits after
	   that before drawing anything -- confirmed live: cn_size's own
	   widened retry budget made no difference, and neither did an
	   unconditional repaint once the desktop's own main loop started,
	   but a plain `sleep 1` before this point, with nothing else
	   different, fixed it outright. So this is not a size the terminal
	   answers wrong and later corrects -- something in the terminal's
	   own window setup has not finished, and entering the alternate
	   screen before it has does not queue for replay once it does.
	   One second, tested and confirmed working; not yet narrowed to
	   whether less would do. Not applied on any other platform, where
	   this has never been reported. */
	usleep(1000000);
#endif
	cn_wr(cn_fd, cn_enter, sizeof cn_enter - 1);
	cn_pid = getpid();
	cn_hook();
	cn_on = 1;
	if (cn_mousemode) {
		int m = cn_mousemode;
		cn_mousemode = 0;
		cn_mouseon(m);
	}
	cn_winch = 0;
	cn_askcolours();
	cn_size(&rows, &cols);
	lg(HIBR_LDBG, "screen open, %d rows by %d columns", rows, cols);
	cn_clear();
	return HIBR_OK;
}

/* Give the terminal back exactly as it was found -- or, in a forked child,
   only forget it: the terminal is the parent's, and a child that reset its
   modes or left the alternate screen would take the display from under the
   process still drawing on it. */
void cn_close(sh *s)
{
	if (!cn_on)
		return;
	if (getpid() != cn_pid) {
		lg(HIBR_LDBG, "screen forgotten in a forked child, left to its owner");
		if (cn_own)
			close(cn_fd);
		cn_kbdon = 0;
		cn_own = 0;
		cn_fd = -1;
		cn_on = 0;
		return;
	}
	cn_olay.on = 0;
	cn_imgclear();
	if (cn_gfx(s) == CN_GFX_KITTY) {
		str o;

		s_init(&o);
		kt_delall(&o);
		cn_wr(cn_fd, o.p, o.n);
		s_free(&o);
	}
	cn_wr(cn_fd, cn_moff, sizeof cn_moff - 1);
	if (cn_kbdon)
		cn_wr(cn_fd, cn_kbdpop, sizeof cn_kbdpop - 1);
	cn_kbdon = 0;
	if (cn_ptrset)
		cn_wr(cn_fd, cn_ptroff, sizeof cn_ptroff - 1);
	cn_ptrset = 0;
	cn_ptrnm.n = 0;
	if (cn_ptrnm.p)
		cn_ptrnm.p[0] = 0;
	cn_wr(cn_fd, cn_leave, sizeof cn_leave - 1);
	tcsetattr(cn_fd, TCSADRAIN, &cn_sv);
	cn_unhook();
	if (cn_own)
		close(cn_fd);
	cn_own = 0;
	cn_fd = -1;
	cn_on = 0;
	lg(HIBR_LDBG, "screen closed");
}

/* Ring the terminal's bell. */
void cn_bell(void)
{
	if (cn_on)
		cn_wr(cn_fd, "\a", 1);
}

/* Ask the terminal to show a notification of its own, with OSC 9; one
   that does not know it shows nothing. */
void cn_notify(const char *t)
{
	str o;

	if (!cn_on)
		return;
	s_init(&o);
	s_cat(&o, "\033]9;");
	s_cat(&o, t);
	s_ch(&o, 7);
	cn_wr(cn_fd, o.p, o.n);
	s_free(&o);
}

/* Append n bytes as base64, which is what OSC 52 carries. */
void cn_b64(str *o, const unsigned char *p, size_t n)
{
	static const char al[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned v;
	size_t i;

	for (i = 0; i + 2 < n; i += 3) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8 | p[i + 2];
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, al[v & 63]);
	}
	if (n - i == 1) {
		v = (unsigned)p[i] << 16;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, '=');
	}
}

/* Put text on the clipboard of the terminal the screen is on, with OSC 52.

   The terminal decides whether to honour it: most modern ones do, some ask
   first, and some ignore it entirely -- which is harmless, since the copy
   the desktop keeps for itself does not depend on it. Through hold it
   reaches whichever terminal is attached, which is the one being used. */
void cn_clip(const char *t)
{
	str o;

	if (!cn_on)
		return;
	s_init(&o);
	s_cat(&o, "\033]52;c;");
	cn_b64(&o, (const unsigned char *)t, strlen(t));
	s_ch(&o, 7);
	cn_wr(cn_fd, o.p, o.n);
	s_free(&o);
}
