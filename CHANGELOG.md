# Changelog

## 0.99.319

**Two variables 0.99.317 left global in the Browser and YouTube apps.** `o` in
`bw_heard` (the window events a page raised) and `l` in `yt_drawplay` (the
quality label in the player's bar) were assigned without a `local`, which
`strict vars` refuses at run time, so those lines did nothing there.
`tests/strictvars.py` found both (2319 of 2319 now); `tests/web.py` 57 and
`tests/youtube.py` 39 pass.

## 0.99.318

**The bar frame's title bar is square at its ends.** 0.99.316 drew them as
half circles (◖ ◗, or Powerline's), which round a one-row bar's bottom corners
as well as its top and made every window's title a pill. What was asked for is
a cell with only its outer top corner rounded, the bar meeting the window
square underneath -- and no font has that glyph. So the ends are the bar's own
colour to the edge until Blit draws one (blit#140), when its glyph set will
use it.

The sanitizer gate's leak pass no longer runs `541-ships` beside all.py's own
run of it: both did `make install` into the same tree, and one could catch the
other mid-write (Gitea #380, #381). `tests/run.sh` takes `RUNSKIP`, a list of
test names to leave out, for exactly that kind of second run.

## 0.99.317

**One Browser and one YouTube app for both engines** (Gitea #376). The WPE
WebKit module of the `hibr-browser` package offers the same `web` verbs as the
Chromium one, and had its own copies of both apps and both panes. They are
this tree's again, and ask the engine what it does instead of knowing which it
is: `web caps` lists one word for each thing (`dnt`, `pdf`, `config`, `tap`;
WebKit adds `popups`, `windows`, `dlasks`, `sound`...), and `mods/web` answers
it too -- Chromium's is `js images zoom ua dark lang dnt block geo notes
permit downloads pdf live config tap`. Do Not Track, Save as PDF, the program
and flags, and the frame rate and colour detail of YouTube's picture are
offered only where the engine lists them; with `tap` (Chromium) YouTube's
video goes down the tap to the media player as before, without it (WebKit) the
engine plays the page itself.

From the WebKit side the Browser (2.3) gains: a window a page opens by a click
is a tab beside it and `window.close()` closes it; one a page opens on its own
is refused and noted; a download is named on the address row with how far it
is, File > Cancel Download stops it, and with no download folder set the
desktop's own Save dialog asks where it goes; a finished one is a note that
opens the folder. YouTube (3.5) gains captions, the quality key and menu, the
history and favourites keys and a mini player in pixel mode.

An app that asks `web caps` needs this release: the catalogue entries carry it
as their minimum hibr.

## 0.99.316

**Charcoal, and the three things it needed.** A theme in the catalogue, not
shipped: a charcoal title bar that is the window's only frame, its two ends
rounded, coloured buttons on the left and the title centred, soft white text
with a bright white accent, and windows and the menu bar the wallpaper shows
through, over a picture of its own. Install it with `catalogue install
themes/charcoal` (its colours are `colours/charcoal`).

**A frame that is only a title bar.** Control Panel > Windows > Frame has
`bar`: the title bar is the window's one row of frame, its ends rounded --
◖ ◗, or Powerline's half circles in the nerd and Blit glyph sets -- and the
content runs to the window's edge. No app had to change for it: the console's
panes can now start their own coordinates inside their rectangle (`console
pane origin name row col`), so an app's row 1, column 1 is still where its
content begins and lands on the window's edge, and its clicks are counted
from there. The sides and the bottom are the app's own cells under this
frame -- a press there reaches the app, a scrollbar on the last column
included -- so the title bar's two ends are the resize grips.

**A title colour of its own.** A colour scheme may set `titlebar` and
`titleink`, the focused window's frame or title bar and the text on it. Left
out they are the accent and `selink`, which is what every scheme had before;
a dark bar and a bright accent need them apart.

**Windows and the menu bar the wallpaper shows through.** Control Panel >
Windows and Appearance > Menu Bar each have a Wallpaper Shows Through
slider (`DT_WINTRANS`, `DT_BARTRANS`, and `wintrans`/`bartrans` in a theme):
a window's face, and the bar's own colour, take the picture's colour scaled
to that percentage; a selection, a field or an open menu title drawn in
another colour stays itself, and a title bar stays opaque. It needs a
picture wallpaper drawn in pixels; anywhere else nothing changes. `console
translucent` takes a third argument for this, the colour that counts as none
while it is on. A frame measured the same with it off and at 55%, 3.9 to 4.3
ms either way at 120x40, inside the run-to-run spread. A terminal's own
setting still decides inside a terminal when it is above 0.

**Screenshots draw what the terminal drew again.** `tools/screenshot.py` had
an escape parser of its own that knew four sequences, and from 0.99.313 the
console sends the cheapest moves, erases and repeats -- so every picture it
made lost windows and text. It reads the cells from `tests/screen.py`'s
model now, the one every suite asserts against, and waits for `--until` on
the screen rather than in the byte stream. No picture in the tree had been
made with it since.

## 0.99.315

**The notification icon opens a panel, not a window.** A click on the icon in
the menu bar shows the latest notes in a panel beneath it, as the speaker's
slider is: each with its time and who sent it, newest first. A click on one
runs what it was for, or opens it to read in full; Clear All empties the list,
Show All opens the whole history as a window, and Settings opens Control Panel
> Notifications. Opening it puts the notes on screen away; they stay in the
list. A right click on the icon is the same choices as a menu.

**Each sender's notes can be told what to do.** A note says pop up, be kept in
the list and be counted on the bar according to its priority, as before -- and
a right click on a note in the panel is its sender's own menu, with a tick for
each of the three, turning that one on or off for everything that sender sends,
and As Its Priority Says to give them back. Control Panel > Notifications has a
From row for each sender it has seen, with the same choices as five: auto, all,
quiet (listed and counted, never popped up), popup, off. The choices are kept
with the settings.

**The desktop's own chatter is kept out of the list.** A new List From row
(normal unless set) decides which priorities are kept in the list, beside Count
From: a volume step, a workspace switch or "Copied" -- all low -- still pop up
and are no longer listed.

0.99.314 added a Colours row to Control Panel > Pictures, and the suite that
makes one check per pane row was not told: its planned count was one short.

## 0.99.314

**Windows on another display keep their title bar after a restart.** Quit
or stop a desktop with windows on a display to the left of (or above) the
primary, start it again, and each came back drawn where it was with its
title bar, buttons and drags answering somewhere on the primary instead --
so nothing but an alt-drag on its content moved it. Two faults together:
the snapshot kept every window's place in hold's union frame without saying
which frame it was, and hold moves that frame as displays rejoin; and when
it moved, the desktop moved each window's record and not its pane, so what
was drawn and what was clicked parted company. The snapshot keeps the frame
now and a restore carries its windows across, a display not back yet keeps
its windows waiting off the screen where it will be, and a move of the frame
moves the panes with the records -- and the menu bar before any window is
drawn under where it used to be. Not yet covered: desktop icons and the
Control Strip are kept in the settings rather than the snapshot, so a plain
start with the displays in a different arrangement can still put those off.

**Colours a terminal can show** (Gitea #353, the second step). Everything
was sent in 24-bit colour, which a terminal without it draws wrongly or not
at all -- the Linux console, GNU screen, Terminal.app before macOS 26. Each
display is now asked what it takes (XTGETTCAP `RGB` and `colors`, at attach,
beside the other questions), with `COLORTERM` and the terminal's name as the
fallbacks, and is sent the nearest colour it can show: the 256-colour cube or
greys, or the standard sixteen as the classic codes. A held session does this
per display, so a laptop's own console and a kitty window attached to the same
desktop each get what they draw. `HIBR_COLOURS=256` (or 16, 8, 24) says it
outright for one display; Control Panel > Pictures > Colours sets it for every
display at once.

**A run of one glyph is sent as a count** on a terminal that repeats a
character (REP, asked the same way): a window's border, a rule, a fill.

A held program that drew before hold's emulator had said what it is used to
keep the colours of that first guess for the life of the screen; a change of
depth now repaints.

`console consumed` counts what was typed and no longer the terminal's answers
to the display's own questions, which put a test's wait for a key's frame
ahead by every answer -- five more of them in this release, enough to make a
dragged window's steps read as drawn before they were.

## 0.99.313

**Less sent to every display** (Gitea #353, the first step). A held desktop
is drawn to each attached terminal by hold, and a window scrolling text cost
one absolute cursor move before almost every changed cell -- 37% of the bytes.
Each move is now the cheapest of an absolute one and a relative one (a count
down, forward or back, a column, a carriage return), a short gap of cells
already right is drawn over when that is shorter, and a run of blank cells is
one erase -- to the end of the line, or a count of characters -- wherever the
terminal gives the erased cells the same colour: blanks with no colour of
their own anywhere, and any blank on a terminal that says its erase paints the
pen's background (asked at attach, with XTGETTCAP `bce`). Measured on a
200x60 held desktop with one terminal window scrolling real text for ten
seconds: 434 kB to 274 kB, 37% less, for the same frames. The console's own
output, unheld, went 285 kB to 252 kB, and a blank screen's first paint is an
erase a row rather than every cell.

**What each display is sent is counted**: `desktop ctl stats` has
`console_bytes` and, held, `displays` -- each attached terminal's bytes and
frames, from the new `hold stats` -- and `console sent` answers the console's
own count.

The terminal emulator no longer forgets every picture on any erase: a
partial erase drops only a picture that owns the erased cells, the same rule
a write follows, so a line ended with an erase does not take the wallpaper
with it.

## 0.99.312

**A window in pixels no longer goes blank while another window overlaps it**
(Gitea #374). The Browser, the YouTube player, Boxer and anything else that
draws its window as a picture went blank the moment another window was moved
over part of it, and stayed blank until that window was moved right off.
A picture drawn over the text owns its cells, so a window above it drawing
inside it meant the picture was gone; with the overlap that was every frame,
and the picture was deleted before it was ever sent.

On a terminal that keeps pictures as objects -- kitty, and Blit -- a window's
picture now goes below the cells instead, the way the wallpaper does, and
whatever is over it covers just that part: another window, its shadow, a menu,
a note. Dragging a window across it sends no picture again, only the window.
The wallpaper moved further down to make room. A terminal drawing sixel has no
layers, so a sixel window behaves as before; and a shadow falling on such a
picture is drawn in the shadow's own colour rather than as a darkened picture.

## 0.99.311

**A window can hide the pointer over itself** (Gitea #373). A game that
draws its own cursor -- Boxer's DOS games -- showed two arrows, the game's and
the terminal's. An app asks for the terminal's to be hidden with
`dt_pointer "$id" none`, and the desktop hides it while the pointer is over
that window's content, the window has focus, no menu is open and hovering is
on; on the frame, the bar, a menu or anywhere else it comes back, and it is
always given back when the desktop closes or crashes.

This needs a terminal that can hide its pointer, and almost none can: the
standard way to set a pointer shape, OSC 22, has no hidden shape in kitty,
foot or Ghostty. hibr asks each display's terminal at attach (OSC 22
`?none`), and hold sends `none` only to one that said it can, so every other
display keeps its pointer. Blit can, since it draws its own; it needs the
change filed as blit#138 before Boxer shows one arrow there.

New underneath: `console pointer hide|show|<shape>` and `console pointer
can`, `hold pointers`, and the terminal emulator keeps the shape a program
asks for (interface version 10). Shapes other than `none` -- a text cursor,
a crosshair -- pass through the same way to terminals that know them.

## 0.99.310

**Hover on Auto works on Blit.** Blit starts every session with
`HIBR_GFX=kitty`, which says which picture protocol to use, and a display
attaching to a held desktop took that as the whole answer and asked its
terminal nothing more. So whether it reports the mouse in pixels, whether it
speaks the kitty keyboard protocol and whether it copies a rectangle of cells
were never asked, and each came back no. A Blit display therefore read as one
that does not report pixels, and Control Panel > Mouse > Hover on Auto left
hovering off. `HIBR_GFX` now decides the picture protocol and nothing else;
the other three are asked whatever it says.

A display records its answers when it attaches, so a Blit display already
attached needs `desktop -r` once after updating.

**A page caught between documents no longer throws.** Before rendering a
page as cells the Browser hides its text with a style, and a zoom is a style
too; both were appended to the page's head or its root element, and a page
mid-navigation has neither yet, so the module printed a script error. Found by
the release gate, which reaches that moment once in a while under load.

## 0.99.309

**No leaks, and the gate now looks for them** (Gitea #351). Every recorded
test runs clean under LeakSanitizer, and `tests/asan.py` runs the recorded
tests a third time with leak detection on, so a new leak fails a gate instead
of waiting for someone to look. Seven were found and fixed:

- a program started with exported variables leaked its environment, once per
  command (`mkdir`, `rm` and `mv` falling through to the program showed it);
- `most` kept its buffers and `hvi` its editor state after quitting;
- `img` kept its remembered failure reasons;
- the prompt kept its powerline colour;
- an unfinished process substitution kept its record at exit.

**Fixed-size buffers out of the shell and the modules**. A number, a JSON
request or a file read is built into a growing string now, never a local
array (`s_fmt`, beside `s_cat`). The few arrays left are sized by a file
format -- a tar header, an MD5 block, deflate's tables, a cmsg -- and say so.
Two things this fixed that showed:

- `math -s 2 1e200` printed 63 characters of a 203-character answer; it
  prints the whole number now.
- `hold upgrade` carried each display's picture protocol and lost what else
  it knew about it -- the kitty keyboard, OSC 52 copy and pixel mouse. A
  display kept across an upgrade keeps all three.

## 0.99.308

**Hover highlights** (Gitea #370). Where hovering is on (Control Panel >
Mouse > Hover, Auto by default: on where a display reports the pointer in
pixels by itself), what is under the pointer says so before it is pressed: a
window's title-bar button -- close, minimise, zoom -- is drawn lighter in its
own colour, a held one lighter still as before; a menu title and an item on
the menu bar's right sit on the theme's well colour. Each goes back the moment
the pointer leaves. A highlight that comes or goes asks for one full frame, so
a window the desktop would otherwise leave alone still shows it; a pointer
that moves without changing what it is over still draws nothing.

## 0.99.307

**Power** (Gitea #365). A machine with a battery has it on the menu bar --
its level and charge, `▄ 62%`, with `↯` in front while it charges (Nerd's
battery and bolt in the nerd glyph set, words in ascii) -- read from the
kernel's own files every half minute, one read a supply. A click opens its
menu: the charge in words with the time left or to full, then Sleep,
Hibernate where the machine can, Restart… and Shut Down…, and Power
Settings…; the same three are at the foot of **Special**, as on System 7.

They act through logind, which needs no root for the person at the machine,
and only where the desktop is the machine's own -- a local session on a seat,
which a Blit login is since 0.99.305. Over SSH, or nested in another
desktop's terminal, they are dimmed: Shut Down there would turn off a computer
nobody at it asked to turn off. Restart and Shut Down ask first.

**Control Panel > Power** shows the charge, whether it is plugged in and the
battery's health -- what it holds full against what it held new -- and has
Battery on Menu Bar, Low Battery Note (5 to 25%, or off), Critical Battery
(3 to 10%) and On Critical Battery: nothing, or sleep, hibernate or shut down
after a note and a minute in which plugging in calls it off. A note raised as
the desktop starts -- already low -- now shows at once: a ticker runs after
the frame, so a note it raised waited for the next one and could expire
unseen on an idle desktop.

Not in this release: what closing the lid does, which needs logind's lid
inhibitor, and the brightness keys and slider (#366). The checks drive a
stand-in battery folder and a stand-in `loginctl`, never the real one.

## 0.99.306

**The powerline look for the prompt** (Gitea #360), off unless asked for:
`PROMPT[look]=powerline` draws each segment as a band of its own colour --
its style's, or `PROMPT[seg][bg]` -- with its text black or white, whichever
reads against it, a separator between two bands in the left one's colour over
the right one's, a thin one between two bands of one colour, and a cap at the
end; the `char` segment comes after the bands. `PROMPT[sep]` is `arrow`,
`round`, `slant` or `flat`. A segment that renders nothing is left out before
the separators are placed, so two never meet. The plain look is byte for byte
what it was.

**And glyph sets**: `PROMPT[glyphs]` decides the separators and the segments'
symbols together -- `nerd` (the powerline glyphs, and Nerd's branch and cross
for git and status), `unicode` and `blit` (`▶`; Blit's fonts differ in which
powerline glyphs they draw, so it promises none of them), `ascii` (`>` and
words: `git:`, `jobs:`) -- or `auto`, the default, which takes `HIBR_GLYPHS`
first: the desktop now sets that to its own glyph setting for every program it
starts, so a terminal window's prompt follows Control Panel > Appearance >
Glyphs. Then Blit, then `ascii` on the Linux console or a locale whose charset
is not UTF-8, then `unicode`. `examples/hibrc` and the starter `.hibrc` carry a
commented block to turn it on; `docs/prompt.md` has the rest.

Not in this release, and still on the ticket: the right-hand prompt drawn as
the mirror image, and stepping a truecolour band down to 256 or 16 colours on
a terminal without truecolour, which waits on #353's detection.

## 0.99.305

**A login on a Blit display is a local session** (Gitea #367, with Blit
0.2.7). The login screen runs on a pty there, which says no seat and no VT,
so logind registered every such login as a remote session with no seat --
and everything that is only for the person sitting at the machine refused
it: WirePlumber drove no Bluetooth audio, so a connected headset never
became a sound output; NetworkManager's "allowed for an active session"
did not apply, which is why joining Wi-Fi wanted root's password; logind's
own brightness and power need it too (#365, #366). `auth open` takes
`-e NAME=VALUE` for PAM's environment, the login screen passes `XDG_SEAT` and
`XDG_VTNR` when it is told them (`LG_SEAT`, `LG_VTNR`), and Blit's login
wrapper tells it `seat0` and the VT Blit owns. On a real VT nothing changes:
PAM works the seat out from the terminal itself.

**The login screen, easier to use:**

- **Shell no longer looks stuck.** The session inherited the login screen's
  standard error, which goes to its log, so a shell's every prompt went
  there and the screen never seemed to answer. The session's standard error
  is the terminal now.
- **A wrong name is easy to back out of.** Backspace with nothing typed goes
  back to the name, kept to be put right, as Escape always did -- and a
  refusal now says so, where it used to replace the hint that did.
- **Desktop and Shell can be clicked.**

## 0.99.304

**Hovering** (Gitea #370). The desktop hears the pointer move with no button
held, on displays where that is cheap and expected: Control Panel > Mouse >
Hover is Auto by default, which is on where a display reports the pointer in
pixels by itself -- Blit, kitty, foot -- and off where it reports cells only,
since there plain motion is the terminal's own. On and Off override it. A held
session learns that per display, from `hold pixels`, a new request: each
client says whether its terminal answered the 1016 question when it attached.

- An open menu follows the pointer, so enter takes the row hovered, and moving
  onto another title on the bar opens that menu, as every menu bar does.
- A window whose app has a `_hover` is told the cell under the pointer, in the
  coordinates it draws in, and `-1 -1` when it leaves.
- A hover that changes nothing under the pointer draws no frame, and runs the
  loop's periodic checks at most every 100 ms. Measured with sixty reports a
  second, which is what Blit sends while the pointer moves: 1.7% of a core and
  no frames, where those checks run per report cost 3.9%. A still pointer
  sends nothing and costs nothing.

## 0.99.303

**The mouse in pixels, end to end** (Gitea #370, with Blit 0.2.6 for
blit#137). A terminal that can report the pointer in pixels -- SGR-Pixels,
mode 1016, which Blit now does -- is asked to, and a program anywhere in the
stack can have the pointer more finely than a cell:

- **The console** asks for pixels when a program says `console pixels on`,
  and only once the terminal has said it reports them (a mode request, fenced
  by the device attributes, never `$TERM`) and a cell's size is known. A
  report then carries the pixel after its cell, `mouse press left 3 12
  101,53`, and `-` where pixels were asked for and cannot be had. Without
  `console pixels on` nothing changes, so no script reading four or five
  fields gets a sixth.
- **hold** gives a held program pixels whatever its displays can do: a client
  whose terminal reports them is asked to and scaled from its own cell size to
  the session's, and one that cannot has its cell turned into the middle of
  that cell.
- **The terminal emulator** tracks 1016 and answers for it, so a program in a
  terminal window or inside hold that asks for pixels gets them, the real
  pixel when the desktop has one.
- **The desktop** asks for pixels and keeps where the pointer was in
  `DT_PX`/`DT_PY` for an app that draws pixels. No callback changed.

Two bugs found on the way, both older than this work:

- A held program's questions to its terminal were never answered. hold's
  emulator answers a cursor position request, a status request and a mode
  request itself, and having no pty of its own it dropped every answer, so a
  held program asking where its cursor was waited for ever. They are written
  back to the program now.
- A hover in a terminal window reached its program as a left-button drag (a
  button of 0 where "none" is 3).

## 0.99.302

**Joining a network never joined before asks for the password it needs
too** (Gitea #369). 0.99.301 asked for it when joining a known or an open
network, and a secured network never joined still said *a password is
needed and none could be asked for*: its settings are saved through nmcli's
connection editor, which was still run the old way. The editor now runs
with `--ask` on a pty of its own and is typed its commands at each
`nmcli>` prompt -- so the network's key is on no command line, and is left
out of anything logged, since a pty echoes what is typed -- and the
administrator's password nmcli asks for is asked for in the desktop's own
window. Then the connection is brought up; a new one that will not come up
is taken away again, so a mistyped key leaves no profile behind. A saved
network whose key has changed goes the same way.

## 0.99.301

**hibr is LGPL-3.0-or-later** -- the GNU Lesser General Public License,
version 3 or any later version -- where it was MIT. `LICENSE` holds the
notice and the LGPL's text, `COPYING` the GNU General Public License it adds
its permissions to; the deb, rpm, Arch, AUR and Homebrew packages say so,
and so does every entry in the catalogue. Releases before this one stay MIT
for anyone who has them; that cannot be taken back, and nothing here
claims otherwise. The name "hibr" is a trademark of Omar Sakka, and
`TRADEMARKS` says what that means: refer to hibr and package an official
release freely, and give a fork a name of its own.

**Joining a network asks for the password it needs** (Gitea #369). Where
NetworkManager wants a password -- polkit's `auth`, as on most machines
for a person who is not an administrator -- joining said *not allowed from
this session* and no password was ever asked for. Measured on the owner's
machine: nmcli without `--ask` tells NetworkManager the request is not
interactive, and polkit then refuses it without asking any agent, the
desktop's included. Now an action a person asks for -- joining a known or
an open network, leaving, Wi-Fi on and off, Forget, Look Again -- runs
`nmcli --ask` on a pty of its own, and the password nmcli asks for appears
in a window of the desktop's, saying whose it is (often root's) and what
it is for. Only nmcli's own password prompt is answered; anything else it
asks ends the job and says so, so a Wi-Fi key's prompt could never be
given that password. A background scan still never asks.

**No Network Password**, a row in Control Panel > Network where a
password is wanted. Turned on, it installs a polkit rule -- once, with the
administrator's password, through pkexec -- letting this person join,
leave, forget and scan for networks and turn Wi-Fi on and off with none,
as laptop distributions do. The rule names this person and those five
actions alone, and turning the row off removes it.

The runner under it: `dt_ptyjob` runs a job on a pty for a program that may
ask something on its terminal, with its output kept as any job's, a
handler for its questions, and a bound; `dt_ask` takes a callback for when
it is cancelled.

The suite's stand-in for nmcli now refuses a request without `--ask` as the
real one does; 0.99.300's network code fails four checks against it, as it
failed on the owner's machine.

Still to come: joining a secured network never joined before, which goes
through nmcli's connection editor, waits on a capture of what nmcli asks
there.

**A display waiting to join is not refused after a reboot.** A terminal
started with `desktop --standby` -- as Blit starts one on each pane -- keeps
a record of itself in `~/.local/state/hibr/standby`, and refused to start
when a record of its name pointed at a live process: "a display called
blit-DP-1-p1 is already waiting", and it never joined. That folder outlives
a reboot, and a process id from before one is very often some other
process by then, so the check took a stranger for the display and refused
for ever, and the desktop listed it as a display waiting to join. A record
now says which boot and which start of its process wrote it, and one that
does not match -- or an old one that says neither -- is cleared.

## 0.99.300

**Copy, Cut and Paste in a text field are the desktop's own** (Gitea #357,
part 3). Thirty-odd fields in apps and Control Panel dialogs each carried
their own Copy, Cut and Paste, written out by hand. Now, when the window
with the focus has a field with the focus, the desktop's own editing takes
them first, the same way for every field: what is selected is copied or
cut, and a paste is typed in, its lines joined. The callbacks that only did
that are gone -- in Files' Rename, Get Info and Open With, the Browser,
Date & Time, File Types, X Screen and Network Servers settings, Sheet and
Write's own dialogs, the Open dialog, Mail's headers, the Browser's address
bar, dbase's typed lines and a Sheet cell being edited -- 450 lines in all.

**A paste no longer presses a button.** A window's field stayed the paste
target after the focus had moved from it to a button in the same window,
so a paste holding a space was typed at the button, and a dialog that
presses its button on a space, as Contacts' edit dialog does, pressed it.
A field now takes a paste only while the last drawing of its window drew
it focused. The host terminal's own paste goes the same way as the Paste
key, through one path rather than two.

## 0.99.299

**One job runner for the whole desktop** (Gitea #357). Background work had
eight runners written out by hand, each with its own files, its own poll,
and its own pace (120, 200, 300 and 500 ms): Mail, the calendars and
contacts, the Catalogue window, the mail test and server detection, the
vault's sync, Bluetooth and the network. All of them now run through
`dt_job` (`wm/jobs.hibr`), and the code that differed between them is gone.
- **Nothing polls.** A job's child exiting raises SIGCHLD, which ends the
  desktop's wait, and the next pass hands the job to its callback: measured
  at 3.5 ms after the job ended. Before, every runner asked for a frame
  every 120-500 ms while it had anything running -- and Mail's IMAP IDLE is
  a job that runs for 25 minutes, so a desktop with Mail open drew twice a
  second for as long as it was open. A safety net of one wake in five
  seconds remains while a job is live. Measured with a long job running:
  2 wakes in 10 s and 1 tick of CPU, against 20 wakes and 5 ticks at the
  old 500 ms pace.
- **`dt_jobmerge`** runs a command with its errors in its output, in order,
  for the jobs whose errors are their answer (bluetoothctl, nmcli).
- After a callback has run the desktop draws a frame, since what it changed
  is usually on screen.
- Mail, Calendar, Contacts and the Mail and Calendar panes need this release
  from the catalogue; nothing was kept for older installs.

## 0.99.298

**Joining a new Wi-Fi network works** (Gitea #363). Joining a secured
network for the first time said *Could not join ...: nmcli 1*. The desktop
saves a new network through nmcli's connection editor, sending it commands
on standard input so the password is never on a command line. On `save`,
the editor asks whether to save a connection that connects automatically
-- every new one does -- and reads the answer from that same input. It
took the next command for a no, saved nothing, and ended with status 1 and
no error. Both editor scripts now turn the question off first
(`nmcli save-confirmation no`). And a network action that fails without an
`Error:` line now says nmcli's last line rather than only its status, with
everything nmcli printed in the desktop's log. The suite's stand-in for
nmcli asks that question too now; it never did, which is why the suite
passed.

**An escaped brace in a `${...}` default parses** (Gitea #361).
`"${x:-{\}}"` was refused with *unexpected end of input*, and so was
`"${x:-a{b}"`: hibr counted every `{` inside `${...}` as opening a level.
In bash and dash only another `${` does; a bare `{` is a character, and
`$(...)` and backquotes are passed over whole. Inside double quotes, a
`:-`, `:=`, `:?` or `:+` word now keeps a backslash before a character the
string does not treat as special, as bash keeps it: `"${x:-a\zb}"` is
`a\zb`. `tests/196-brace-escape.t` compares the family with bash.

## 0.99.297

**How far the wheel scrolls is a setting.** Control Panel > Mouse >
**Wheel Scrolls**: 1, 2, 3, 5 or 8 rows a notch, in every window that
scrolls, which since 0.99.296 is all of them. 3 by default, as it always
was.

**Two helpers every app can build on, the first of #357's.** The size
review found the same background-job runner written seven times and the
same one-line dialog eight.
- `dt_job` runs work in the background, keeps what it prints, and calls
  back after a frame once it has ended -- marked done first, so a callback
  can start the next, and its files gone after. The desktop's WebDAV
  transfers run on it now.
- `dt_ask` asks a question with one line of text for an answer: a label,
  the field (stars for a secret), a line of help, OK and Cancel, and Copy,
  Cut and Paste from the desktop. The Screenshot Folder dialog is it now.

## 0.99.296

Four faults the size review found (Gitea #354).

**About and Task Manager agree on how busy the machine is.** They computed
CPU two ways and showed different numbers at the same moment: Task
Manager added up every field of `/proc/stat`, counting `guest` and
`guest_nice` twice (the kernel already includes them in `user` and `nice`)
and leaving `iowait` out of idle. Both now use `lib/sysstat.hibr`, which
counts as top(1) does, and its memory reading too; `tests/215-sysstat.t`
checks it against figures worked out by hand.

**The regex layout check runs.** `src/recheck.c` checks `include/re.h`, the
regex types hibr declares by hand because tcc cannot read glibc's
`<regex.h>`, against the real header, and the documentation said a wrong
width fails the build. It had never been compiled. `make` now compiles it
with whatever compiler on the machine can read `<regex.h>` -- the
configured one, or else `cc`, `gcc` or `clang` -- and stops on a mismatch,
which is the macOS width that once hung `match`.

**`HIBR_CONNTIMEO` is read one way.** The shell and `hold` read it
differently, and a value over an hour turned the shell's bound off. Both
use `hibr_conntimeo` now: a whole number of milliseconds, `0` or less for
no bound, at most an hour, and anything else -- `5s` -- ignored as though
unset, where the shell had read it as 5 ms.

**A notch of the wheel is the same distance everywhere.** Terminal, the
PDF Reader, dbase, Write, Mail, Mail's compose window, Stickies and the
Calendar's agenda each scrolled by a 3 of their own; they follow the
desktop's one wheel step (`DT_SCROLLWHEEL`) now. The Calendar's month view
still turns a month a notch, and X Screen passes a notch to its program.

## 0.99.295

**The Open dialog's preview counts a folder once** (Gitea #358). A
highlighted folder's "Folder, N items" was counted again on every frame
the dialog drew: on a folder of 43,793 entries, 324 ms a frame, and a
Downloads folder of 5,000 files about 37 ms, fifty times what a frame may
cost. It is counted when the folder is highlighted, as a file's first lines
already were, and again when the folder is listed again.

**The test suites leave nothing in /tmp** (Gitea #359). They had left
43,766 entries there -- their own folders and scratch files, and the
profiles of every Chromium the web suites started -- enough to make the
Open dialog's preview of `/tmp` fail five checks under the sanitizers. A
folder a suite makes is now removed when it exits, and so is every scratch
file named after its process; a program under test makes its temporary
files under a `TMPDIR` inside the suite's own scratch home, which goes with
it. `tests/all.py` fails a run that leaves a new `hibr-` entry in `/tmp`,
and names it.

## 0.99.294

**A bash prompt works as written** (Gitea #350). `\033` and every other
octal escape in `PS1` is the byte it names, so
`PS1='\[\033[1;31m\]\u@\h\[\033[0m\]:\w# '` draws in colour with the
cursor where it belongs, instead of printing `\033[1;31m`. The rest of bash's
prompt escapes are there too: `\r`, `\A`, `\D{format}`, `\l`, `\!`, `\#`
and `\V`. `\#` counts every command the shell has run; it had counted only
the ones holding a `!`.

**Tab completion quotes what it completes** (Gitea #350). A name with a
space, a `$`, an `&`, a quote or any other character the shell would read
is escaped (`cd My<Tab>` gives `cd My\ Folder/`). A word that opened a
quote is finished inside it and the quote closed once the name is whole
(`ls "My Fo<Tab>`). A word already escaped or quoted is understood before it
is completed, where it used to complete nothing. And `~/` and `~user/`
complete as the folder they name.

**The window list stays on its own display** (Gitea #352). The list of
windows at the right end of the bar was a fixed 18 columns and drew a
longer title whole, past its own edge and onto the display beside the
bar's. It is as wide as its longest title now, no wider than Control Panel
> Appearance > Menu Width or the display. A longer title is cut with an
ellipsis, as #324 already did for every other menu.

**Packages for Fedora, RHEL, openSUSE and Arch.** hibr has a signed dnf
repository ([osakka.github.io/hibr-rpm](https://osakka.github.io/hibr-rpm))
and a signed pacman repository ([osakka.github.io/hibr-arch](https://osakka.github.io/hibr-arch))
beside the apt one, each published with every release by
`packaging/rpm/` and `packaging/arch/`. Both are built from the release's
Debian package, so the three hold the same files byte for byte; only the
PAM files differ, using the system's own `login` rules. One key signs all
three. The README says how to add each.

**The build is clean under gcc's warnings as well as tcc's.** `gcc -Wall
-Wextra` found nine: three unused variables, five unused parameters and one
signed comparison. None was a bug, and all nine are gone. What is still
short of "no compromises" is #351: fixed-size buffers the coding rules
forbid, and five tests that leak.

## 0.99.293

**Desktop icons** (Gitea #349).
- **Over a picture wallpaper, an icon has no background of its own by
  default.** The picture shows behind its glyph and its name, which keep
  their colours. A selected icon keeps its highlight. Control Panel >
  Desktop > Icon Background chooses, Transparent or Solid. Over a pattern,
  or a picture drawn in half blocks, an icon is solid either way.
- **No empty cell beside an icon where Blit's glyphs fit.** The blank 0.99.290
  put after each glyph, so a Mac's fallback font can draw it whole, is left
  out with Blit's own glyph set; it showed there as a gap in the pattern.
  Where it is drawn, it is drawn as the wallpaper's own cell, the picture
  itself over a picture.

Arabic 3.19.

## 0.99.292

**A PDF reader** (Gitea #348): the `pdf` module and the PDF Reader, an
Office application in the catalogue.
- **The module** reads a document through Poppler, every library loaded at
  run time, so nothing is linked and a machine without it runs everything
  else. It renders the part of the document a window shows as one picture,
  pages laid one under the next, and the img module places it: pixels where
  the terminal takes them, half blocks or characters where it does not. It
  also answers a page's text, search matches, the text in a rectangle, the
  link at a point and the contents. Every position is in points from a
  page's top left or in pixels of the laid-out document.
- **The reader:**
  - the document is one continuous column of pages, moved by the wheel, the
    arrows, page up and down and the scroll bar;
  - Fit Width (where it opens), Fit Page, zoom in and out;
  - `/` to search, `n` and `N` through the matches, each marked on its page;
  - `o` for the contents beside the pages;
  - a click follows a link: to its page, or an address to the Browser;
  - a drag selects text, and Copy takes it, or the whole page when nothing
    is selected;
  - `g` goes to a page, and `t` shows the page as text, which is also what
    a window without the img module gets.
- **File Types:** `.pdf` files open with it.
- **Without Poppler** it says what to install: `apt install
  libpoppler-glib8`, or `brew install poppler`. The deb suggests the
  library.

Arabic 3.18.

## 0.99.291

**A Files cover no longer stays behind when its window moves or reflows**
(Gitea #346). A picture that owns its cells was retired only when the cells
under it changed, and a window's blank face landing back where a cover had
been looks unchanged. Moving a window by less than its width, a resize or a
scroll did exactly that, and kitty kept the old cover on screen as debris.
- **A pane's own pictures:** a picture placed through a pane now belongs to
  that pane's drawing. On a frame the pane was drawn, a picture it did not
  place again is deleted, and a pane that goes takes its pictures with it.
- **No new cost:** a window that is not drawn keeps its pictures for
  nothing, as before.
- **Also fixed:** an under-text picture's per-cell colours were not freed
  when it went.

## 0.99.290

**On a Mac, the desktop's icons are drawn whole** (Gitea #345). In kitty
and iTerm2 each icon lost its right side. Their fonts lack symbols such as
`⌂` `▥` `▽`, so the terminal takes them from a fallback font where they are
wider than a cell, and it lets a symbol run into the next cell only when
that cell is blank. The wallpaper's pattern was there instead. Each icon's
glyph now has a blank cell after it. A font that fits the symbol, as Blit's
on Linux does, looks as before.

**`man desktop`** (Gitea #344). The desktop has a man page beside
`hibr(1)`, built and installed the same way, covering:
- the options;
- every `desktop ctl` request;
- detaching and resuming;
- the keys needed before the menus;
- the settings, the log and the control socket.

`tests/532-desktop-man.t` reads the options out of `session.hibr` and the
requests out of `lib/ctl.hibr`, and fails when the page and the code
disagree in either direction.

## 0.99.289

**`desktop ctl stats`: what the desktop itself costs, measured the same way
on every platform** (Gitea #343). Asked to compare a Linux desktop with a
Mac one, machine-wide load says nothing, since both machines run everything
else besides. `desktop ctl stats reset` starts a window, and `desktop ctl
stats` reads it:
- the desktop's own CPU and its programs', as a percentage of a core;
- wakes, frames and events;
- how often each of the sound, network and Bluetooth layers re-read its
  server;
- each ticker's runs and milliseconds, and the frames' time.

It says what it cannot see: hold's server, and the terminal or Blit. Reset,
leave the desktop idle a minute, read, on each machine, and the two answers
compare directly.

**`times`**, bash's builtin, which hibr lacked: the shell's own user and
system time, then its children's, as bash prints them. Bound with `:=`, the
same two lines fill the slot.

## 0.99.288

**A page in the Browser no longer grows and goes jagged with the terminal's
font** (Gitea #342). Drawn as pixels, a page was laid out at 8 by 16 CSS
pixels a cell whatever the terminal's cells measured. A larger font gave the
same window fewer cells, so the page was laid out narrower, and therefore
larger, and the display then stretched its screenshot to the real cells.
- **Real pixels:** the page is laid out at the terminal's own cell, one CSS
  pixel to a screen pixel, and photographed at exactly that size, so it is
  never stretched.
- **The font changes nothing:** Browser > Zoom is what makes a page bigger.
  On a HiDPI terminal, raise Zoom.
- **Blocks are unchanged:** drawn as half blocks a cell is a character, and
  that stays at 8 by 16.

## 0.99.287

**The menu bar keeps no hand-written lists** (Gitea #341). The owner asked
whether the bar is built from what is loaded and set. It is, and these were
its two exceptions:
- **The hibr menu's shortcut letters** were a typed string, `aqdurls`,
  beside the items they belonged to. An item added or renamed without
  editing it could share a letter with an application, and then one of the
  two could not be reached. The applications are now given every letter
  the menu's own items have not taken, read from those items: the About
  above them, and the items below them, built once with only their letters
  collected. Detach keeps its `d` while dimmed, so no application's letter
  moves with whether the desktop is held.
- **The order of the items on the bar's right** was whichever registered
  first. Control Panel > Appearance > **Item Order** shows them in the order
  they read across the bar:
  - choosing one from its drop-down puts it first;
  - left and right turn the order one place round;
  - Default goes back.

  Each item's owner names it (`dt_barilabel`): Volume, Network, Bluetooth,
  Vault. An item the order does not name follows in the order it
  registered, so with nothing set the bar is as it was.

Arabic 3.17.

## 0.99.286

**Half Block shadows** (Gitea #340): half a row below and one column right,
in place of a row and two columns. A cell is about twice as tall as it is
wide, so the shadow is as thick each way, and thinner.
- **The shape:** a lower half block beside the box's top row, one column
  down its side, and upper half blocks along the row under it.
- **The setting:** Control Panel > Appearance > Shadow Shape, Half Block or
  Two Columns. Half Block is the default and needs no migration, since no
  saved file has the setting yet.
- **Everywhere:** every shadow takes it, since all of them now go through
  one helper, `dt_castshadow`: windows, menus, the bar's pop-ups, the menu
  bar and the Control Strip.
- **Nothing readable is lost:** a half block replaces its cell, so it is
  drawn only on the wallpaper, its pattern included. Over a window or any
  text the shadow dims whole cells, as before. Over a picture wallpaper the
  other half stays the picture. The ASCII glyph set keeps Two Columns.
- **The console:** `console darken -t|-b` darkens one half of each cell,
  working out what each half shows: a plain cell's background, a half
  block's own two colours, or the picture under the text.

## 0.99.285

**The desktop's watchers are one program each** (Gitea #339). Sound,
network and Bluetooth each ran their watcher through shell plumbing:
- a `setsid sh -c`;
- a second `sh` looping `cut` and `sleep 5` over the desktop's `/proc`
  entry, so a watcher died with the desktop;
- for PipeWire, `pw-mon | grep`;
- for Bluetooth, `sleep 2147483647 | bluetoothctl | grep`.

That was three `sh`, two `grep` and three `sleep` under every desktop.
Now each watcher is `pactl`, `pw-mon`, `nmcli` or `bluetoothctl` alone,
straight under the desktop:
- **On a pty of its own:** `pty spawn -x`, close-on-exec, so the desktop is
  the only process holding it. However the desktop ends, `kill -9`
  included, the kernel hangs the program up.
- **No `sleep |`:** its input is the pty, which never ends.
- **Line by line:** its output is a terminal, so it comes a line at a time.
- **Filtered in hibr:** `dt_watchlines` splits the lines, keeps a prompt
  with no newline for later rather than waiting on it, and takes a
  terminal's escapes off before the desktop matches what changed.

The `pty` module gains `spawn -x` and `pty fd`. `desktop ctl state` names
each watcher's `pty` where it named a process group. Pairing a Bluetooth
device no longer goes through `env` either.

**The volume slider ends in a blank row**, as every drop-down does. The
track keeps its ten rows.

## 0.99.284

**Every frame lands whole: synchronized output** (Gitea #338).
- **The console** brackets every frame that sends anything in
  `CSI ?2026h` ... `CSI ?2026l`: the copies, the pictures, the cells and the
  cursor, all inside. A terminal that knows mode 2026 (Blit, kitty, and
  most current ones) presents the frame at once however its bytes are split
  on the way, so a drag cannot be seen half drawn. One that does not ignores
  the pair. An empty flush still sends nothing, and a visible cursor that
  has not moved is no longer placed again on every flush (7 bytes each).
- **hold** brackets every frame it sends a client the same way, and honours
  the program's own pair: it renders from its emulator's live grid, so a
  frame larger than one read of the pty -- any full repaint -- used to reach
  every client half drawn. A render now waits for the frame to close, or
  for the emulator's one-second guard (`tm_api` 8's `syncleft`), sleeping
  exactly that long rather than polling.
- **hibr's emulator**, as a relay, keeps no frozen copy of the grid during a
  frame, since nothing reads it then. That copy would otherwise have been a
  whole grid, 460 kB at 232x71, made and thrown away on every desktop frame.
  A terminal window still freezes on a program's own pair, as before.

A held session gets hold's half of this only once its server is new:
Restart Desktop upgrades it.

## 0.99.283

**Solid window moves copy rectangles where the terminal can** (Gitea #337,
blit#120). This is for Drag Windows: Solid. The default, Outline, is
unchanged.
- **The console:** when a window moves whole, the console sends it as one
  DECCRA (CSI ... $ v). It copies the cells in both of its buffers exactly
  as the terminal will, so the frame after sends only what the copy did not
  already put there. Only cells where the copy cut a wide glyph are sent
  again. `console copy` and `console copied` are the verbs.
- **hibr's emulator** applies DECCRA itself, blanking a wide glyph cut by an
  edge as Blit does. It remembers the copies for hold (`tm_api` 7), and
  answers XTVERSION `hibr-term 0.25 deccra`.
- **hold** replays each copy to a client whose own terminal said it copies
  (feature 28, sent as `HD_COPY`), clipped to that client's view, and diffs
  as always for one that did not.
- **The desktop** leaves a moved window undrawn when the copy has drawn it
  and its app says nothing changed. The wallpaper paints the strip it left.
  A translucent terminal is drawn again, since its colours depend on what is
  behind it.

Whether to copy is decided by what will *apply* the copy, never by the
terminal's name:
- one of hibr's own emulators, by its XTVERSION answer;
- a hold server, by `HIBR_HOLDCOPY` in its program's environment, since
  nothing inside hold answers the program;
- any other terminal, by feature 28.

A program under a hold server older than this one is never told, so it
never copies, even when Blit's 28 is relayed to it. That old server's
emulator would ignore the copy, and that was the false positive the Blit
side's audit warned about. A control run against 0.99.282's hold confirms
that no copy is sent.

Moving a window whose cells each have their own colour cost 194 bytes
against 2,140. A three-step solid drag cost 2.2 kB against 14 kB, with the
window drawn twice instead of five times.

**Smaller things in the bar's pop-ups.**
- The Bluetooth and Network pop-ups end with a blank row, the way a menu
  does.
- The volume slider no longer has a speaker at its foot. Mute is `m`, or the
  right click's menu.
- The volume slider is a column narrower.

## 0.99.282

**Blit's own glyphs** (Gitea #330). Control Panel > Appearance > Glyphs
gains **blit**, for Blit's Retro and Modern fonts. It is the nerd set,
since those fonts carry the whole Nerd catalogue, with Blit's originals for
hibr over it. The hibr menu is Blit's colour pen (U+100001), Bluetooth is
Blit's own glyph (U+100006) in place of the runic letter, and the Wi-Fi in
use is one of Blit's four signal levels (U+100002 to U+100005), chosen from
the signal as the bars were, the icon and its bars in one cell. Each is one
column wide, as Blit specifies and as hibr measures it. The notification
icon is the Nerd Font's own for the chosen style in the nerd and blit sets.
The hibr menu's mark has a name now (`GL[hibr]`), still U+270E in every
other set. Blit's icons are private to its fonts, so the set is chosen, like
nerd, and never detected.

**Covers can be turned off** (Gitea #336). Control Panel > Files > **Use
Covers**, on by default: off, the icon view draws every folder's glyph and
the 3-row tile, whatever pictures the folders hold. Files 1.7, Files pane
1.3.

## 0.99.281

**A folder can show its own picture in Files** (Gitea #336). In the icon
view, on a terminal that draws pixels, a bundle shows its `.cover.png`, or
failing that its `cover.png`, as its icon. An ordinary folder shows its
`.cover.png` only, so a stray `cover.png` is not taken for a cover. Boxer's
games already carry one, so Files is now a shelf of them. A listing with
any cover has tiles 7 rows high, the picture 5 rows by 10 columns in its own
shape, or keeps the 3-row tile with the picture in the glyph's place:
Control Panel > Files > **Cover Size**, Large (the default) or Small.
Folders are checked once per listing, one file test each, and only in the
icon view. A folder without a cover, a picture that does not decode, and a
terminal without pixels keep the glyph. Files 1.6, Files pane 1.2.

## 0.99.280

**Windows move and resize by their outline** (the owner: "moving windows is
a nightmare"). Dragging a window used to redraw the whole desktop and send
the window's whole area at every step. On the owner's own settings (a
232x71 screen, a 3840x2160 picture wallpaper in pixels, translucent
terminals, Redraw Skip 3), that was about 18 ms of CPU and 7.8 kB a drawn
frame, and only one step in four was drawn. Now the window stays where it is
while the mouse is held, and its **outline** follows, with the window's title
in its top edge. The console composites it at flush (`console outline`), so
a step draws no frame: it moves only the outline's edge and pauses the
desktop's periodic checks until release. Let go, the window goes there and
draws once. Escape puts it back. A corner resize works the same way, so a
terminal reflows its program once instead of at every step.

On the owner's settings, a 30-step drag of a 38x144 terminal was 9 frames
and 150 to 170 ms of CPU. It is now every step shown, 2 frames and 60 to
70 ms. Control Panel > Mouse > **Drag Windows** is Outline by default, or
Solid for the old way. Redraw Skip does not apply to an outline, which has no
frame to skip.

**A colour change sends only the colour that changed.** The console and
hold's renderer each sent a full reset, the attributes and both colours for
every pen change. A cell over a translucent window or a picture, which
differs from the one before it only in its background, paid for all of it.
They send only what changed now when the attributes are the same. A solid
drag on the owner's settings went 70 kB to 52 kB, and every frame benefits.
`tests/screen.py` resolves a pen as a terminal does, rather than keeping the
escape's text.

## 0.99.279

**File Types knows a kind of folder** (Gitea #318). Control Panel > File
Types said `.boxer` opened in hvi, because it asked what opens a *file*
called x.boxer. Boxer opens that kind of folder and not a file, so it was
left out, and the text editor, which takes any file, came first.
`dt_openers` takes `dir` now, to ask about a kind of folder with none on
disk. The pane asks that way for every extension an app claims as a
bundle, writes the row as Files writes a folder (`.boxer/  Boxer`), and
Open With offers the apps that open that kind of folder.

**The Browser's errors reach the log** (Gitea #327). The Browser sent the
web module's error output to /dev/null on every call that matters: opening
Chromium, its settings, loading and reloading a page, rendering and drawing
it. So the messages 0.99.275 added could never reach `desktop.log` from the
Browser, which is why the owner's log said nothing. They go to the log
now. A message the module repeats, such as `the browser is not running`
from a render on every frame, is written once and then once a minute with
a count (`wb_err`). A navigation Chromium aborts (a download, or one
replaced by the next) is no longer a warning.

## 0.99.278

**Every line in desktop.log says when it was written** (Gitea #329). The
shell gains `set -o logtime` (also `shopt -s logtime`), which puts the local
time in front of each of its own diagnostics, as RFC 3339:
`2026-10-21T09:19:34+01:00 hibr: x: command not found`. Subshells and `$(…)`
keep it, and agent mode's JSON lines are not stamped. The desktop turns it
on where its standard error becomes the log. Its own `desktop:` lines go
through one helper (`dt_log`) that writes the same stamp. Each start now
writes `desktop: started: hibr <version>, pid <pid>, session <name>`, so a
log read later says which build wrote which part. A program the desktop
starts that writes to its standard error itself is still unstamped, but its
lines fall between stamped ones.

## 0.99.277

**`/` searches the hibr menu** (Gitea #325). With the hibr menu or one of
its submenus open, `/` opens a search on the menu's own bottom row, and the
menu becomes what matches. The search covers everything on the hibr menu:
every application in every folder, the desktop's own apps and its actions.
Each result names the folder it came from (`Calculator — Accessories`).
Names that begin with what was typed come first. Enter or a click opens the
highlighted result, and escape goes back to the hibr menu. While it is open,
every letter is text, so `q` looks for Quit instead of choosing it. Nothing
is drawn until `/` is pressed, as with every other search in the desktop.
From the keyboard alone, it is F10 then `/`.

## 0.99.276

**A menu stays on its own display, and no wider than Menu Width** (Gitea
#324). A menu dropped from the bar was kept inside the whole screen, not
the display the bar is on. So the Window menu, at the end of a bar full of
an app's menus, opened onto the monitor to the right of it. It is kept to
the bar's display now, its shadow clipped at the edge too. A menu is also
no wider than Control Panel > Appearance > **Menu Width** (40 columns by
default, 30 to 80, or No Limit), and a row longer than that is cut with an
ellipsis rather than making the menu as wide as itself. A menu opened at a
point (a right-click, a submenu) already kept to its own display.

**The desktop's log turns over** (Gitea #328). Past 8 MB at a start it
becomes `desktop.log.1` and a new one begins, so it cannot grow for ever.
The 75 MB log on the ticket came from a desktop still running code from
before 0.99.259. That release had already stopped a
missing wallpaper being retried on every frame; it is tried once and said
so once.

## 0.99.275

**A Browser that shows nothing now says why in the log** (Gitea #327). The
owner's Browser drew nothing: an address typed and entered went nowhere,
and the start page never appeared, with not one line in desktop.log. All
three reasons it could have had were invisible:
- **Chromium's own error output** went to `/dev/null`. It goes to
  `~/.local/state/hibr/web/chromium.log` now, emptied at each start, and a
  browser that will not start is logged with the last thing it said
  ("Running as root without --no-sandbox is not supported", for one).
- **A page the browser refused to load** was a debug line. It is a
  warning now, with the browser's own reason, such as
  `net::ERR_NAME_NOT_RESOLVED`.
- **A page that never finishes loading** said nothing. It is logged once,
  after twenty seconds.

The suite passes against the owner's Chromium 155, so the cause is still
to be found on their machine. These three lines are how it will be named.

## 0.99.274

**A folder can be one thing: bundle extensions in Files** (Gitea #318). An
app registers a kind of folder the way it registers a kind of file, with
`dir` after its opener: `dt_opener boxerplay "Boxer" "boxer" app boxerplay
dir`. Files then shows `Prince of Persia.boxer` as one item, with a glyph
of its own. Enter and a double click hand its path to the app instead of
going in, and Open With lists every opener that takes it. The right-click
menu has **Show Contents**, which still goes in, since a bundle is a
folder to every other program. Verbs an app adds with `dt_openerverb`
(Boxer's Get Info, its settings for one game) follow it. An extension
nobody registered, or one whose app is not loaded, is a plain folder, as
before. Hibr-boxer is the first app to use it. The desktop's own icons are
Home, Trash and the disks, none of them a folder that could be a bundle,
so nothing there changes.

## 0.99.273

**Shortcut Key: the desktop's own shortcuts on Alt, Super or Alt, or
Super** (Gitea #326). Every shortcut the desktop used to put on alt (copy,
cut, paste, close, the workspaces, snapping, cycling, the volume) is
stored once as `mod-…`. One row at the top of Control Panel > Shortcuts
says what that means. On Super or Alt, super-c copies as well as alt-c
does, on a terminal that can send super, and alt still works on one that
cannot. Nothing has to know which terminal a key came from, so a desktop
attached from two machines works on each. Super also stands in for alt
inside the ctrl-alt shortcuts. Ctrl+Alt is not offered as a choice: the
desktop already has shortcuts there, and they would land on the same keys.
Each row shows what to press ("super-c or alt-c"), and so does every menu.
A saved shortcut still on its old alt default moves to mod once; one you
chose by hand stays exactly as it is.

**Super reaches the desktop through the kitty keyboard protocol.** A
terminal sends nothing at all for super in the old encoding.
- **The console** asks the protocol's question when it opens. A terminal
  that answers is asked to tell chords apart and given its own flags back
  at close. Every key such a terminal sends is named as before, with
  `super-` in front where super was held. Caps lock and num lock never
  change a name, a release is not reported, and escape arrives at once,
  without the 50 ms wait for an alt chord.
- **hold** passes the question to each display's terminal. A display whose
  terminal answers when it joins is given the flags the program asked for;
  one that does not answer is given none, and its keys come in the old
  form, which the console still reads.
- **Blit** has implemented its side as 0.1.585 (blit #116). Super pressed
  on its own, to open the hibr menu, needs key releases and is a later
  step.

## 0.99.272

**A desktop with Bluetooth no longer freezes a few seconds after it starts.**
0.99.271's Bluetooth watcher runs an interactive bluetoothctl. That
program reprints its `[bluetooth]# ` prompt after every line it prints,
with no newline after the prompt. The desktop drained the watcher by asking
whether anything was waiting and then reading a line. A prompt is
something waiting, and the read then waited for a newline that never came.
That froze every window until bluetoothctl next said something or went
away, then the watcher was started again and did the same. The watcher
now passes only whole event lines, through grep, the way the PipeWire
watcher always has. All three watchers' drains (sound, network and
Bluetooth) read with a bound and leave a partial line where it is. The
suite's stand-in for bluetoothctl now prints its prompt the way the real
one does. Against 0.99.271's watcher it fails at the first click on the
Bluetooth item.

**`read -t` bounds the whole read, as bash's does.** hibr's timeout covered
only the wait for the first byte. Once half a line had arrived, `read`
waited for the rest of it however short the timeout, which is what the
desktop fell into. It now times out with status 142 and keeps what it took
in the variable, and `read -t 0` afterwards says nothing is waiting. Bash
answers the same, and `tests/193-read-timeout.t` compares the two.

## 0.99.271

**Bluetooth on the menu bar, and Control Panel > Bluetooth** (Gitea #312).
An item beside the network's shows Bluetooth's sign with how many devices
are connected, or that it is off. A left click lists the devices it knows
-- connected ones ticked, with their battery -- and any in reach, to pair;
a click connects, disconnects or pairs. A right click opens the Bluetooth
menu. Pairing runs an interactive bluetoothctl on a pty with an agent, and
asks what the agent asks in a window: confirm a passkey both show, type a
PIN, or type the number shown on a keyboard; then trusts and connects.
Connecting, scanning and forgetting run in the background and say how they
went, in words. The pane has the Bluetooth System (Auto, BlueZ, None), the
adapter, Bluetooth and Visible to Others, My Devices with Forget a Device,
Other Devices with Look for Devices, and Bluetooth on Menu Bar. Built
against a stand-in for bluetoothctl whose show, devices and info are byte
for byte the owner's own bluetoothctl 5.82; its replies to actions and the
agent's questions are bluez's own words from its source, not yet captured
from a real pairing.

**A forked child no longer touches the terminal its parent's console holds**
(Gitea #320). The console's cleanup on a fatal signal -- restore the
terminal's modes, leave the alternate screen -- is installed when a display
opens and inherited by every fork, and ran in whichever process the signal
reached. A background child ended with SIGTERM was stopped by SIGTTOU for
setting the terminal's modes: the login screen's watchdog left two root
processes stopped for a session's whole life under Blit. Any other child
would have taken the display out of the alternate screen under the desktop
still drawing on it. Only the process that opened the console cleans up
now; a child takes the signal's default and leaves the terminal alone, and a
child's `console close` only forgets it. Found and reproduced by the Blit
side.

**Exactly one process is called `desktop [desktop::NAME]`** (Gitea #323).
The supervisor wore the same `[session::NAME]` as the desktop, so every
session showed two of them, which reads as two desktops; it is
`supervisor [NAME]` now, and the network and Bluetooth jobs name
themselves for what they do, as every other job already did.

**A watcher ends with its desktop, however the desktop ends.** The sound,
network and Bluetooth watchers (`pactl subscribe`, `pw-mon`, `nmcli
monitor`, an idle `bluetoothctl`) outlived a desktop killed with -9 -- until
their next event, and Bluetooth's for ever. Each runs in a process group of
its own now with a guard that ends the group within five seconds of the
desktop's pid going away, a zombie counting as gone.

**A desktop no longer spins at a whole core behind its screen saver or its
lock** (Gitea #323). The saver's wait read what windows' programs wrote and
nothing else. The sound, network and Bluetooth watchers, the control socket
and the polkit agent are all read by tickers, and the tickers did not run
there. So from the first event on any of them, every wait ended at once
until a key came. A desktop nobody was looking at -- a second stack left
in a terminal server's screen -- went round its loop 1,200 times a second
at up to 82% of a core, with tens of thousands of `wait4` calls a second.
Measured on a stand-in watcher: nothing read in two seconds of saver and a
third of a core, against every line read and 1%. The tickers run in the
saver's wait now.

**The console stops waking for a watched pipe nobody reads, and says so.**
If a pipe stays readable for two seconds and what is waiting never goes
down, the console stops waking for it. That covers end of file, which is
readable with nothing in it. The log names the descriptor. The pipe is
looked at again once a second and woken for once something has read it.
Only pipes: a terminal's pty may stay full while it is being read. This
bounds what any such mistake can cost. It costs about 3 us a wait with
three watchers, an `ioctl` and at most one `select` each.

**`desktop ctl state`** says what the desktop is waiting on: its pid,
whether the saver or the lock is up, each watcher's descriptor, process
group and command, and what the console wakes for (`console watching`, a
muted one marked). Each watcher's start and stop goes in the log.

## 0.99.270

**The network on the menu bar, and Control Panel > Network** (Gitea #311).
An item beside the speaker shows Wi-Fi's four signal bars, a cable or no
network. A left click lists the Wi-Fi networks in reach -- the one in use
first and ticked, a key on each that asks for a password -- with Wi-Fi's
switch at the top; a right click opens the network menu. A network joined
before is joined through the connection NetworkManager keeps for it, never
a new one; an open one at once; a secured one asks its password in a small
window, starred, and the password reaches NetworkManager through nmcli's
own editor on its standard input -- never on a command line, where any
user's `ps` would show it. A wrong password leaves no connection behind.
Joining runs in the background and a note says how it went. The pane has
the Network System (Auto, NetworkManager, None), the address, gateway and
name servers, Wi-Fi on or off, the networks in reach, the known ones to
Forget, and Network on Menu Bar. With no NetworkManager the bar still says
wired or wireless, from iproute2. The WebDAV pane is called **Servers**
now.

**The desktop asks for the password polkit wants.** From a session that
is not the machine's active local one, polkit answers NetworkManager's
every action with "a password first" -- the owner's own session did, so a
join failed with "Insufficient privileges" rather than asking. While a
join, a Disconnect, a Forget or Look Again runs, the desktop keeps polkit's
own text agent (`pkttyagent`) on a pty and asks in a window of its own,
naming what is wanted and whose password; the answer reaches the agent by
a builtin, never a command line. A wrong one is said as such. Opening the
list never asks: where a scan wants a password, it shows what
NetworkManager already knows. What polkit refuses outright -- Wi-Fi's
switch, from the owner's session -- is shown locked; where a connection for
everyone may not be saved but one's own may, a new network is saved as
one's own. Built against stand-ins for nmcli and pkttyagent whose output is
captured from the real ones; nmcli's editor reading its commands on stdin
is confirmed on the owner's machine, and the whole of a join through the
agent is still to be run there.

**Splitting on an IFS that is not whitespace keeps empty fields** (Gitea
#317), in `read` and in word splitting: `IFS=: read -ra a <<< ":x:y:"` is
three fields, the first empty, and `a::b` is three, as POSIX and bash have
them. hibr dropped every empty field, so a terse nmcli line with nothing
in its first field moved every field one place left. IFS whitespace still
collapses; `read`'s last variable takes the rest of the line, losing a
single trailing delimiter only when one field is left, as bash does.
`tests/201-ifs-fields.t` compares 26 cases against bash. A loop that does
nothing but split costs 0.39% more, for the one check of whether IFS holds
anything but whitespace.

## 0.99.269

**A menu row no longer shows a shortcut left over from the frame before**
(Gitea #313). The menus are rebuilt from the first index on every frame,
and a row's shortcut was the one field its builder never cleared, so a
dropdown or a context menu built where a bigger bar's Window menu had
been showed that row's `alt-...`, stuck to its own label in a menu never
widened for it -- "Move to Workspacealt". Every row now starts with none.
The title bar's own menu shows the Window menu's two shortcuts, Zoom and
Hide, on purpose: it had shown Zoom's only by that same accident.

**The Control Panel's pane list stops its scrollbar above the bottom
border** (Gitea #314). It started a row low, a leftover from when the
search sat above the list, so its last cell was the border: scrolled to
the end, the thumb went under the frame and the border answered as the
bar.

## 0.99.268

**`printf` takes a width or a precision from an argument with `*`**, as
bash does: `printf '%*s' 5 ab` pads to five, `%-*s` and a negative width
left-justify, `%.*s` cuts, a negative precision is none, and a missing
argument is 0. hibr printed the star and the letter after it -- `5s` --
with every argument after it shifted by one.

**A number argument that is not a number says so and fails**, as bash's
does: `printf '%d' 12abc` prints `12`, says `printf: 12abc: invalid
number` and returns 1, for `%d`, `%i`, `%u`, `%x`, `%o` and the floating
conversions alike. hibr printed what it could read and returned 0. Leading
blanks, `0x`, a leading quote's character code and an empty argument are
still numbers, as in bash; a trailing blank is not. `tests/194-printf-star.t`
compares both against bash.

## 0.99.267

**A left click on the menu bar's speaker opens the volume as a slider**
(Gitea #310). Beneath the speaker, as its menu opens: the level, a track to
click or drag the thumb along -- 100% at the top, and a drag past either
end goes to it -- and the speaker at its foot, which mutes. The wheel over
it and the arrow keys step the volume, page up and down four steps, home
and end go to the top and the bottom, and escape or a click anywhere else
closes it, like a menu. A right click on the speaker opens the sound menu
as a left click did before. Any bar item can have such a panel now
(`dt_baripop`) and a right click of its own (`dt_bariright`), in
`programs/desktop/WRITING.md`.

## 0.99.266

**X Screen can close a program's window when the program ends** (Gitea
#308). Each program in Control Panel > X Screen has When It Ends --
default, close or keep -- and Close When It Ends is the default for every
program, off. Ended means every process it started has gone and none of
its windows is left on the screen, so a launcher that forks and leaves
keeps its window while the program's own is up; and not in the first
three seconds, before a slow program has shown one. The xscr module's
`xscr windows ID` says how many there are: the ones it manages as the
window manager, else the shown ones the X server lists. A program list
written now has a fifth field; an older desktop reading it falls back to
the default picture for that program.

**A browser that hangs no longer takes the desktop with it.** Restoring a
Browser window whose Chromium had stopped answering waited out two
eight-second timeouts per frame, and the stop that followed wrote the
windows back, so every Restart Desktop froze the same way. A Chromium that
misses two answers in a row (or its first) is stopped now and no new one
starts for 30 seconds, so a restore costs seconds once rather than on every
frame. And Chromium ends with the
desktop: the web module asks the kernel to stop it when the process that
started it goes (`PR_SET_PDEATHSIG`, Linux), where a desktop that died
used to leave it running.

**A desktop that dies soon after putting its windows back starts without
them.** Twice within ten minutes, each time within a minute of restoring,
and the snapshot is set aside (`snap-<session>.json.skipped` in the state
folder) and a note says so, rather than restoring into the same crash for
ever. The snapshot is also written to a file of its own per writer, so a
restart and the supervisor no longer race over one name (`mv: cannot
stat`).

**A page's sound is recorded from when it plays, not from every click.**
The Browser started its capture on the first click or key, which also
meant a click on a page with no sound at all started one; it starts when
the page plays a video or audio element or makes an AudioContext.

**A missing JPEG or PNG library is said once, with what to install**
(Gitea #301). The img module logged "no libturbojpeg" on every frame that
drew a JPEG -- one desktop's log grew by a line per frame of it -- and now
says it once per message, naming the package: libturbojpeg0, libpng16-16,
or jpeg-turbo and libpng with Homebrew. The Debian package now recommends
both, so an ordinary install has them.

**A terminal's scrollbar thumb can be dragged again in a long session.**
A window's widget regions are forgotten just before its app draws now.
The terminal, About, Task Manager, Clipboard, Modules, Notifications and
the Displays pane never forgot their own, so each frame added another
frame's worth -- 18 a frame for a terminal's scrollbar -- and a press
walked all of them before finding the thumb, which in a session open for a
day was long enough to look like nothing happening. Every click in such a
window paid the same, and a column the bar had been at before a resize
still answered as the bar.

## 0.99.265

**A window can go to the top row of a display with no menu bar.** Every
move, drag, corner resize and move to another display stopped at row 1,
for the bar -- which is only on the primary display, so beside it that row
was out of reach. Only the bar's own row keeps a window off now.

**Control Panel > Appearance > Bold Menu Titles**, off: an app's own menu
titles on the bar -- File, Edit and the rest -- in bold, and nothing else.

**alt-up zooms the focused window, alt-down minimises it** (again puts a
zoomed one back). Snap Top and Bottom move to ctrl-alt-up and
ctrl-alt-down; a settings file still holding their old keys moves with
them, a key you chose stays. The Window menu shows each key beside its
item.

**The Browser has no line of key hints under the page.** The keys are in
the File and Go menus, beside each item, the page runs to the window's
bottom, and a page still loading says so at the end of the address.
**Nor has YouTube**, under its list or its player: every key is beside its
item in the menus, and the ones the menus lacked are there now -- Open
(enter), Back and On 5 Seconds (left, right), Volume Up and Down, and Next
Picture (m).

**Sound on PipeWire without its PulseAudio side.** Such a machine has no
`pactl` to answer, so the desktop fell to ALSA's raw mixer: it read 0%
while sound played, and choosing an output chose the input with it. The
desktop speaks to PipeWire itself through `wpctl` now, and watches it
with `pw-mon`. Control Panel > Sound gains **Sound System** -- Auto,
PulseAudio, PipeWire, ALSA or None, a chosen one said not to answer
rather than replaced -- and **Level on Menu Bar**, to show the speaker
without its percentage. With ALSA alone, the output and the input are
chosen apart, and the level is read from the control the card has.

**A click on a link in the Browser follows it again.** Since 0.99.263 the
first press on a page started the recording of its sound, inside the
page's own pointerdown -- and that swallowed the press about one time in
three, so the link did nothing. The recording starts a tick after the
click now. Found by the release gate, as "alt-b goes back" landing on a
blank page: the click before it had never left.

**Two desktops no longer share the Vault's error file.** Every desktop of
one person wrote the reason a Vault command failed to the same
`desk.err` in the runtime folder, so one could remove it while another
read it; the file is named after the desktop now. Found as a shell error
in a release gate, with suites running side by side.

**The sound menu opens under the bar on a display stacked below another.**
It was given row 1, which is the top display's; a bar item's menu now
opens through one helper that asks where the bar is, and
`tests/540-examples.t` refuses a menu opened at a literal row anywhere.

## 0.99.264

**Sound: a Control Panel pane, volume keys and a speaker on the menu bar**
(Gitea #306). The speaker shows the volume of the output in use, or that
it is muted; clicked, a menu with the level, Volume Up, Volume Down, Mute,
the outputs and inputs with the one in use ticked, and Sound Settings…;
the wheel over it turns the volume. `alt-=`, `alt--` and `alt-0` turn it
up, down and mute -- rows in Shortcuts. Control Panel > Sound: the server
found, the output Device, Volume and Mute, the input Device, Level and
Mute, Volume on Menu Bar, and Volume Step. PipeWire and PulseAudio both
through `pactl`, ALSA's mixer without a server; the desktop watches the
server's events, so a change made anywhere reaches the bar and an idle
desktop asks nothing. Choosing an output moves what is playing to it.
macOS (CoreAudio) is still to come.

**hibr's own sound goes to the sound server when there is one**: the media
module plays through libpulse-simple (`HIBR_MEDIA_AUDIO=pulse`) before
ALSA, so YouTube and the Browser follow the output chosen in the pane.

**`read -t` as bash has it.** A timeout of 0 now only says whether input is
waiting, where it read a whole line into `REPLY` and lost it; a fraction
of a second is a fraction, where `read -t 0.5` was cut to 0 and then
waited for ever; a timeout is status 142, and one that is not a number is
refused. And `$!` names a process substitution's child, as in bash, so a
script can end the one it started.

## 0.99.263

**The Browser plays a page's sound, through hibr's own media module**
(Gitea #305). suno.com, and every page with sound, was silent: Chromium
ran muted, and only YouTube's app, which catches its own player's stream,
had a way out. Now each tab is recorded -- the tab itself, through
Chromium's tab capture, which is the only way that reaches sound served
from another site's address, where every site keeps its media -- and its
sound plays through a player of the media module's own, one a tab, where
hibr's sound goes. A page starts making sound once it is pressed or typed
into, the browser's own rule; Control Panel > Browser > **Pages May Play
Sound On Their Own** lets it start as it loads. The recording pauses after
two seconds of silence, so a tab that played once costs nothing while
quiet. Chromium is no longer started muted, a recorded tab is kept off the
machine's own device, and a page may no longer ask for its own tab --
which the browser would now grant without asking. `web sound T
off|click|load` is the module's half, and `web take` says the tab's state
as a last word, `snd`.

## 0.99.262

**A video comes back moving after a workspace switch or the screen
saver** (Gitea #288). A window that is not drawn takes no frames, and
the player's queue of frames, once full, made room by dropping its oldest
-- which was the present. The picture's decoder then ran on, faster than
the clock, every time the sound's queue dipped, until all it held were
frames from seconds or a minute ahead: coming back, the picture stood on
the last frame drawn while the clock and the sound went on, until the
clock caught up. A frame the clock has passed is now dropped as soon as a
later one is due, drawn or not, and a fed player's picture and sound each
wait on their own queue, so neither holds the other back. A file played
with `media open` (`examples/play.hibr`) gets the first half too.

## 0.99.261

**The lock's password box is on the primary display** (Gitea #303). With
several displays it was centred on all of them together, which is
between them. It is in the middle of the primary now -- where the bar
is -- or, with Control Panel > Screen Saver > Password Box On set to
every, on each display at once, typing filling them all, so whichever
screen a person faces asks; the primary's is drawn last and keeps the
cursor.

**YouTube's mini player remembers where it was, and has a shadow and
Sticky of its own** (Gitea #304). It opens where it was last left -- its
place and size kept when it goes back to the full player or closes, and
used again while they still fit the screen -- rather than in its
display's corner each time. Its right-click menu, and Control Panel >
YouTube, carry Sticky (on every workspace, as it always was) and Shadow,
each kept; a window can do without a shadow at all now
(`DT[id]["noshadow"]`). And the Window menu's *On Every Workspace* is
called **Sticky**, its key `k`, in the menu bar and on a right click.

## 0.99.260

**Select, copy and cut in every field and editor** (Gitea #295). A
one-line field had no selection at all -- the Browser's address bar, the
Control Panel's fields and every dialog's -- so Copy and Cut took the
whole field, and nothing could be highlighted to cut.

- **A field has a selection.** Shift with an arrow, home or end draws it
  out, typing replaces it, backspace and delete remove it, and Paste goes
  over it. A press on the focused field puts the cursor there, a drag
  across the text selects it, and shift with a press extends it -- done
  once, in the window manager, by typing into each app's own key handler,
  so all thirty-four fields have it with no app changed for it.
- **Copy and Cut take the selection**, the same as everywhere else in the
  desktop; with nothing selected they copy nothing and say so, which is
  the owner's choice over the whole field. Sixteen dialogs and apps had a
  Copy and Cut of their own that took the whole field -- Files' Rename,
  Get Info and Open With, Sheet's formula bar, Database, the Browser's
  address, the Open dialog, the Date & Time and Browser settings dialogs,
  File Types, Network Servers, X Screen's, the screenshot folder and more
  -- and each takes the selection now (`dt_fieldsel`, `dt_fieldcut`).
- **What already had a selection gets Copy and Cut.** Mail's compose body
  and Diagram's source had a selection and no Copy or Cut, so Copy said
  there was nothing there; they have Copy, Cut and Select All, Diagram
  Paste too. The Browser had no Cut.
- **The Edit menu says what works.** Copy, Cut, Paste and Select All were
  dimmed in a window that had only fields, though the keys worked there;
  they are live wherever a field has focus. And a paste from the host
  terminal into such a window went nowhere -- it is typed in, as the Paste
  key's is.

## 0.99.259

**A wallpaper picture that will not draw says so once, and is not tried
again on every frame** (Gitea #301, the part found so far). The desktop
tried the picture on each frame that painted the wallpaper, logging the
failure each time, and showed the pattern with nothing on screen to say
why. One desktop's log held 135,170 lines of `cannot open
.../themes/retro_car.png` -- a theme's picture from when the package still
shipped that theme, saved as a path into the package's own folder, which
0.99.230 stopped shipping. A picture that fails now gives one note,
*Cannot show the wallpaper picture …*, and the pattern; choosing a
picture again -- in either wallpaper pane, the image viewer's Set as
Wallpaper, or a theme -- tries afresh. Why some pictures do not draw at
all is still open: a picture that fails is what is needed to find out.

## 0.99.258

**Every scrollbar can be held and dragged, and the wheel goes where it is
pointed** (Gitea #298). Reported as the Catalogue's wheel stopping at the
end of one category, and no scrollbar's thumb, vertical or horizontal,
moving when held.

- **Holding a bar.** The bars were drawing and nothing else: no region,
  no press, no drag. `dt_scrollbar` and `dt_hscrollbar` take a key now,
  and a bar drawn with one is a clickable track: a press puts the view
  where it lands and the window manager carries the drag to the release
  -- for a window with only `_click` as well, which never sees a drag
  itself -- telling the app each step as `<app>_scroll id key top`. All
  nineteen bars in the desktop have one: Catalogue (list and entry),
  Control Panel (list and pane), the Open dialog, Files, Terminal's
  scrollback, Sheet, Task Manager, the Browser (page and text), Diagram
  (both ways), About (both ways), Contacts, YouTube, Modules, the
  Clipboard, a notification's detail and the mail account test. A bar on
  a split divider makes its thumb alone live, so the rest of the column
  still moves the divider.
- **The wheel and the selection.** The Catalogue brought its view back to
  the selection on every frame, so the wheel, the page keys and now a
  held bar were pulled back on the next one -- the wheel never got past
  the category the selection was in. A list is brought to its selection
  only when the selection has moved now (`dt_scrollview`, the rule the
  Control Panel's list already kept), and the wheel moves the view, not
  the selection, in the Catalogue, Modules, the Clipboard, the Open
  dialog, Contacts and YouTube alike -- three of them moved the
  selection by three instead. Every wheel moves `DT_SCROLLWHEEL` rows.
- **Three wheels had never worked.** Contacts, Diagram and Calendar
  matched `wheelup` and `wheeldown`, where the window manager hands a
  `_wheel` `up` and `down`, so their wheels did nothing at all.
  `tests/540-examples.t` fails on that now.

Every app and system app named above is bumped, and needs this hibr: a
bar drawn with a key is refused by an older `dt_scrollbar`.

## 0.99.257

**A pattern's `?` and bracket take one character, not one byte** (Gitea
#296). `${v//?/R}` over `héllo wörld` gave thirteen R's, one for each
byte, where bash gives eleven, and `${v#??}` left half of `é` at the
front -- the same in `case`, `[[ == ]]`, file names and every trim and
substitution, against ADR 0021's rule that the shell counts characters.
`?` takes a whole UTF-8 character, a bracket compares code points (so
`[à-ü]` is a range of letters, and `[é€]` holds two characters rather
than five bytes), and a star, a search and a trim step only from one
character to the next, so nothing is cut in the middle of one.
`[:alpha:]`, `[:upper:]` and `[:lower:]` take the cased letters of
Latin, Greek, Cyrillic and Armenian -- the ones `${x^^}` already knows --
and `[:print:]`/`[:graph:]` everything from U+00A0 on; bash answers those
from its locale, which hibr does not read. `tests/192-utf8-patterns.t`
compares all of it against bash in a UTF-8 locale, and 191's grid has
`héllo wörld` in it again.

**Classes are cheaper.** Every `[[:digit:]]` test walked a table of
twelve names with a `strlen` and a `strncmp` each; it is one `memcmp` on
the name's length and first letter now. On a loop of `case` and `[[ ]]`
tests, 649.7 million instructions went to 542.4 million (-16.5%) with
the character work included; the character work alone costs 1.0% on
that loop (stepping a character means looking at its first byte), and
nothing on a loop with no patterns in it. None of the expansion fast
paths are touched.

A lone unquoted backslash as a whole substitution pattern still matches
a backslash here, as it does in `[[ ]]` and `case` in both shells: bash's
substitution alone misses it, because its quick check wraps the pattern
as `*\*` and the backslash escapes the star.

## 0.99.256

**A display that sleeps and comes back has its windows again, and they
move** (Gitea #297). Reported from a desktop with two monitors under Blit,
one stacked above the other: after a screen slept and woke, the windows
showed where they had been and could not be moved, and the only way back
was to close every one. Reproduced in a held desktop set out the way Blit
sets it out -- a primary, a second display joined and moved above it --
and three things were wrong, one after another:

- A display whose terminal hung up was dropped and nothing else: no new
  primary, no new screen size, nothing told the desktop. Only Displays >
  Detach did those. Every way a display goes now does what Detach does.
- Joined again under the same name, it came back beside everything,
  because nothing remembered where it had been -- so the windows it had
  were on no screen. hold remembers each display's place by name as it
  goes, and `desktop --join` and `--standby` ask (`hold slot`): it comes
  back where it was. A primary that comes back takes the role back,
  unless another was chosen in the meantime.
- Blit then sends its move again, `hold move ... -20 0`, and a move was
  absolute: the second one pushed everything down another twenty rows,
  opening a strip no display showed, with the windows in it -- drawn
  still on the screen above from before, so a press there found nothing.
  **A move counts from the primary display now**, which is what Blit's
  own `-20 0` meant from the start ("twenty rows above the primary"), so
  sent again it changes nothing. Blit needs no change; Control Panel >
  Displays sends the new coordinates. A script that moved displays in
  hold's absolute frame with the primary not at 0,0 needs the primary's
  place taken off.

A display that shrinks and comes back, the other way a screen can sleep,
was measured and was already fine.

And a window larger than the whole screen -- zoomed on a display since
gone -- got the same place from every drag, off the screen's top left,
and could not be moved at all. It keeps its corner on the screen now.

The hold half reaches a session only when its server is new: `hold
upgrade <session>`, or a new session. The remembered places are not
carried across an upgrade.

## 0.99.255

Three of the owner's reports, each small once found.

**YouTube keeps the screen saver away for as long as the picture moves**
(Gitea #299). The app answered "awake" from what state it believed it was
in, and a real stream can put that wrong while the picture plays on -- an
ad's length taken for the video's, a reset -- so the saver came over a
video being watched, in the app's own visible window. It asks the one
thing that cannot be mistaken now: is the frame on screen a later one
than when the desktop last asked. Not the position, which runs on past a
stream's end with no frames in it -- a twelve-second clip read 0:29 --
and would have kept the saver away from a finished video for ever. A
paused, finished or minimised video still lets the saver come, and a new
check says so for a finished one. The check that had failed the
0.99.254 gate was right that something was off and wrong about what:
the suite's history took the clip up seconds in, and under load it
reached its end inside the check's four seconds. It has a history of
its own now.

**X Screen keeps its programs across a restart, and each has a Delete on
its row** (Gitea #300). A session loads the apps before the Control
Panel's panes, and the X Screen pane emptied the list the app had just
read -- so after every new session the pane showed no programs, and the
next one added was saved over all the rest. The pane declares the list
and leaves it alone. Delete was in each program's dialog only; it is a
button on the program's own row in the pane now, as well.

**The Open dialog shows a picture as a picture** (Gitea #302). Its
preview read every file as text and called it binary only on bytes 1 to
3 -- and a line is cut at its first NUL when read, so the bytes after one
were never looked at: About Me's picker drew a PNG as rows of random
characters. A PNG or JPEG is drawn in the preview with `img draw -p`,
fitted to its shape, as the wallpaper picker does; any other file with a
NUL in its first 4 kB, or a control character other than tab, newline,
return or escape, says "Not text".

## 0.99.254

**`${s//pat/rep}` takes time in proportion to the string, not its cube**
(Gitea #294).

Each replacement tried every end of the rest of the string, longest
first, and copied that piece out before matching it: a copy and a match
per candidate end, at every position. 2 KB took 0.064 s, 8 KB 2.8 s and
16 KB 20 s. It is bash's own method now:
before each search, `*pat*` says whether anything is left to find; a
pattern that matches one length (`e`, `[0-9]`, `a?`) has one end to try;
a literal first byte is found with `memchr`; and each end is tried by
writing a NUL into one copy of the string, never by copying it out.

| 70 KB | 0.99.253 | 0.99.254 | bash 5 |
|---|---|---|---|
| `${s//e/E}` | over 60 s | 0.001 s | 0.402 s |
| `${s//[0-9]/N}` | over 60 s | 0.003 s | 0.738 s |
| `${s//$'\n'/ }` | over 60 s | 0.000 s | 0.202 s |
| `${s#zz}` | 0.105 s | 0.000 s | 0.167 s |

The anchored forms (`#`, `##`, `%`, `%%`, `/#`, `/%`) had the same copy
per candidate and get the same treatment. A pattern of many lengths with
an extended group, `${s//+([0-9])/N}`, is still quadratic -- 0.05 s for
1 KB, against bash's 2.9 s.

Rewriting it to bash's method brought two of bash's answers with it,
each a difference the old code had and no test had caught:

- **An empty pattern replaces nothing.** `${s//$sep/-}` with `sep` empty
  put a `-` between every character; it now leaves `s` alone.
- **An empty match is replaced.** `${s//*(a)/R}` skipped the positions
  where `*(a)` matches nothing, so `banana` gave `bRnRnR`; it gives
  bash's `RbRRnRRnR`, an empty match taking the whole character after it
  along -- never half of a multibyte one.

`tests/191-patsub.t` runs 347 pairs of a value and a pattern through all
ten forms and compares them against bash, with the long values, which
take 0.08 s here and over a minute on 0.99.253. Two differences it leaves out are filed as Gitea #296: `?`
and a bracket match one byte rather than one character, and a lone
unquoted backslash matches one. The bracket scan `gmatch` did inline is
`gbrk` now, shared with the new length count, so the two cannot read a
bracket differently. None of the expansion fast paths are touched.

**hibr builds on macOS again.** The X Screen module of 0.99.250 opened its
socket with `SOCK_CLOEXEC`, which macOS does not have, and sent with
`MSG_NOSIGNAL`, which it may not either, so `brew install hibr` stopped
at `xscr.so`. It sets close-on-exec with `fcntl` and sends through
`hibr_wrall`, the SIGPIPE-safe write the http, dav and web modules use.
Checked by compiling the module with both names made undeclared, which
fails against 0.99.253's copy and passes against this one; no Mac was
at hand to build it on.

## 0.99.253

**The menu bar is always on top; the window list and every dialog open
where you are working** (Gitea #293).

Three reports from a desktop with a display stacked above the primary:

- **The bar is above everything, always.** A window filling the display
  above cast its shadow on the bar, and a window dragged across the bar
  drew over it. Windows can still go anywhere; they now go *under* the
  bar. The console has a **top pane** (`console pane top name`): its
  cells are written only by drawing through it, so text, fills and
  shadows from anywhere else leave them alone, `console hit` names it
  even with a window raised over it, and a picture that owns its cells
  -- a Browser, a film or an X Screen in pixels -- is placed as the parts
  above and below it, since a bitmap cannot have a hole. A picture under
  the text, the wallpaper, crosses it whole, because the bar's own cells
  already cover it. The bar's pane is marked top when it is made. The
  screenshot chooser is the one thing allowed over it while its band is
  drawn, and its status line now goes on the bar wherever the bar is.
  **The cost was measured, not reasoned**: the check asks once a row
  whether the row is the bar's, never once a cell. A first version asked
  per cell and cost 2 ns a cell, about 35 us a frame at 232x71. Now 1000
  full-screen fills and shadows take 108 ms against 0.99.252's 112, medians
  of nine runs, so nothing measurable.
- **The window list opens under the bar.** It drew from row 1 across the
  whole screen's width, so with a display above the primary it opened up
  there. It opens at the bar's row now, inside the primary's columns, and
  a click reads the same rectangle it was drawn in.
- **A dialog opens over the window you are working in, on that window's
  display**, or in the middle of the primary when nothing has focus. Some
  thirty dialogs centred themselves on the whole screen, which with
  several displays is somewhere between them. X Screen's Add Program,
  opened from a Control Panel on another display, was out of sight. They
  go through one helper, `dt_dlgat h w [margin]`, which also shrinks a
  dialog to fit that display rather than the whole screen. That covers
  Files' Rename, Get Info and Open With, Sheet, Write, Mail's compose and
  account dialogs, Contacts, Calendar, Prayer Times, About Me, Date &
  Time, File Types, Screenshots, the Browser and X Screen settings, the
  wallpaper picker, the vault, network servers, the file dialog, About
  <App> and Task Manager's Process Details. YouTube's mini player goes to
  the corner of its own display, and About This Computer is sized to the
  primary. The calendar's event editor, 23 rows, used to start on the
  bar's own row at 80x24; it now takes the primary under the bar.

The checks for the bar had to be made able to fail first. They passed
against 0.99.252, because the fault is on frames where the bar is left
alone while windows are drawn again, and Quit's confirm box is an overlay
whose next frame draws the bar afresh. A window that keeps asking for
frames, and a screen read before quitting, are what make them fail
there (`tests/desktop.py`). `tests/console.py` and `tests/kitgfx.py`
check the console's side, including pictures cut around a top pane.

Found on the way and filed as #294: `${s//pat/rep}` grows nearly with
the cube of the string's length (8 KB 2.8 s, 16 KB 20 s; bash 8 ms for
60 KB).

The apps and panes above go to the catalogue at new versions and need
this hibr.

## 0.99.252

**The Browser: one tab without a strip, tab keys, live pages, a scroll
bar, and wide windows in pixels** (Gitea #292, #289).

Reported by the owner, five things at once:

- **A lone tab has no tab strip**: the address is the window's first row
  and the page gets the strip's row. A second tab brings the strip back.
- **Tab keys**: ctrl-t New Tab, ctrl-w Close Tab, ctrl-PageDown and
  ctrl-PageUp to the next and previous tab, shown on File and on Go (Next
  Tab, Previous Tab). They come after the desktop's own shortcuts, so a
  desktop key on one of them still wins.
- **A page that changes on its own is drawn as it changes** (#289), where
  before it was read again only on a load, a click or a key. The tab on
  show has Chromium say when the page repaints -- a screencast at 32
  pixels kept running only as that signal (`web set T live`) -- and the
  window reads it again, no more often than `DT_TERMMS`. A tab not shown
  and a window hidden have it off. A repaint a render itself causes (cells
  mode hides the page's text and shows it again) is kept quiet for 200 ms,
  or every render would ask for another. Measured: a still page settles to
  no renders at all, and a page changing four times a second renders 18
  times in four seconds.
- **A scroll bar** in the page's last column, from where the page is and
  how tall it is (`web scroll T`, read with the render it already makes),
  and a click on it scrolls the page there.
- **Garbled and empty when the window is very wide, in pixels.** Every
  picture a held program shows reaches the display only as hold's
  emulator keeps it, and it kept at most 4 MB of escapes. A Browser frame
  is sent uncompressed, so a window wider than about 160 columns crossed
  that and the picture was refused: an empty area, with the page's own
  cells around it. Measured through hold with a kitty display: a 960x640
  picture arrived, 2.5 MB, and a 2000x960 one, 7.7 MB, did not arrive at
  all. The bound is 64 MB now, a full 3840x2160 screen sent raw with
  room, and a picture past it is let go to its last chunk. It reaches a
  running session after `hold upgrade` or a new session, since the
  session keeps its own copy of the emulator.

New checks. `tests/web.py`: one tab has no strip and its address is the
first row; ctrl-t opens a second and the strip is back; ctrl-w on the last
tab closes the window; a ticking page is drawn again with nothing typed; a
long page has a scroll bar; and a click low on it scrolls there.
`tests/holdpix.py`: a picture past 4 MB reaches the held display whole,
which fails against the old bound. Browser 1.3 and Arabic 2.8 in the
catalogue.

## 0.99.251

**X Screen keeps a list of programs, each fitted to its window** (Gitea
#291).

Asked by the owner: name programs and their command lines in the Control
Panel, have each one under the menu, title its window with that name, and
size the program to its window.

- **Control Panel > X Screen** lists the programs: Add Program, and a row
  per program that opens it to change or delete. A program is a name, a
  command, a **Window** choice and a **Picture** choice. The list is
  `~/.local/share/hibr/xscreen.tsv`, one program a line.
- Each program is an entry under **Applications > X Screen**. The plain X
  Screen entry is gone: the app is hidden, and opens only for a program.
  The menu entries are a new kind in the window manager, `dt_appvia`, an
  entry that opens another app with an argument; they come off and go back
  with the app's file on a rescan, and are put back whenever the list
  changes.
- **Window: Fit** (the default, the owner's choice): the xscr module is
  the screen's window manager. The program's window is kept the hibr
  window's body in the screen's pixels, 1:1, and follows a resize; a
  dialog keeps its own size, centred; focus goes to what was mapped last
  and to the window under a click, so a program no longer needs clicking
  before it takes keys. Xvfb cannot change its size once running
  (measured: RandR's maximum is the starting size and it refuses new
  modes), so the screen starts at **Largest Screen**, 1920x1080 by
  default, 8.3 MB of memory-backed storage per window, and a window
  larger than that is the screen, scaled. **Fixed** is a screen of Fixed
  Size scaled in, as before. **Program's own** leaves the program to place
  itself, for an entry that starts a window manager: two cannot share a
  screen, and the module reads back the BadAccess a second one is
  answered with, so it knows rather than looking managed.
- The window is titled with the program's name, then `Name [its title]`
  once the program names its window.

Measured: a picture costs the same fitted or scaled, about 340 KB a frame
for a 14x48-cell window, since the console sizes every picture to the
window's pixels either way. What Fit buys is a sharp 1:1 picture, a
program laid out for the window it is in, and converting only the
window's corner of the screen.

Six new checks in `tests/xscreen.py`:
- each program is an entry under Applications > X Screen and the plain
  one is not;
- deleting a program takes it off the menu at once;
- a fitted program's window is the hibr window's body in pixels;
- that window follows a resize;
- the window is titled `Entry [entry]`;
- a program left to place itself keeps GTK's own 600x120.

X Screen 1.1 and its pane, and Arabic 2.7 (thirteen new strings, five
gone with Start With), are in the catalogue.

## 0.99.250

**X Screen: X11 programs in a window** (Gitea #290).

Asked by the owner: an application that is to graphical programs what the
Terminal is to text ones, drawn through the kitty and sixel picture path,
self-contained and light. Talked through with the owner and the advisor
first, and measured: Steam's client, Big Picture included, needs an X
display with GLX, and on a machine with no GPU GLX comes only from X.org's
own servers with Mesa in software; a small X server of our own would not
run it. So the engine is **Xvfb**, X.org's smallest server (the `xvfb`
package, now in the package's Suggests), and hibr is everything around it.

- **The xscr module**: starts Xvfb, maps the file it draws the screen into
  (`-fbdir`), and speaks the few X requests it needs over the socket itself,
  with no libX11: **DAMAGE**, so the screen is drawn only when the server
  says it changed and an idle one costs nothing; **XTEST** for keys,
  buttons, drags and the wheel; and the server's own keyboard map, so
  shifted characters type as themselves. The screen goes into the window's
  pane as pixels where the terminal can place them, as the video player's
  frames do, and as half blocks where it cannot.
- **The X Screen app**: each window is one X screen with an Xvfb of its own,
  scaled into the window with its shape kept; programs run full screen or
  under a window manager started in it. Paste types into it.
- **Control Panel > X Screen**: Screen Size for a new window, Picture
  (follow, half, pixels) and Start With, what runs when a window opens.
- **Lifetime**: the server is the shell's own child, in a session of its
  own, reaped by the module, and on Linux ended by the kernel when the
  desktop's process ends, so quitting, a crash or a kill takes the screen
  and its programs with it. Restart Desktop is an exec of the same process,
  so the server should survive it and the window reconnect; that is not yet
  checked by a suite. A screen folder a dead desktop left behind is swept
  when the next window opens.

Measured on this machine, with the desktop at 80x24:
- **Software OpenGL:** GTK 3 and OpenGL (llvmpipe, OpenGL 4.5) both read
  back from Xvfb's mapped screen; glxgears runs at about 2,000 frames a
  second in a small window and 340 full screen.
- **What the desktop pays, in half blocks:** a GTK label changing ten
  times a second is drawn at 9.9 frames a second, for 3.2% of a core.
  glxgears is held to 14.2 frames a second by `DT_TERMMS`, for 5.8%.
  Either costs **0.0%** minimised, and so does a screen with nothing
  changing.
- **In pixels**, each changed frame is a whole kitty picture, about 814 KB
  for a 640x400 screen, which a local terminal takes easily and a link
  slower than a gigabit does not. Sending only the damaged rectangles is
  the next step.

Not yet: hover with no button held, keys outside the US map, a shared
clipboard, and resizing a running screen. Steam needs i386 enabled before
it can install, and games are not realistic without a GPU.

New suite `tests/xscreen.py`, eleven checks against a real Xvfb and a real
GTK program: the module reads the screen and its size, DAMAGE reports a
change, XTEST typing reaches GTK (shifted characters too), closing ends
the server; in a desktop, the screen is drawn as pixels on a terminal that
takes them and as half blocks on one that does not, typing reaches the
program, a killed desktop leaves no Xvfb behind, and a screen changing ten
times a second is drawn about that often when shown and not at all when
minimised. That last check fails against the module re-arming DAMAGE the
moment a change arrives, which is how it was first written. The suite
skips when Xvfb or the GTK bindings are missing. Two dropdown checks for the pane in
`tests/apps.py`. X Screen 1.0 with its pane, and Arabic 2.6 (seven new
strings), in the catalogue.

Also in this release, test-only (Gitea #283): `tests/790-hold.t` waits for
each client to attach instead of a fixed 0.6 seconds. Under a loaded gate
the keys reached a client that had not attached yet, one run in six.

## 0.99.249

**A right-click menu stays on the display it was opened on** (Gitea #287).

Reported by the owner: a right-click menu was placed correctly on the
primary display and wrongly on the others. A menu that opens at a point --
a right-click, a submenu, a dropdown's choices -- was kept inside the
primary's rows, and its columns inside the whole union, so on a display
stacked above or below the primary it was pushed onto the primary, and on
one beside it of a different height it moved near the top or bottom
edge. It is kept inside the display the point is on now, with the bar's
row clear only on the primary. Bar menus still drop from the bar, which
is the primary's.

The display under a point is `dt_dispat`, split out of 0.99.248's
`dt_windisp`, so Zoom, the snaps and the menus share one rule.

One new check in `tests/desktop.py`, with an eight-row primary above a
sixteen-row display: a right-click on the lower display opens its menu at
the pointer. It fails against the release before.

## 0.99.248

**Zoom and snapping act on the display the window is on** (Gitea #286).

Asked by the owner: a Zoom should fill the display it is on, not the whole
union. It never looked at displays at all: a zoomed window filled every
display put together, and the snaps (left, right, top, bottom, centre)
always used the primary's rectangle, so a window on another display jumped
to the primary when snapped.

- Both now use the display holding the window's centre: its whole height
  on a display with no bar, and below the bar on the primary, the only
  one with a bar. With one display nothing changes.
- **Control Panel > Windows > Zoom Fills**: `display` (the default) or
  `every`, which fills every display at once as before. Snapping always
  keeps to the window's own display (the owner's choice).

Three new checks in `tests/desktop.py`, with two displays side by side and
the window on the right one: Zoom fills that display alone; with Zoom
Fills at every, both; and a snap to the left takes the left half of the
window's own display. The first and the third fail against the release
before. Windows 1.1 and Arabic 2.5 in the catalogue.

## 0.99.247

**Control Panel > Browser, part two** (Gitea #285).

The owner chose the next settings from what #276 left for later: language,
privacy, blocking, permissions, downloads and Print to PDF. Proxy and
pop-ups still wait.

- **Privacy.** *Languages*: what pages are asked for (`Accept-Language`),
  and the locale their scripts see from the first. *Do Not Track*: `DNT`
  and `Sec-GPC` on every request, and `navigator.doNotTrack` and
  `navigator.globalPrivacyControl`. *Block Addresses*: patterns never
  fetched, `*` for anything. All three per tab, as #276's were, so the
  YouTube player's tabs keep their own.
- **What a Page May Have**: Location, Camera, Microphone and
  Notifications, each refuse (the default) or allow, and Reset
  Permissions. A headless Chromium can show nobody a prompt, so there is no
  "ask"; the pane says what Allow comes to here. An allowed location is the
  place Prayer Times knows, an allowed camera or microphone finds no
  device, and an allowed notification becomes one of the desktop's notes,
  title and body.
- **Downloads**: none until Allow Downloads is on (as before), then into
  Folder, `~/Downloads` by default, each told in a note as it lands.
- **File > Save as PDF** prints the page to a file chosen in the desktop's
  save dialog.

Permissions and downloads are the whole browser's, which is what Chromium's
protocol offers, so the YouTube player shares them. The web module gains
`web set T lang|dnt|block|geo|notes`, `web permit`, `web downloads`, `web
downloaded`, `web notes` and `web pdf`.

Measured before choosing how to block and add headers: they need
Chromium's network event stream on, several messages a request, and hibr's
CPU with it on and off was the same within run-to-run noise (1.4 to 1.7 s
for three loads of a 300-image page, Chromium included). A notification
needs the runtime domain on as well, or the binding it reports through is
never there; that is turned on only for a tab whose notifications are
allowed.

Nine new checks in `tests/web.py`, each reading the effect rather than the
setting: the server's own echo of `Accept-Language`, `DNT` and `Sec-GPC`
and the page's `navigator.language`; a blocked script that never ran; a
position given when allowed and error 1 when refused; a page's
notification handed over; no file in the folder until downloads are on,
then the file and a `done` line; a file that begins `%PDF-`; and through
the Browser itself, the headers it sends and the note a download raises.
The Control Panel suite's dropdown walk covers the four permission rows.
Browser 1.2 and its pane 1.1, and Arabic 2.4 (28 new strings), are in the
catalogue.

## 0.99.246

**A wallpaper for each display, remembered by its name** (Gitea #284).

Asked by the owner: a different wallpaper on each attached display, kept
across reattaches and restarts. A display had no name worth remembering:
hold calls an unnamed one `client-<fd>`, the socket's number on that
attach.

- **A display names itself when it attaches.** `--name` still wins.
  Otherwise the name is the machine an ssh connection comes from, by its
  short host name where it resolves and its address where it does not, or
  for a terminal on this machine its terminal program. Two terminals on
  one machine get one name and share its wallpaper (the owner's choice);
  hold needs the names to differ, since the Displays pane moves, drops and
  makes primary by name, so the second attaches as `laptop#2`, and the
  wallpaper is looked up by the part before the `#`. `--standby` joins
  under the same name.
- **Control Panel > Wallpaper**, a new shipped pane: the wallpaper rows
  move here from Appearance. **Display** offers All Displays, then every
  display attached now and every one with a wallpaper saved. Choosing one
  makes Image, Mode and Picture that display's. Its first change gives it a
  wallpaper of its own, starting from what it showed, and **Delete** (or
  `d`) forgets it, so it follows All Displays again. The pattern is the
  whole desktop's and only offered under All Displays. The desktop's
  Change Wallpaper opens this pane.
- Each record is a `dt_walldisp name image mode picture` line in the
  settings file. A settings file from before means what it did: All
  Displays is the three settings there always were.

Measured, since a cost was expected: two displays in pixels showing two
different 3840x2160 pictures cost the same per frame as one picture on
both, about 65 us. `img draw` keeps a picture already placed in the same
rectangle before it looks at its one-picture cache, and keeps a record per
rectangle since #258, so a repaint decodes nothing.

Deliberately unchanged: a theme's picture, the Image Viewer's Set as
Wallpaper and the Control Strip's wallpaper module set All Displays, so a
display with a wallpaper of its own keeps it. And a desktop joined from a
terminal window inside another held session inherits that session's ssh
connection, so it is named for the machine that session came from.

New checks. `tests/holdpix.py`: two held displays, one with a wallpaper of
its own, are each sent their own picture as pixels, and its control, two
with none, are sent the same bytes. `tests/apps.py`: the pane offers a
saved display, whose Mode is its own while All Displays keeps stretch;
Delete forgets it; Appearance no longer has the rows; a display with no
`--name` is named for where it connects from, with no space in it; its
record is saved and read back; and `laptop#2` shares `laptop`'s, offered
once. Arabic 2.3 in the catalogue (two new
strings), Appearance 1.7.

## 0.99.245

**Control Panel values stay inside the window** (Gitea #282).

Reported by the owner: in Network Servers, Language, Pictures, Versions
and others, values, buttons and widgets ran off the right-hand edge. Every
value had an eleven-column slot that was padded but never cut, so a longer
one ran over the margin and the border and was clipped there:
`Automatic (hibr)`, `0.99.244 (abi 16)`, a server's address, a wallpaper's
file name. A dropdown was eleven columns too, so a long choice covered its
own caret. Plain values also started one column left of the widgets beside
them.

- A pane's value column is as wide as its widest value, eleven columns at
  least and half the pane at most, and every row in the pane shares it, so
  values and widgets line up. A label that no longer fits ends in an
  ellipsis; a value wider than half the pane does too (the owner's choice).
- A dropdown cuts a choice too long for it with an ellipsis, so its caret
  always shows. This is in the widget itself, so it holds for every app.
- Shortcuts showed an ellipsis before every key, because a shortcut's label
  was indented twice and so always read as too long. Gone, and the mirrored
  layout's shortcut labels no longer sit two columns off the others.

Two new checks in `tests/apps.py`: Language's dropdowns keep their carets
on one column inside the window with `Automatic (hibr)` whole, and a
shortcut's row has no ellipsis before its key. Both fail against the
release before.

Notifications 1.2 is in the catalogue for 0.99.244's Show On row.

## 0.99.244

**Notifications go to the primary display, or to every display** (Gitea
#280).

Asked by the owner: should notes be on the primary screen, or on all of
them, selectable? They were on neither on purpose. A note was placed
against every display put together, so "top right" was the corner of
whichever display was right-most, or bottom-most when stacked, primary or
not. The bar, the icons and the wallpaper have kept to the primary since
#238 and #258; notes had been left out.

- **Control Panel > Notifications > Show On**: primary (the default, as
  the owner chose) or every. Primary puts the stack in Position's corner
  of the primary display, below the bar. Every puts the same stack in that
  corner of each attached display, and a click on any copy acts on the
  note. The size limits are held to the display the stack is on, not to
  all of them together.

Three new checks. `tests/desktop.py`, with two displays and the primary on
the left: a note goes to the primary's corner and not to the right-hand
display, where it went before. `tests/holdpix.py`, with Show On set to
every: the note is on both displays, and a click on the second display's
copy removes it from both. Against the release before, the note was on one
display only. Arabic 2.2 in the catalogue (the row's name).

## 0.99.243

**Under the login screen, the wallpaper is pixels again and Task Manager
shows your processes as yours** (Gitea #281).

This was one bug, and it was also why Blit's wallpaper stayed in half
blocks while `hold gfx` said `kitty` and YouTube drew pixels. The desktop
reads hold's list of what each display draws with `read -r gnm gv _`, so
in a login-screen session that read was refused too. The desktop never
learned the display drew kitty, took it for `unknown`, and drew the
wallpaper in half blocks on every restart and reattach. YouTube and the
image viewer ask the console directly, so they were unaffected.

Reported by the owner: logged in through the login screen as osakka, Task
Manager > This Desktop showed every process owned by root, while `ps` and
`/proc` said osakka.

The cause was in the shell, and it reached further than the Task Manager.
`read -r k v _` is the usual way to throw a field away, and `_` normally
comes in with the environment, because a parent shell exports it. The
login screen starts a session with a cleared environment, so there `_` did
not exist, and `strict vars` refused the read as a desktop function
creating a global. The refusal fails the read. Task Manager read each
process's uid that way, got nothing, and handed back `" 0"`: read as `uid
rss`, that is uid 0, which is root, beside a memory of 0K. Thirteen other
desktop functions read into `_` and failed the same way in such a session.
This is #251's class again, which was PWD and OLDPWD.

- `_` is now one of the names the shell manages, which `strict vars` never
  takes for a new global. It is the shell's own parameter, as in bash.
- Task Manager's reading of a process's uid fails when there is no uid,
  and the scan skips that row, so a missing reading can no longer show as
  root. Task Manager 1.1.

Two new checks, and one made stricter. `tests/216-strict-underscore.t`
reads into `_` in a strict function with `_` taken out of the environment:
two lines read, against none and a refusal on the release before. A name
the script really creates is still refused. `tests/apps.py` scans This
Desktop with no `_` in the environment and finds the row is this user's,
with its memory. And `tests/holdpix.py`'s new-session check now starts the
held desktop with no `_`, as the login screen does: on 0.99.242 that is
Blit exactly, with no picture, 18 rows of half blocks and `hold gfx`
saying kitty, and on this release the picture.

## 0.99.242

**A display attached from an older hibr is sent pictures again** (Gitea
#279), and **`desktop ctl displays` says what decides a picture on each
display**.

- A client older than 0.99.235 never says what its terminal draws, and
  hold started every new client as `unknown`, which is sent no picture.
  So a machine still on an older package saw its wallpaper in half blocks
  for the whole of its attach, where it had been sent pictures before.
  That is #274's shape, never asked read as answered nothing, for a live
  client rather than a carried one. A new client now has half a second to
  say what it draws (a current one says within milliseconds) and is
  `unasked` after that, the same default a carried client has. It is not
  `unasked` from the start, because hold renders a new client before it
  reads that client's next message, so a current client on a terminal
  that draws nothing would have been sent the session's bitmap.
- `desktop ctl displays` gains each display's `gfx`, what the desktop has
  recorded that terminal draws, and `wallpaper`, `pixels` or `cells`,
  beside a top-level `console` with the session's own answer. On Blit the
  wallpaper stays in half blocks while YouTube and the image viewer draw
  pixels in the same process, and these three are what can say why.

Two new checks. `tests/holdpix.py` plays a client that speaks the old
protocol (attach and size, nothing about pictures): `hold gfx` lists it as
`unasked` and it is sent the picture. Against the release before it was
`unknown` and sent nothing. `tests/desktop.py` reads the new fields.

Also: the YouTube suite's screen saver check failed in two gates, and the
cause was the check. The saver's two seconds counted from the session's
start, and under load the search results took longer than that to arrive,
so the saver came on before anything was played, which was correct with
nothing playing. The Enter meant to play the video only dismissed the
saver, so the video never started. Reproduced 2 runs in 3 under load by
logging what the desktop asked the window: an empty state at every
deadline. The check now presses Home first, which does nothing on the
first result or dismisses a saver already showing, so Enter always plays
the video. Up was tried first and is wrong: on the first result it opens
the search, so with no saver showing Enter searched instead of playing,
which the next gate caught. 0.99.241's change for a loading video was not
this, as its entry said; it stays, on its own merits.

## 0.99.241

**A new held desktop draws its pictures as pixels** (Gitea #278).

Reported by the owner: a new session's wallpaper was always half blocks
and became pixels after a restart, and on another machine (Blit) it stayed
half blocks even across restarts, while YouTube drew in pixels and
`hold gfx` said `kitty` throughout.

The cause was in hold. Its terminal emulator passes a picture question
(kitty's `a=q`, the device attributes) on to the real terminal only once
it knows it is a relay, and it learned that at hold's first drain of what
it passes on, which came just after the program's first output had been
fed. A new desktop asks in its very first output. So the emulator answered
for itself, a terminal with no sixel, and dropped kitty's query. The
console took that for a real no and drew every picture as cells for the
life of the session. A restart inside an existing session asked after the
relay was on, which is why that worked. Restart Desktop also upgrades a
stale session's server, and a new server has a new emulator, which fits
Blit, where each restart followed an install.

hold's emulator is a relay from the moment it is made now, both for a new
session and for one resumed by `hold upgrade`. A question asked before any
display has attached reaches nobody, which leaves the console unsure
rather than wrong, and the attach asks again.

One new check in `tests/holdpix.py`: a new session started on a terminal
that draws kitty pictures sends its wallpaper as one and draws no half
blocks. Run against the release before, it sent nothing and drew 18 rows
of half blocks. The resumed path was driven by hand: a session whose
emulator was already a relay, upgraded, then a fresh console inside it
was sent its picture with the fix and nothing without it.

Also:

- **A YouTube video that is loading keeps the screen saver away too**, as
  well as one that is playing. That covers the start of a video, the next
  one in a list, and the page's player being loaded again after it gave
  up. The release gate saw the saver start under a playing video once.
  That failure has not been reproduced (the suite passed alone), so this
  is not shown to be its cause, but a deadline landing in a reload
  should not let the saver in either way. YouTube 1.6.
- Two of the desktop suite's idle CPU checks waited a fixed second after
  minimising or switching away before measuring. Under a full gate the
  desktop acted on the click late, the terminal was still drawn for part
  of the six seconds measured, and the check read 11% of a core where it
  measures 0.5% alone and under a CPU spinner per core. They wait for the
  window to be gone now.

## 0.99.240

**Control Panel > Browser** (Gitea #276).

Asked for: a Browser pane with the home page, how pages are drawn, text
only, and what Chromium's DevTools protocol lets the Browser set. Until now
the Browser kept nothing a person chose, and its only settings were
environment variables nobody sees. The first release is what the owner
picked: everything the app or the module already had, plus JavaScript,
Images, Dark Pages, Zoom and the three Clear actions.

- **Pages.** Home Page is what a new window and a new tab open at, typed
  like an address (empty is a blank page with the keyboard in the address).
  Search With is the address words are searched at; empty makes them an
  address.
- **Picture**: Follow (Control Panel > Pictures, as before), Half Blocks,
  Pixels, or **Text Only**. Text Only is the page's words wrapped to the
  window, with each link followed by its number in brackets; clicking the
  number follows the link, and the arrows, the page keys, space and the
  wheel scroll. It is read from the page's own DOM, so it works with
  JavaScript off.
- **JavaScript**, **Images**, **Ask Sites for Dark Pages**, **Page Zoom**
  and **User Agent** are told to each Browser tab, and to every open one
  when changed. Scripts and pictures already loaded go when the page loads
  again, which the pane does for them. They are per tab, so the YouTube
  player sharing the same Chromium keeps its own. Images are refused by
  resource type, which catches a picture with no file extension.
- **When Chromium Next Starts**: Program, Profile Folder and Extra Flags,
  over `HIBR_WEB_BROWSER`, `HIBR_WEB_PROFILE` and `HIBR_WEB_ARGS`. The
  YouTube player asks for these too when it starts the browser, since there
  is one browser for both.
- **Forget**: Clear Cookies and Clear Cache for the whole profile, and
  Clear History for the back and forward lists.

The web module gains `web set T js|images|dark|zoom|ua`, `web config
browser|profile|args`, `web clear cookies|cache|history` and `web mode T
text`. Eleven new checks in `tests/web.py`, against pages of its own and a
local HTTP server for the cookie. Each compares the page with the setting
against the page without it, so each fails if the setting does nothing. Two
more in the Control Panel part of `tests/apps.py` drive the pane's two
dropdowns. Browser 1.1 (with its new pane), YouTube 1.5 and Arabic 2.1 in
the catalogue.

## 0.99.239

**The screen saver asks a playing window, once, instead of being told every
frame** (Gitea #277, second part).

The owner asked whether 0.99.238 had to ask every frame, and it did more
than it needed to. The YouTube player asked its player twice a frame for
one fact: once for its clock, and once more to take or free a hold. The
hold was also state to clean up in three places: the app, `dt_del` and the
saver's own sweep for windows that had gone.

- **`<app>_awake id` replaces `dt_saverhold` and `dt_saverfree`.** The
  desktop asks it only when the saver is due, and only of windows that
  still exist. A yes puts the idle clock back a whole period, so the lock,
  whose timer starts with the saver, waits too. Nothing is kept, so nothing
  can be left behind.
- **A playing video costs nothing extra.** The player is asked once per
  idle period rather than once per frame. The answer is also fresher than
  before: it is what the player says at the moment it matters, not what it
  said a frame earlier.
- `dt_saverhold` and `dt_saverfree` are gone rather than kept unused.
  WRITING.md and ARCHITECTURE.md describe the callback, and
  `tests/540-examples.t` checks its arity.

The same six checks pass unchanged: four in the desktop suite, now driving a
window that answers `_awake`, and two in the YouTube suite. YouTube 1.4 in
the catalogue.

## 0.99.238

**A playing video keeps the screen saver and the lock away** (Gitea #277).

Asked for: an option in the YouTube player to stop the screen saver while a
video plays. Nothing but a key or a click held the saver off, so a video
longer than the idle time was covered halfway through, and then locked.

- **Any window can hold the saver off**: `dt_saverhold id` and
  `dt_saverfree id`. A hold is the window's own, so two players hold for
  themselves, and closing the window frees it -- `dt_del` does, and a hold
  whose window has gone is dropped -- so a forgotten hold cannot keep the
  screen lit for ever. While any window holds, the desktop counts as in
  use. **The lock is held off too**, as the owner chose: its own timer
  starts with the saver, so a saver that never starts never locks.
- **The YouTube player holds while a video is playing** and lets go when
  it is paused, stopped, finished or minimised. It asks every frame from
  what its player says now, rather than setting and clearing at each change,
  so no path through play, pause and the end can leave a hold behind.
- **Control Panel > YouTube > Keep the Screen Saver Away While Playing**,
  on by default as the owner chose.

Four checks in the desktop suite drive a window that holds: the saver stays
away while it holds, comes once it frees, comes when the window closes still
holding, and the lock does not come while it holds. Two in the YouTube
suite: a playing video keeps the saver away past its idle time, and a paused
one lets it come. YouTube 1.3 (with its pane) and Arabic 2.0 in the
catalogue.

## 0.99.237

**The wallpaper has a Picture setting of its own** (Gitea #275).

Asked for: the same choice YouTube's player has, for the wallpaper. The
wallpaper drew by Control Panel > Pictures, the one setting every picture
in the desktop follows, so a wallpaper in half blocks meant every picture in
half blocks. Appearance > Wallpaper now has a **Picture** row -- Follow,
Half Blocks, Characters, Grey Blocks, Pixels, the same values in the same
order as YouTube's -- with a **Drawn Now** row saying what Follow comes to
on this terminal. It starts at Follow, so no wallpaper changes until
somebody chooses. Pixels on a display whose terminal cannot show them is
still half blocks there (#258); the setting says what is wanted.

`dt_imgm` and `dt_imgwill` take the mode as an optional argument, which the
wallpaper and the terminal's own wallpaper check pass and every other
caller leaves out, so nothing else moved. Appearance is 1.6 and Terminal
1.2 in the catalogue.

The checks that read the whole Appearance pane run on a taller terminal
now: their 26-row window had been clipped on a 24-row terminal all along,
and fitted only until these two rows were added below Shadows.

## 0.99.236

**A kitty display whose session predates 0.99.235 shows its picture
wallpaper again** (Gitea #274), and **nothing shows through the box over a
screen saver** (Gitea #259).

0.99.235 drew each display's wallpaper by what that display's terminal had
said, read with `hold gfx`, and drew half blocks for a display that had not
said. Reported by the owner within the hour: a PNG wallpaper in kitty had
become half blocks. Their session's server predated 0.99.235, so it could
not answer `hold gfx`, and every display read as unknown -- while that
server was still sending every picture to every display, as it always had.
A display carried across `hold upgrade` from such a server had the same
fault the other way: its old client never asked its terminal anything, and
the new server stopped sending it pictures. Both are a display **never
asked**, which is not the same fact as one that answered nothing. It has a
state of its own now, `unasked`, and gets what it got before: hold sends it
every picture it contains, and the desktop draws it by the session's own
answer. `unknown` keeps its meaning -- a new client whose terminal answered
nothing -- and still draws half blocks. A display's answer is carried
across `hold upgrade` too, which `790-hold` now checks, and the desktop
suite stubs a server that cannot answer and checks every display comes out
`unasked`, drawn as before; that check fails against 0.99.235.

**The screen saver showed through the login box, and the lock's.**
`sv_frame` flushed the saver's frame itself, before the box was drawn over
it and flushed again, so every frame reached the terminal twice and the
saver was on screen inside the box between the two -- "the bottom shows
through at times", as reported. Measured on the login screen: five of six
savers bled into the box, a toaster sat on its corner in 40 samples of 40.
`sv_frame` no longer flushes; whoever draws over it flushes once, and the
stand-alone saver flushes its own frames. The desktop suite samples the
login box over Flying Toasters for several seconds, every state the
terminal is sent, and fails against 0.99.235.

## 0.99.235

**A display that cannot show pictures is never sent one, and two displays
keep their wallpapers** (Gitea #258).

Reported from two displays: *"when I did a join, and the display is in
pixel mode, the wallpaper went black; when I changed it to half block, it
drew again."* Measured with a held desktop driven through two ptys, it was
two bugs.

**The first display's wallpaper was deleted and sent again over and
over.** `img` kept one record of what it had placed under the text, and
`dt_wallkeep` kept one rectangle, the last display's. So with two displays,
every frame that only kept the wallpaper let the console retire the first
display's picture: ten deletes and retransmissions in six seconds, against
none for one display alone. `img` keeps a record per rectangle now, and the
keep covers every display that has pixels.

**One display that could show pictures made every display get them.** The
session drew pixels once any display could, and hold sent the bitmap to
every display whose rectangle held it -- so a terminal that draws none got
a bitmap it dropped, over cells left blank for it: black, beside and
stacked alike. Now:

- **each attaching client asks its own terminal** what it draws, before it
  joins -- kitty's query and the device attributes, the two questions the
  console asks, from one parser both modules build in (`mods/gfxask.c`) --
  and tells the server. hold reads no replies out of what clients type;
- **hold sends a picture only to a display whose terminal draws that
  kind**, and one that never said -- a client older than this -- is sent
  none;
- **the desktop draws each display's wallpaper by what that display said**:
  pixels where its terminal draws the session's protocol, half blocks
  everywhere else. `hold gfx` names what each display said.

`hold clients` is unchanged, on purpose: ten readers take it with the
primary flag last, and a seventh field would have made a desktop running
beside an upgraded server lose its primary.

Two costs. A terminal that answers neither question waits a second, once,
at attach. And a display carried across `hold upgrade` from an older server
has not said, so it draws half blocks until it attaches again -- run
`desktop -r` after the upgrade the bar asks for.

Five checks in `tests/holdpix.py` drive a real held desktop through two
displays; four fail against 0.99.234.

## 0.99.234

**With the displays stacked, the bar answers on the primary, and the icons
are there** (Gitea #273), and **the test harness no longer inherits the
shell's own variables from whoever started it** (Gitea #253).

Asked by the owner, from two displays stacked with the primary at the
bottom: are the bar and the icons on the primary? The bar was drawn there
-- #240 taught the desktop the primary's row in 0.99.205 -- but **a press
was a bar click only at row 0 of the whole screen**, which is the display
*above* a stacked primary: the bar drew on the bottom display and took its
clicks on the top one, in the same place. A drag let go over a workspace
number and a drop onto the bar asked the same thing. And **an icon that had
not been dragged was laid out from row 2 of the whole screen**, so it drew
on the display above as well; a dragged one was already held to the
primary. `dt_onbar` answers "is this the bar" as the primary's own top row
inside its own columns -- which also stops the top row of a display *beside*
the primary counting as the bar -- and the icons start below the primary's
bar. Two checks in the stacked-display section, both failing against
0.99.233.

#253 recorded one gate where `090-module`, `199-sysmods` and `450-drop`
failed together, each finding a module that should have been gone, and the
next gate passing. **`tests/run.sh` passed on whatever environment it was
started from**, and `HIBR_MODPATH=build/mods` there reproduces the first
and the last exactly, diff for diff. The harness and `tests/all.py` now
clear `HIBR_MODPATH`, `HIBR_MODULES`, `HIBR_HOLD` and `HIBR_SYSDIR` -- every
test that wants one sets its own -- and `tests/215-test-env.t` fails if one
gets through, as it does against the old harness. `199-sysmods` sets its
own module path and is not reproduced by this; #253 stays open for its
next occurrence.

## 0.99.233

**A note has a smallest and a largest size, and says when it was cut**
(Gitea #227).

A note was as wide as its own text up to the screen, so the *"this session
still runs the older hibr"* note, or any program's one-line message, was a
box across the whole screen. As the owner set it out:

- **Four sizes, in Control Panel > Notifications**: Smallest Width and
  Largest Width in columns, Smallest Height and Largest Height in rows, the
  whole box with its border -- 24 to 52 columns and 3 to 8 rows unless set.
  Moving a smallest past its largest moves the largest with it, so the pair
  can never describe a box that cannot be drawn.
- **A note longer than its largest is cut there and marked with ▾** on its
  bottom edge, and it does not time out: it waits to be clicked, or the
  mark would be gone before anybody could use it. A click on the ▾ opens it
  to its full length, up to the screen, and ▴ shuts it again.
- **A double click opens the note in a window of its own**, the history's
  detail window, which **scrolls** now -- with a scrollbar, the arrows, the
  page keys and the wheel -- where it stopped at about nine lines and said
  nothing about the rest.
- A single click does what it always did -- runs the note's action and
  dismisses it -- but `DT_DBLMS` (400 ms) later, once a second click has
  had its chance to make it a double.

The layout is a widget, **`widgets/textfit.hibr`** (`dt_textwrap`,
`dt_textfit`, `dt_fitmark`): text between a smallest and a largest size,
marked when it was cut or opened. It knows nothing of notes, so the next
place that cuts text off to fit a space -- About This Computer, the
Catalogue's descriptions -- can say so the same way. `dt_notewrap` was the
wrapper inside it and is now `dt_textwrap`.

Notifications 1.1 and the Notifications pane 1.1 in the catalogue, and
Arabic 1.9 for the four new labels.

## 0.99.232

**A Homebrew install keeps the system scope where an upgrade leaves it, and its modules are found**
(Gitea #268), and **the package ships one screen saver and English only**
(Gitea #271, the owner's second pass).

Homebrew's `prefix` is the versioned keg, `Cellar/hibr/<version>`, and the
Makefile derived the system scope from it -- so everything `catalogue
--system` placed on a Mac was inside one version's keg, looked gone the
moment `brew upgrade` built the next, and was deleted by `brew cleanup`
after it. The formula now passes `SYSDIR=#{var}/lib/hibr`, Homebrew's own
`var/`, to both make steps, and `MKSYSDIR=no` to the install: the folder
is the catalogue's to make on first use, which it already does, so the
install writes nothing outside the keg. Anything a Mac installed with
`--system` on 0.99.215 to 0.99.231 is in the old keg and needs installing
again. Measured here by running the formula's two steps as an unprivileged
user into a keg-shaped prefix; what Homebrew's own sandbox makes of it
wants `brew upgrade hibr` on a Mac.

**And a module installed there is found.** The shell trusted the
compiled-in system modules folder only when root owned it, so every
module `catalogue --system` put in a Homebrew prefix -- which is its
user's, binary and `var/` alike -- was a file nothing loaded. The rule is
now that the folder be as trusted as the shell itself: owned by root **or
by whoever owns the running `hibr` binary**, and writable by nobody else.
That adds nothing an attacker lacked, since anyone who can write the folder
can already rewrite the binary beside it. The owner's choice of two;
the other was refusing such an install with a sentence. `tests/199` builds
a second shell whose default points at a folder this account owns -- the
Homebrew shape exactly -- and checks it is searched, and that it is not
once others may write it; the first fails against 0.99.231.

The package now ships **Mystify** and no other screen saver, and **no
language beyond English**. Classic Mac, Clock, Matrix, Pipes and Flying
Toasters, and Arabic, are in the catalogue. A desktop whose saved saver is
not installed picks a random one of those that are, and one set to a
language it does not have says so in its log and draws in English. The
pseudo-languages `xx` and `xy` stay in the tree, for checking its layouts.

## 0.99.231

**errexit holds inside a pipeline stage** (Gitea #270).

`set -e; { false; echo X; } | cat` printed `X`: every pipeline stage ran
with errexit off, which it had since the first commit, so a compound
command or a function in a pipeline ran on past a failure that would have
stopped it anywhere else -- the one place `set -e` quietly meant nothing,
which is what ADR 0002 exists to prevent. bash keeps errexit in a stage
and drops it only in `$( )`, and so does hibr now. A stage, a `( )` and a
background job keep it; a stage or subshell is exempt exactly when its
status is tested; `$( )` drops it unless `inherit_errexit` is on.

Two more in the same place. **A child that stopped printed the errexit
line, and then its parent printed it again** -- a `( )`, an inheriting
`$( )` and a waited-for background job each said it twice. The child
stops quietly now and the shell that forked it reports once. And **`!`
tripped errexit** when the status it inverted came out as a failure,
which POSIX says it never does: `set -e; ! true; echo after` stopped, and
goes on now.

What changes for a script: under ADR 0002 a failing stage fails the
pipeline, so `f | cat` where `f` fails partway now stops the script, as
`false | cat` always did here. In bash it goes on, because bash looks at
the last stage only.

`tests/214-errexit-children.t` covers each case and fails against
0.99.230.

## 0.99.230

**What the package ships is one list, and an app's pane comes with its
app** (Gitea #271, ADR 0047).

Asked: *"I do not understand why youtube control panel would be installed
without the youtube app"*, and then *"why do we also ship all the themes
and all the colours?"* Because `make install` copied the desktop folder
whole and took out `apps/` alone (0.99.194), so everything that was not an
application shipped, whatever it belonged to: **eight panes of apps the
package did not ship**, each a row in a fresh Control Panel saying *"YouTube
is not loaded"*, Prayer Times' strip module, and **every theme and colour
scheme**. And `mkindex` made every file in the catalogue's panes folder an
entry, so each app's pane was installable on its own with nothing behind
it.

There is no new kind. A file's **kind** is where it lands; its **owner** is
the entry that places it, and a **part** -- declared by the owner's `#
part:` line -- **goes where its owner goes, and is never an entry of its
own**. What the package ships is now **`packaging/ships`**, one path a
line, read by one tool, **`tools/ships.hibr`**, which the Makefile, the
scope suite and the documentation's pictures all ask. As the owner chose,
it ships:

- the desktop's own apps, panes, strip modules, savers and languages, as
  before;
- **Files, Terminal, Stickies, Calculator, Clock, Puzzle and Image
  Viewer** -- the package ships applications again, with their panes;
- **the Classic theme and the Midnight and Paper colours**, where it
  shipped four themes (2.1 MB of it three wallpaper pictures) and twelve
  schemes;
- and nothing else: Mail, Calendar, Contacts, Vault, YouTube, Prayer Times
  and their panes, the games, the office apps, the browser and the other
  themes and schemes come from the catalogue.

`make install SHIP=all` places everything, for a machine with no catalogue,
and `make install-extras` places exactly the rest -- the renamed `APPS=all`
and `install-apps`. `tools/ships.hibr -c` refuses a list naming a file that
is not there, a part listed on its own, a shipped theme whose colours do
not ship, and a pane filed under Applications that no app brings, and
`tests/541-ships.t` holds the install to the list file for file; it fails
against the old Makefile.

The catalogue drops thirteen entries that were only parts -- `tube`,
`notes`, `mailset`, `pimset`, `vaultset`, `filesview`, `terminal`,
`prayerset`, `abouthibr`, `cliphist`, `taskmgr`, `notifications` and
`nextprayer` -- each of which still arrives with its app. **`catalogue
update` died at the first installed entry the index no longer had**, which
would have stopped every update for anybody holding one; it says which app
the pane comes with now and goes on. An older `catalogue` still stops
there, and upgrading hibr, or `catalogue remove` of the pane, answers it.

The tour shows a desktop with an Applications menu and installs Mail, and
every picture was made again from what the package installs.

## 0.99.229

**`set` lists the shell, and `declare -p` prints what reads back in**
(Gitea #269).

Asked: *"in bash, we do `set` to see all the variables and functions ...
what do we have in hibr for the same?"* Measured against bash 5.2.15, the
answer was a gap and three bugs:

- **`set` with no arguments printed nothing.** It lists every variable and
  then every function now, as bash's does -- a value bare when it is safe,
  `'…'` when it is not, `$'…'` for a control character. Aliases are
  `alias`'s, in both shells.
- **`declare -p` did not escape a value.** `x='q"$z'` printed as
  `x="q"$z"`, so `eval "$(declare -p x)"` gave something else. It quotes as
  bash does now: `"…"` with `\"`, `\$`, `` \` `` and `\\` escaped, or
  `$'…'`.
- **An array or a map printed as `(...)`.** Its contents print now:
  `([0]="1" [1]="two words")`, and `([k]="v" )` for `-A`, with bash's own
  space before the parenthesis. A key that is not a plain word is quoted,
  and every non-numeric key under `-a` is, since an unquoted word there is
  arithmetic outside `(…)`.
- **`declare -x int n` printed as `declare -xint n`**, which reads back as
  `-x -i -n -t` and makes `n` a nameref. There is a space before the type
  now. And `declare --` was refused, so hibr could not read its own
  `declare -- x=…` lines; `declare --` and `readonly --` end the options,
  as `export` and `local` already did.

**[hibr]** A map holding maps has no bash syntax, so `declare -p` writes
the declaration and then one assignment a line, `n["a"]["b"]="v"`, which
hibr reads back. An empty map inside a map has no assignment and is left
out, and the JSON types `json parse` gave values are written as text;
`json emit` keeps both.

The listings -- `set`, `declare -p`, `export -p`, `readonly -p` -- come
out **sorted by name**, where they were in the order of the variable
table's hash. **`set +o`** prints the `set -o`/`set +o` lines that put the
options back (its two branches had always printed the same table), **`shopt
-p`** prints `shopt -s`/`-u` lines, and `hibr +o` does what `set +o` does.
Each listing fills a bound slot rather than printing, so `s := declare -p m`
holds it, which none of them did.

`tests/212-declare-p.t` compares the quoting, an indexed array and `set
+o` against bash; `tests/213-declare-p-maps.t` puts every shape through
`eval` in the same shell and in a fresh one. Both fail against 0.99.228.

Writing the checks found that hibr drops `errexit` inside a pipeline stage,
where bash keeps it: `set -e; { false; echo X; } | cat` prints `X` here.
That changes what scripts do rather than what a listing prints, so it is
Gitea #270 of its own.

## 0.99.228

**A wallpaper image can be chosen without the Files app** (Gitea #265).

The picker Appearance opens for *Wallpaper image* read its folders with
the Files app's own model (`fb_scan`, `fb_go`, `fb_path`). The package has
shipped no applications since 0.99.194, so on a fresh install the row did
nothing but say that Files was not installed -- a core setting needing an
application. The picker now reads folders with the desktop's own Open
dialog (`fp_go` and `fp_path` in `wm/filepick.hibr`), which ships with the
window manager. That dialog filters by type, so the picker lists folders
and PNG and JPEG files only, where it used to list everything and leave a
non-image unpreviewed. A folder it remembered that has since gone starts
it at home rather than failing.

Moving it found a bug in the Open dialog itself: going up from a folder
just under the root -- `/tmp`, `/home` -- handed `fp_go` the empty name
`${dir%/*}` makes of it, which said *Cannot open the folder* and stayed
put. Open, Save As and Export in every app had it. An empty name is the
root now.

Appearance goes to 1.5 in the catalogue, since its guard for the missing
Files app is gone.

## 0.99.227

**A running desktop picks up what the catalogue installs, without a
restart** (Gitea #267).

An app installed from the Catalogue window, or with `catalogue install`
at a terminal, reached the Applications menu only after Restart Desktop,
because the desktop read its folders once, when it started. The tour
released yesterday had to tell visitors so. Now:

- **`dt_rescan`** reads the folders again. The four loaders already
  skipped any file they had loaded, so a second pass sources only what is
  new; a name whose file has gone from every scope is taken off the menus
  (a window still open on it keeps working); and the Control Panel's panes
  and the Control Strip's modules are read again by their own owners. Each
  open Control Panel is put back on the pane it was showing, by name, since
  the list is sorted again.
- **It is told, never polling**, because an idle desktop's budget is a
  hundredth of a core. `catalogue` sends `{"op":"rescan"}` to every control
  socket of the account after any verb that wrote its state, and nothing
  after one that only reads; the Catalogue window asks its own desktop
  directly as well; and **Refresh Desktop** rescans, which covers a file
  somebody put in a folder by hand. `desktop ctl rescan` answers how many
  names arrived, went and changed.
- **Something already loaded that changes is not sourced again.** Apps run
  code at the top of their file that resets their state and registers
  hooks, and a desktop file is `strict`, which refuses a second definition
  of a function from the same file -- the first run of the new checks
  printed thirteen such refusals for one calculator. An update, a copy in
  another scope now winning the name, or a name that went and came back
  keeps running what was loaded; the bar shows ↻ beside the bell and a note
  names it until Restart Desktop. A name that comes back is given the
  registration it went with, so it is on the menu again at once.

A `--system` install reaches only the desktops of whoever ran it; anybody
else's picks it up at Refresh Desktop or its next start.

`tests/scopes.py` drives one desktop through all of it -- an app placed,
changed, removed, a pane and a strip module placed, and a file returned
by hand and picked up by Refresh Desktop -- and reads the result from the
menus and the bar. `tests/apps.py` installs through the Catalogue window
and finds the app on the menu; that check fails with the window's own
rescan taken out. `tests/appmgr.py` checks that `catalogue` tells a
desktop after an install and a removal and says nothing after `list`.

One collision on the way, caught before it shipped: the first name for
putting back a gone app's registration was `dt_restore`, which is
`wm/restart.hibr`'s function for putting back a restart's windows. The
later definition won, so the rescan quietly ran the restart's restore.
`tests/540-examples.t` already fails on a name defined twice across the
desktop's parts; it is `dt_reinstate`.

The Catalogue window is `system/catalogue` 2.3.

## 0.99.226

**The desktop has a tour, its guide is in two halves, and every picture
was made again.**

The documentation sweep asked for earlier. A visitor reading about the
desktop met a 1,888-line page that opened on X's three layers and went
straight from where windows land to `dt_menu`'s arguments. Three changes:

- **`docs/desktop.md`, your first hour with the desktop**: start it,
  detach and come back, the hibr menu, the Catalogue, a Restart Desktop to
  see what it installed, the keys worth learning, Appearance and the
  Control Strip, and which of your two folders is which. Five pictures,
  linked from the project README and the docs index.
- **The guide is split**: `programs/desktop/README.md` is how to use the
  desktop, and the new `programs/desktop/WRITING.md` is how to write for
  it -- sessions, apps, widgets, menus, managing other windows, Control
  Panel panes, strip modules and the control socket -- opening with a
  table of what goes in which folder. The split was made by a script and
  checked paragraph by paragraph: all 287 of the old page's paragraphs are
  in one of the two files. WRITING.md is checked for its examples like
  every other page.
- **Every picture is new, and none of them shows a desktop nobody has.**
  The package installs no applications, and the tour's first pictures
  were rendered with the tree's `apps/` loaded -- an Applications menu no
  install has. `tools/docshots.py` now serves a copy of the real catalogue
  on this machine, installs Calculator and Files from it with the real
  `catalogue` command, and photographs the desktop before and after. Two
  of the reference pictures were rendered with no system apps and no
  Control Panel, so the hibr menu showed an empty group between two rules
  and Change Wallpaper was dimmed; both are a real session now. And every
  picture of Files showed the home folder of whoever ran the tool; they
  show `/tmp/visitor`, made fresh for the run.

Two things the pictures found in the tool. **A glyph DejaVu Sans Mono
lacks was drawn as the missing-glyph box** -- three Control Panel icons in
every picture of it -- where a terminal falls back to another font;
`tools/screenshot.py` falls back to Noto Sans Symbols now, and
`tests/shots.py` checks it. And `--env` passes a variable to the session,
which is how a picture is taken over a data folder of its own.

**Anchors are checked.** `tests/530-docs.t` checked a linked page exists
and never the part after the `#`, so moving eight sections would have
left links landing at the top of the wrong page with nothing failing.
`tools/mdanchors.py` slugs every heading the way GitHub does and checks
every `page.md#anchor` in a tracked page; 530 runs it. The one link into
a moved section, from ARCHITECTURE.md, was fixed by it.

One gap the tour had to admit rather than paper over: **an install from
the Catalogue window is not seen until Restart Desktop**, because the
desktop reads its application folders when it starts. The tour says so.

## 0.99.225

**The catalogue command and its window now share one prefix.**

Spotted by the owner: *"is the prefix ap and AP still used in catalogue? I
thought we moved to cat and CAT?"* -- and they read it right. The
`appstore` -> `catalogue` rename in 0.99.216 converted the desktop
**window** to `cat_`/`CAT_` and left the **command** on `ap_`/`AP_`:
390 functions and calls and 592 globals, left over from when the command
was called `apps`, until 0.99.195. Anybody grepping for one prefix found
half of the catalogue.

All 1,009 identifiers are `cat_`/`CAT_` now -- in the command, the
window's one comment, four tests, `tests/deps.py` and `tests/scopes.py`
(which read `CAT_KEXT` and `CAT_KINDS` out of the file), three ADRs,
`docs/catalogue.md` and CLAUDE.md -- and in the catalogue repository's
`mkindex`. Renamed by identifier rather than by a pattern with a word
boundary, because that is what missed `declare -gA AS` and
`appstore_draw` last time; a grep for the old prefix across `programs/`,
`tests/`, `docs/`, `tools/` and the C finds nothing outside this file.

**One interface moved with it:** the lock wait read from the environment
is **`CAT_LOCKWAIT`**, no longer `AP_LOCKWAIT` (`docs/catalogue.md`). It is
a tuning knob a suite sets to make a lock timeout happen in a moment.

`tests/540-examples.t` fails if the old prefix comes back in the command
or the window -- checked by putting one name back and watching it fire.
The window is a catalogue entry itself, so it is `system/catalogue` 2.2.

Gitea #266.

## 0.99.224

**An installed app could not load, because it only worked where the
shipped tree had put it -- and nothing checked what an installed file
reaches.**

Reported from a desktop that would not start:

```
hibr: source: /home/osakka/.local/share/hibr/apps/../lib/pim.hibr: No such file or directory
hibr: pe_onsync: command not found
hibr: source: .../apps/Office/../../lib/diagram.hibr: No such file or directory
```

and then the question that mattered more than the bug: *"what if nothing
was installed? is our desktop really dynamic or is it broken? what guards
us against it being broken and asking for things it does not have?"*

Calendar found its library at `${BASH_SOURCE%/*}/../lib/pim.hibr`. In
this tree that is `programs/desktop/lib/pim.hibr`; once the catalogue
has put Calendar in `~/.local/share/hibr/apps/` it is nothing. The user
scope is read **before** the shipped copy (ADR 0045), so installing
Calendar replaced a working app with one that could not load -- and its
own ticker then called the missing `pe_servers` once a frame, which is
**1.78 million lines** in the reporting desktop's log.

**Measured before anything was changed**, the same root was in more than
one file: eleven paths built from a file's own location (Calendar,
Contacts, Diagram, Write, Sheet, Mail, and the PIM, Mail and Appearance
panes -- Mail's sync job and the mail account test failing *silently*,
with no error, only no mail), and **220 calls from one installable into
another, 156 with no guard**.

### The rule (ADR 0046)

An installable reaches only what every scope has:

- **the platform** -- the core, the shared libraries, and a file whose
  header says `# platform:`. The Control Panel (every pane is built on
  it) and the saver framework (every saver is) say so now, and the
  catalogue refuses to publish them, so neither is ever installed apart
  from what runs on it;
- **shipped files, through `$DT_HERE`**, never `${BASH_SOURCE%/*}/..`;
- **another installable only behind a guard in the same function** --
  `dt_has`, or the new `dt_needs APP FN`, which says *"APP is not
  installed. Find it in the Catalogue."*, the words `dt_launch` already
  used. Every link between two apps is an optional integration rather
  than a dependency: Mail without Calendar shows an invitation's actions
  under their own names; the wallpaper picker without Files says it
  needs Files.

**Libraries are the desktop's to load.** `dt_libs` sources every file in
the shipped `lib/` that marks itself `*_LIBDONE=1`, before any app and
only from there, so a stray copy cannot shadow the package's. The files
there without the mark are programs run as jobs and are never sourced.
This is what repairs **installs already out there without touching their
files**: each old copy's own guarded source line is now never reached.

### What guards it

- **`tests/deps.py`** reads every file of every script kind the catalogue
  installs and fails on an own-location path, a sourced library the
  desktop does not load, and an unguarded call into another installable.
  Against the tree before this release: **167 failures**. After: none.
- **`tests/scopes.py`** starts the desktop with the platform alone; as
  the package ships it (no applications -- every fresh install); with
  each of the 69 installables alone over the platform, *used* -- launched,
  opened, started; and with all of them over the shipped desktop,
  shadowing it. Any shell error fails it, naming the installable. Before:
  Calendar, Contacts, Diagram, Write and everything-over-shipped failed.
  After: **73 of 73**.

They see different things, which is why there are two: no scratch
desktop has a mail account, so only reading catches Mail's sync path.

### Three more found on the way

- **`catalogue update` decides by the bytes.** It asked only whether the
  version had moved, so a part that changed under an unchanged app -- the
  Notes pane under Stickies 1.0 -- reached nobody who had installed the
  app. Every file an entry placed must now match the `sha256` the index
  gives it. Keeping versions in step by hand is a rule the author of this
  release broke the same day: YouTube's catalogue copy was never
  republished after 0.99.221, so anybody who installed it from the
  catalogue never saw that release's player changes.
- **The core assumed the saver framework and every ticker existed.**
  `dt_saverstart` called `sv_begin` unguarded and cleared `SV_NAME`,
  which only the framework declares; `dt_tickers` called every
  registered ticker every frame whether or not it was defined. A
  desktop now starts without the framework and simply has no saver, and
  a ticker that is not defined is dropped with one line in the log.
- **Cursor and Cursor Blink apply at once** (Gitea #261). A terminal was
  told its cursor style when it opened and when a restart adopted it,
  nowhere else; and turning Blink on asked for no frame. And the check
  for it found a second half: a window was judged clean on everything but
  the cursor, so the frame the change asked for came and skipped it.

### And the tests were writing into the developer's own folders (#263)

`tests/desktop.py`'s held fixtures started desktops with `subprocess.run`
and no state folder of their own, so they wrote snapshots into the real
`~/.local/state/hibr` -- and the reporting desktop restored windows out
of a test's snapshot on restart. Every gate that day did it again, and so
did the scratch probes written to diagnose it. `tests/screen.py` now
points every XDG folder at scratch for the whole suite, which covers any
helper written later as well as these.

Not reproduced: the reporting desktop **ending** at start. Its own
installed set, its own settings and its own saved windows, held, on the
installed hibr, keep running -- with the source errors and the per-frame
`pe_servers` flood, both of which are gone now.

Gitea #262, #261, #263.

## 0.99.223

**Every frame of a video asked the terminal to blank the rectangle
before drawing it, and the protocol says so in as many words.**

Reported precisely: *"the text below once it overlaps bleeds through,
and the blinking every frame, it seems we blank then draw, every frame?
... only in pixel mode, half block and other modes seem to be 100%
fine. This is a pixel rendering issue I think that affects all our
pixel rendering?"* -- and that last guess is right.

The stream was read rather than reasoned about. Before each
transmission the console sends 25 to 169 bytes: one cursor move, **no
erases, no printable text**. hibr does not blank anything. The blanking
is the terminal's, and it is required:

> When re-transmitting image data for a specific id, the existing image
> and all its placements **must be deleted**. The new data replaces the
> old image data but is not actually displayed until a placement for it
> is created.
>
> -- kitty's graphics protocol

So re-using an id, which `image.c` described as *"one escape rather
than a delete and a place"*, is exactly a delete and a place, with the
delete first and whatever is under the cells showing in between. At one
frame nobody sees it. At a video's rate it is a flicker on every frame,
and anything drawn under the picture shows through the gap -- both
symptoms, one cause.

A replacement takes a **new** id now and the old one is deleted
**behind** it, after the new placement has gone out, so there is never
an instant without one. The stream before and after, over five seconds
of playback:

```
before:  31 placements, 1 id, 0 deletes
after:   31 placements, 31 ids, 30 deletes
         p:5589 d:5588  p:5590 d:5589  p:5591 d:5590 ...
```

Zero deletes with no newer placement already ahead of them. The cost is
one escape of about thirty bytes a frame against a payload of hundreds
of kilobytes, and two live images per region instead of one.

It is general to pixel rendering, as reported -- a still picture simply
never showed it, because the gap lasts one frame and a still has only
one. Two checks in `tests/kitgfx.py` held the old contract and now hold
this one; two more drive a run of frames.

**And the display work's own gap, found the same day.** Normalising a
negative arrangement runs from `hd_union`, which runs when a client
attaches, moves, drops or resizes -- an upgrade is none of those, so
`hd_resume` restored a session's clients straight out of a file and
went to the serving loop with the fix unreached. Measured live:
`hold shift` answering (so the server was new) beside
`client-6 -66 3` (so nothing had normalised it). That is why a session
upgraded to 0.99.220 still drew nothing above the primary, and why
detaching the secondary cured it.

It is normalised on resume, and the shift counter is carried across the
exec in the resume file -- without that an upgraded server answers `0`
to a desktop last told `12`, and the desktop moves every window back by
a shift that never came undone. A reader that does not know the line
skips it, so either server reads either file. Checked in
`tests/790-hold.t`; against this release without the carry it reads
`0 0` where it should read `10 4`.

And a check in `tests/kitgfx.py` that waited 400ms for an answer which
arrives when the *harness* is next scheduled to reply, not on the
script's clock -- it read the name-list guess at 78 of 79 in a release
gate and passed three concurrent runs under spinners. Ten short waits
instead of one, because `console key` returns early on a resize and a
single wait can be cut short where a loop cannot.

Gitea #199, #257.

## 0.99.222

**A picture was magnified before it was sent, and the magnification
was the whole cost.**

Reported straight after 0.99.221 fixed pixel mode itself: *"I think
you've fixed it! with one exception, it blinks between frame
refreshes."*

Measured first, which is what named it. With the player open, the
desktop's own frame rate:

| mode | frames a second |
|---|---|
| half blocks | 14.1 |
| pixels | 1.5 |

Same clip, same window, and **the same on 0.99.220** -- so the blink
was not the previous release, it is what pixel-mode video has always
cost. A picture that updates one and a half times a second while the
clock runs in real time is a slideshow, and that is what "blinks
between frame refreshes" is.

The cost is bytes. `cn_image` scales a bitmap to the rectangle it will
occupy **in the terminal's own pixels** -- which is right for sixel,
which paints 1:1 and would spill or leave a gap otherwise, and is
pure waste for kitty, which is *given* the rectangle in cells (`c=`,
`r=`) and scales the picture into it itself. So a 320x180 video in a
160x46-cell window was resampled up to 1280x736 here, 2.8 MB a frame,
and sent -- every byte down a pty, through `hold`, and out to each
attached client -- for a picture the terminal then drew at exactly the
size it would have drawn the original.

Never larger than the source, then, for kitty; `im->id` is the one flag
that tells it from sixel. And the same bound in `mv_fitpx`, so the
decode thread does not invent the pixels in the first place -- a scaler
has no more detail to give than arrived.

The same measurement, 320x180 into a 160x46 window:

| | frames a second |
|---|---|
| magnified before sending | **0.3** |
| sent as it is | **4.3** |

**Fourteen times**, for an identical picture on screen. It is the
owner's own case exactly: a 216-column screen at an 8x16 cell is a
1728x960 rectangle, 4.9 MB of raw pixels a frame, for a stream fetched
at a fraction of that.

What this does **not** do is make pixel-mode video as cheap as half
blocks; 4.3 against 14.1 is still the wire. The remaining lever is
compression, which `console imgcomp` applies to the wallpaper alone --
deliberately, because deflating a page cost about 100 ms against a
0.7 ms frame budget (#129). That trade is worth re-measuring now the
frame is bytes-bound rather than budget-bound, and it is its own
ticket rather than this one.

Gitea #199.

## 0.99.221

**Pixel mode had the lowest resolution of all four picture modes, and
when it was refused it fell back to the worst cells rather than the
best.**

Reported from a live desktop and then again directly: *"youtube still
does not render except blocks even pixel mode"*, and *"the browser in
pixel with a pixel wallpaper displays pixel graphics and text
correctly. The YouTube player behaves differently."* The second
sentence is what made this findable -- the wallpaper and the Browser
place bitmaps in the same session, so the capability, the terminal and
`hold` are all out, and what is left is the player's own path.

Two faults in it, both the same shape: **pixel mode was worse than the
mode below it.**

**The frame was decoded at the cell count.** `mv_fit` answers the size
of the *grid* a picture fills -- in cells for the character modes,
half-cells for half blocks -- and `play.c` used that same answer as the
**scaler's output size**. For half blocks that is right: a cell shows
two pixels, so two is all it is worth decoding. For pixels it is not:
the console resamples the frame up to the rectangle's own pixel size,
so a picture asked for in *pixels* was decoded at roughly **one source
pixel per character cell** and then magnified by the whole size of a
cell -- about a hundredfold on an 8x16 terminal. It arrived as large
soft squares, which is what "renders as blocks even in pixel mode"
looks like. `mv_fitpx` is the decode size and `mv_fit` is the grid;
they were one function answering two questions.

**And a refused bitmap drew the brightness ramp.** `mv_cellof` renders
half blocks only for mode 0; mode 3 fell through to the one-character-
per-cell ASCII ramp. So whenever the display declined -- it has no
answer about pictures yet, the terminal draws none, the frame does not
fit its rectangle -- asking for pixels gave a *worse* picture than
asking for half blocks. It falls back to half blocks now, the best
cells there are, with the grid refitted for them (without that the
lower half reads off the end and comes back black).

**And it said nothing about either.** The player's bar showed `pixels`
over a screen of blocks, because it reported the mode it *asked for*.
`media info` carries `drawn` now -- what the last draw actually did --
and the bar says `(drawn as blocks: the display declined)` when the two
differ. That reading is what tells the four layers apart, and not
having it is why this ticket outlived a hundred releases; a debug line
in `mv_draw` prints the rectangle arithmetic under `hibr -d 3` for the
case where it is the fit.

Three checks in `tests/youtube.py`, and one of them closes a coverage
hole worth naming: **every pixel check in that file forced `HIBR_GFX`**,
which skips the capability question entirely -- so the suite had only
ever driven the one path no real session is on. The new one lets the
terminal answer for itself (`Term(caps=["kitty"])`), which is what a
desktop actually does.

What is **not** claimed: this was not reproduced from the owner's own
session, which could not be driven from here. It is two defects found
by reading the arithmetic in the path their report points at, each
fixed and each checked.

## 0.99.220

**A display put above the primary was outside the union, so it drew
nothing at all.**

Reported from a two-display session: *"I want to setup the secondary
(on top) of the primary as I've done. Interestingly, I get no wallpaper
or context menu when I place it up there, and I cannot move windows to
it ... the panel/display sizes seem incorrect on the display control
panel, so when I align them, they overlap."*

Both halves are one bug, and it was measured on the owner's own live
session before anything was touched:

```
$ hold clients desktop
client-4    0  0  64 213  1
client-6  -68  3  64 213  0
$ hold list
desktop attached 1746537 64x216
```

`hd_ubox` measured the union **from 0,0 to the furthest edge any client
reached** and never looked at a minimum, so the client at row −68
contributed nothing to it: the pty and the emulator were sized without
it, and `hd_render` then read rows −68 to −5, for which `tm_at` answers
null. Nothing crashed and nothing was logged. The display was simply
blank for as long as it was there — no wallpaper, no menu could open on
it, no window could be dragged onto it — and the mouse had the mirror of
it, reporting a negative row for every click. The three phantom columns
on the right of that union are the secondary's `col 3` with no left-hand
space to match.

The Control Panel looked wrong for the same reason: it shifts its **own**
picture so nothing falls off its canvas, so it drew a correct arrangement
hold was not in, and aligning two edges there aligned nothing. It had
deliberately stopped clamping a release to zero — its own comment says
*"this clamp was the bug"* — and that was right; the other half was never
done, and the negatives went to a server that had never handled one.

**hold normalises the origin now.** On any change, every client is moved
by the same amount so the arrangement's top-left corner is the union's
0,0: nothing moves relative to anything else, and the program gets one
screen big enough for all of it. A **positive** minimum is left alone,
which is a decision rather than a shortcut — attaching at an offset with
nothing above or left of it is how space is *reserved* for a display that
has not joined yet, which is how `desktop --join` and `tests/desktop.py`
both stack a union.

**And the desktop moves its content with it**, because normalising moves
the primary out from under everything already placed on it. Anything
recomputed from the primary's rectangle each frame — the bar, the menus,
the icon layout, the Control Strip's default, dialogs, `dt_place`,
`dt_tile` — follows on its own; a window a person put somewhere is stored
in the union's coordinates and does not.

The amount is **asked for**, not inferred, and `hold shift` exists
because both ways of inferring it were tried and were wrong. Where the
primary is moves by a whole display's width when somebody merely picks a
different primary, with nothing having moved at all — which slid every
window eighty columns sideways and failed two checks that had nothing to
do with this. And how far one display moved cannot be told from that
display having been the one dragged, which is the case that causes a
shift in the first place.

Checks: a new section in `tests/790-hold.t` — every geometry check in
that file used non-negative offsets, which is the coverage hole this
lived in — failing against 0.99.219 with `nothing is left at a negative
row or column: 1` and a union of `24x80` where it should be `34x84`; and
three in `tests/desktop.py` driving a real held two-display desktop
through `desktop ctl`, comparing the window's movement against the
primary's rather than asserting a row, because what matters is that the
window did **not** move relative to the display it is on. **The whole of
790's existing recording is unchanged**, which is what says the
normalisation changes nothing for every arrangement that already worked.

Three catalogue bundles were behind the tree and are republished with
their versions bumped: `system/catalogue` 2.1, `control-panel/appearance`
1.3, `lang/ar` 1.7.

Gitea #257. #258 (a picture wallpaper going black when a second display
joins in pixel mode) and #259 (what is drawn over a screen saver showing
it through) are open and deliberately not folded in: neither is proven to
share this cause, and a fix that can only be tested after this one says
nothing about which change mattered.

## 0.99.219

**One column cannot hold two facts, and the catalogue was doing it
twice over.**

Reported in four parts, all of them the same shape: *"catalogue list
does not have versions"*, *"catalogue installed still says shipped came
with hibr"*, *"when it's user, it does not say user, only a version
number"*, *"why does it say shipped and allow me to install?"* -- and
then *"why colours/dracula ... it should be different columns"*.

Four facts about a row -- what kind of thing it is, what it is called,
which version, and where that version is -- were being carried in two
columns. `colours/dracula` is a kind and a name jammed together, and
the version cell held **either** a version **or** a scope word
depending on the row, so a column meant one thing on some lines and
another on others. That is the one-value-two-questions trap this file
records for `MLME` (#157), the default background (#184) and
`installed` (#254) -- walked into for a fourth time *while fixing the
third*.

Both listings are four and five columns now, with a heading when
standard output is a terminal and none in a pipe:

```
KIND           NAME          TITLE                     VERSION  SCOPE
apps           vault         Vault                     1.0      user
colours        dracula       Dracula                   1.0      shipped
```

`installed` lists everything present with its scope rather than the
ledger alone, and says `shipped` -- the footer explaining that shipped
means it came with hibr is gone, because the word already says so.

The Catalogue window had the same two bugs and one of its own. A row
now carries VERSION and WHERE as two columns; and the button branches
on what pressing it would actually do, so a theme the package already
provides **at the same version** offers no Install, and one the
catalogue has newer offers Update. `ap_shipver` is what makes that
possible: "do you have one" and "which one have you got" are different
questions, and only the first was ever asked.

Four checks in `tests/apps.py`, each failing against 0.99.218, and
eight in `tests/appmgr.py` that had been reading a listing by looking
for the id as a string -- which the listing no longer holds anywhere,
since the id *is* the two columns. `lsrows` reads a row by its kind
and name; every **message** the manager prints still names the id, so
only the listings wanted it.

## 0.99.218

**Every screenshot in the documentation was corrupt, and there was no
recipe to remake them with.**

First of three passes over the documentation, asked for directly: *"I
want a full update on our documentation ... especially our guides on
desktop with screenshots, think of anyone visiting."*

`tools/screenshot.py` corrupted its own output until 0.99.148, and the
eight pictures under `programs/desktop/img/` were made before that and
never regenerated — sixty-nine releases. `tools/README.md` recorded it
as *"missing the word File"*. It was worse: `desktop-strip.png` had lost
**File**, **View** and **Special**, the workspace switcher and the
notification flag. A visitor's first picture of the desktop showed a
menu bar with three of its five menus gone.

They were never remade because **no recipe existed anywhere**. Each was
made by hand, once, so there was nothing to re-run.
`tools/docshots.py` is that recipe now, one place beside the renderer.

**Writing it found three faults, each of which made a picture quietly
wrong rather than failing:**

- **`--apps` was flat where the desktop globs `**`.** It resolved
  `programs/desktop/apps/<name>.hibr` only, so the calculator — which
  lives in `Accessories/` — sourced nothing and its window photographed
  as an empty box with a correct title. A glob list is where coverage
  goes to hide, wearing another hat.
- **`--wait` is a settle timeout, not a dwell.** It returns the moment
  the desktop first goes idle, which is long before a program inside a
  window has started: a terminal rendered blank however large `--wait`
  was made, and raising it could never have helped. `--until TEXT` waits
  for the thing, which is the rule the suites already follow.
- **A pane and a strip module are scan roots of their own.** Loading the
  Control Panel app without `CP_PANEDIRS` photographs *"Nothing found"*,
  and `CS_ON=1` with no `CS_MODDIRS` photographs a desktop that simply
  has no Control Strip. Both are true pictures of a desktop nobody runs.

**And four of the eight captions were false**, which is the half no
renderer can check. The hibr menu does not say *"About hibr, Clean Up
Icons"* — it says About This Computer, Screen Saver, Lock Screen,
Restart Desktop, Detach and Quit. The context menu has gained Next
Wallpaper and Refresh Desktop. The Control Strip shows five modules, not
four. Control Panel opens on Date & Time, and its picker holds nineteen
panes in two groups. A caption is a claim, and the tool proves the
pixels, not the sentence under them.

## 0.99.217

**The documentation says what the code does.**

Asked for directly: *"Can we please update all the hibr-apps
repository? And the documentation, it should be 100% accurate right?"*

It was not. The catalogue's command was renamed from `apps` in 0.99.195
and the sweep stopped at the code:

- **`docs/catalogue.md` showed a transcript the code cannot produce** --
  bare names where ids are printed, the old column widths, and
  `installed` where the word has been `user` since 0.99.215. That is the
  thing this tree's own standard forbids: *every claim run before it was
  written down, and the output pasted back*. Both blocks are re-run
  output now, checked cell by cell against a real row, and the page says
  what `list` and `installed` each answer rather than implying they
  answer the same thing differently.
- **nine `apps <verb>` in docs and comments** -- two ADRs, `docs/modules.md`,
  and five desktop files -- naming a command that has not existed for
  twenty-two releases. Swept on the backticked form alone, so
  `wm/bar.hibr`'s *"the number of apps installed"*, which is prose about
  applications and not about a command, is untouched.

**And the catalogue repository, where the worst of it was public.**
`index.html` -- the page <https://osakka.github.io/hibr-apps> is served
from -- told every reader to run `apps list` and `apps install snake`.
Its generator said so too, so regenerating would have put it straight
back.

Two more there, neither cosmetic. `mkindex`'s minimum-hibr map was keyed
by **entry id**, so `ENTHIBR["system/appstore"]` stopped applying the
moment that entry became `system/catalogue` in 0.99.216: a desktop too
old to draw the window would have been offered it without a word. And
the README said `--system` installs into `/var/lib/hibr`, which stopped
being true in 0.99.215 -- it is `$HIBR_SYSDIR`, the FHS path only where
hibr is installed the way a distribution installs one, and inside its
own prefix otherwise.

`stale`'s own row in that README described one of the four things it
checks, and not the direction it gained in 0.99.213.

## 0.99.216

**The Catalogue window is called the Catalogue all the way down.**

Asked for directly: *"don't use appstore right? It's Catalogue? We
should probably update all the naming, the internal variables, and
everything to do with the catalogue to reflect its correct name?"*

Right. The window has shown **Catalogue** since the command was renamed
from `apps` in 0.99.195, and everything underneath still said
`appstore`: the file, the nine window callbacks, twenty-five helpers,
seven globals and the window's own map.

    programs/desktop/system/appstore.hibr -> system/catalogue.hibr
    appstore_draw, _open, _key ...        -> catalogue_*
    as_job, as_kindname ...               -> cat_*
    AS_JOBS, AS_KINDS, AS_LIVE ...        -> CAT_*
    AS[...]                               -> CAT[...]

350 sites across five files. `CHANGELOG.md` is left as written: its
entries describe the tree as it was at each release.

Checked free before taking them: `catalogue_`, `cat_`, `CAT_` and `CAT`
were all unused, which matters because the callback contract is the
prefix -- a collision would have had the window manager calling somebody
else's function.

**Two things a word-boundary sweep gets wrong, both found by running
it.** `\bappstore\b` does not match `appstore_draw`, because `_` is a
word character and there is no boundary before it -- so the nine
callbacks, the one contract that must match the prefix, were the only
things it skipped. And `\bAS\[` renamed every *use* of the window's map
and left `declare -gA AS` alone, so the map was declared under one name
and written under another: under `strict vars` every write is refused,
and the window drew **nothing at all** while the command behind it ran
correctly and answered. A rename that misses a declaration does not
error; it produces an app that opens and is empty.

The published bundle moves with it: `system/appstore` becomes
`system/catalogue`, a clean break like `apps` to `catalogue` was.

## 0.99.215

**A prefixed install creates nothing outside its own prefix, so
`brew install hibr` works again.**

Gitea #256, reported from the owner's Mac: *"We broke homebrew? it now
says /var/lib/hibr (permission denied) ... I broke my installation."*

**Ours, not Homebrew's.** Every path in the Makefile derives from
`PREFIX` except one -- `SYSDIR ?= /var/lib/hibr`, written out when the
system scope arrived in 0.99.197 -- and `make install` creates it. The
formula runs `make install PREFIX=#{prefix}` with no `DESTDIR`, so on a
Mac it tried `install -d /var/lib/hibr`: outside the prefix, root-owned,
and none of Homebrew's business.

`/var/lib` is the FHS place for state a program maintains **when the
program is installed where the FHS expects one**. A prefixed install is
self-contained by convention, which that line forgot.

    PREFIX=/usr            /var/lib/hibr              (the deb, unchanged)
    PREFIX=/usr/local      /usr/local/var/lib/hibr
    PREFIX=/opt/homebrew   /opt/homebrew/var/lib/hibr

**`/usr/local` is deliberately not an FHS prefix here**, which is the
detail that matters: Homebrew's prefix on an Intel Mac is exactly
`/usr/local`, so treating it as one would have fixed Apple Silicon and
left Intel broken in the same way.

Nothing else needed changing. There is no hard root-owned gate on the
system scope -- ADR 0045 describes that as what a distribution's install
looks like, and `ap_needwrite` refuses a write the account cannot make
and names the command to use. A Homebrew prefix is user-owned by design,
so `catalogue --system` there simply works, which is right for a
single-user prefix.

`tests/211-prefix-sysdir.t` checks all three with `make -n`, a dry run
costing a fraction of a second, and names the fault precisely against
the old Makefile: *"/opt/homebrew creates /var/lib/hibr, which is
outside its own prefix"*.

**And the catalogue's two listings stop reading as two programs.**
Reported in the same breath:

> *"savers/pipes 1.0 savers shipped came with hibr (this is a rubbish
> experience), why is savers twice, why shipped came with why not just
> shipped?"* and *"catalogue list is inconsistent and so on... why?"*

Three faults, all mine, all from adding the scope without re-reading the
row it went into:

- **the kind twice** -- the id already begins with it, so the kind
  column was pure repetition. Dropped.
- **the scope twice** -- `shipped` in its column and `came with hibr`
  beside it. The note is gone; a row with nothing to note now ends after
  the scope, with no trailing blanks either.
- **one state under two names** -- `list` said `installed` where
  `installed` said `user`. It is `user` in both now, beside `system` and
  `shipped`, so the three words mean the same thing wherever they are
  read.

And the columns line up: both listings measure with `str pad` rather
than `printf`'s own `%-24s`, which counts **bytes**. `Screenshot…` holds
an ellipsis that is three bytes and one column, so that row came out two
columns short and took the rest of the column with it -- the lengths-and-
slices rule of ADR 0021, in the one place nobody had applied it.

## 0.99.214

**The Catalogue window says which scope a thing is in, and its group
names stop explaining themselves.**

Asked for directly, looking at the window after 0.99.212:

> *"how can I distinguish system installed vs user installed? everything
> seems installed, but how can I distinguish between them?"*

A fair question, and the answer was: you could not. 0.99.212 gave the
package's own copies a tick, which was right -- you do have them -- and
left twenty-one things the person installed and sixty-four the package
shipped looking identical in the list. The detail line said which, one
row at a time, which is not what a list is for.

The right-hand column now says **which scope** rather than a version,
for anything that is not the person's own: `shipped` or `system`,
against a version for one they installed. Same three words the command
line uses, so there is one vocabulary rather than two, and a tick plus
a version now means "yours, and removable" where a tick plus `shipped`
means "the package's".

And the group headings say what they are rather than describing
themselves: **Desktop**, **Control Panel** and **Control Strip**, where
they read *"The desktop's own"*, *"Control Panel panes"* and *"Control
Strip modules"*.

`appstore` 1.4 to 1.5 and the Arabic catalogue 1.5 to 1.6, both
republished -- `./stale` is what says so now, in the direction it
gained in 0.99.213.

## 0.99.213

**A kind the catalogue offers must be a folder the desktop reads.**

Gitea #255. The catalogue and the desktop both know where a kind lives
and **cannot be made to know it once**: the desktop declares a scan list
per kind, in the file that owns that kind, and each appends roots of its
own at session time -- so a shared table could only ever describe the
four standard scopes and every list would still have to add to it. The
catalogue is installed as its own command and must run with no desktop
present, so it cannot read the desktop's declarations at all.

A refactor was considered and refused for a measured reason rather than
a taste: those six files are deliberately sourceable **standalone in
fixtures**, so hoisting a helper into `wm/` would buy a benign
duplication at the price of a load-order dependency across files that
are sourced several different ways.

So the duplication stays and the **drift** is what gets closed.
`tests/210-kind-folders.t` fails when the catalogue offers a kind no
desktop scan list reads. The two agree today only because every folder
happens to be named after its kind -- a coincidence of naming, not a
construction -- and this is what makes the coincidence enforced.
Verified by adding a tenth kind nothing reads:

    the catalogue offers widgets and no desktop scan list reads hibr/widgets

`mods` is exempt, deliberately: a module is found on the module search
path, which is the shell's and not a scan list of the desktop's. The
check also refuses to pass when it read too few kinds to be reading the
list at all, which is the guard against the check quietly becoming
vacuous.

**And the catalogue's own staleness check gained the direction it never
had.** `./stale` walked `bundles/**` and nothing else, so it could only
ever be wrong about something the catalogue already carried: a pane
added to the desktop and never published was invisible to it, and it
printed *"every bundle matches the tree"* while two panes sat
unpublished. It now walks the tree as well and reports `never`, with the
five deliberate exemptions named and reasoned in place -- the savers'
shared library is not a saver, `strings.txt` is not a language, `xx` and
`xy` are the pseudo-locales `tools/strings.py` writes, and a theme's
picture travels inside that theme's own bundle.

It found `control-panel/screenshots` and `control-panel/wallpaper`,
never published since they were written, and two bundles left behind by
0.99.212 -- `system/appstore.hibr`, which learned to read `where`, and
`lang/ar.json`, which gained two strings. Both are real changes rather
than comments, so both are **version bumps** rather than a `--same`
republish: appstore 1.3 to 1.4, the Arabic catalogue 1.4 to 1.5. That
0.99.212 shipped without running `./stale` first is the miss this new
direction exists to make impossible.

## 0.99.212

**A thing the package ships is one you have, and the catalogue said
nothing about it.**

Gitea #254, reported live:

> *"when I catalogue list and catalogue installed, I see all apps
> installed, but no themes, control panels, and and and ... but they are
> all there? didn't we differentiate between user installed vs system
> installed? where is all that good work?"*

**Nothing was lost.** The ledger was right, the four scopes of ADR 0045
were right, and the desktop was loading everything. Two verbs answered a
question nobody asked and did not say so.

The catalogue is a **ledger of what it placed**. `installed` reads the
ledger; `list`'s mark was "is this name in the ledger". The **shipped
package is never in the ledger** -- nobody placed it, it is the floor
every desktop stands on -- so `list` rendered that invisibility as *you
do not have this*:

    themes/classic    Classic    1.0     <- sitting in /usr/share/hibr/desktop

Four themes, twelve colours, thirty panes, seven savers. One value
answering two questions, the third time this tree has paid for it after
`MLME` (#157) and "default background" (#184). The tell is the same each
time: a name that states a fact rather than answering a question.

**And the Catalogue window inherited it** -- `system/appstore.hibr` drew
its tick from `entries[].installed`, so a shipped theme showed unticked
there too, which is most likely where this was seen.

`ap_where(id, kind, name)` answers where a thing is, out of three
mechanisms that **already existed and `list` never called**: this scope's
ledger, `ap_sysver` for the machine's own, and `ap_shipped` for the
package. **No new glob**, on purpose: a fourth would be a second encoding
of where a kind lives, beside the desktop's own scan lists, which is the
parallel this is avoiding rather than adding to.

- `list` marks `installed`, `system` or `shipped`, and `list -j` carries
  the same `where` field, which the window now reads. `installed`,
  `scope`, `defunct` and `shadows` keep their meanings beside it, so a
  reader written against 0.99.211 still works.
- **`installed` answers the question it is typed to ask**: what do I
  have. It lists everything present, each with its scope -- `user`,
  `system`, `shipped` -- rather than only what the catalogue placed. 21
  entries became 85 on the owner's own machine, the other 64 being the
  package's own panes, themes, colours and savers.
  The first version of this fix left `installed` alone and added a line
  pointing at `list`. That was a consolation prize, not a fix, and the
  owner said so: *"that's not met, that's a half baked solution,
  right?"* -- correctly. A command that answers a different question
  from the one it is asked does not get better by admitting it.
  **Ownership stays exactly where it was**: `remove`, `update` and
  `purge` still act only on what the catalogue placed, and still refuse
  what they did not, which the suite now checks beside the listing. The
  scope column is what carries the difference, instead of absence.
  This also means the person's own listing **shows what an administrator
  installed for the whole machine**, marked `system`, where before it
  was hidden from them entirely.

**What it still cannot see, named rather than left to be discovered:**
`~/.config/hibr/<kind>`, yours-by-hand, which wins over all four. Marking
it needs exactly the new glob this avoids.

`tests/appmgr.py` drives it against a scratch `$HIBR_DESKTOP`, in a scope
of its own so the assertion is about `shipped` and not about whatever the
suite installed earlier. Two of the three checks fail against 0.99.211;
the third passes both ways deliberately, as the arm proving the mark is
not applied indiscriminately.

## 0.99.211

**`examples/` held three products and nine samples; the products move to
`programs/`.**

Gitea #252, split out of #236 whose rename was declined.

    programs/desktop/         the desktop
    programs/catalogue.hibr   the catalogue
    programs/vw.hibr          the vault client
    examples/                 the nine samples, honestly named at last

`make install` already treated them as two kinds of thing -- it places
those three and none of the other nine -- so the folder name had been
lying, and `540-examples.t` linted products and samples in one loop
because they shared a folder rather than because they are alike.

**Nothing user-visible changes.** `DESKDIR` derives from `PREFIX`, never
from the source path, so `make install` into a scratch prefix produces an
identical tree: `bin/{catalogue,desktop,hibr,vw}` and
`share/hibr/desktop/...`, the same before and after. No settings
migration, nothing for a running desktop to notice.

298 path references across 59 files. `CHANGELOG.md` is deliberately left
alone: its entries describe the tree as it was at each release, and
rewriting them would make the record false.

**Three things a straight sweep would have broken**, each caught by
measuring rather than by reading:

- `850-lint.t` would have gone from **141 files to 8** -- `find
  "$root/examples"` would have linted the nine samples and silently
  stopped linting the whole desktop. Its own `n > 60` floor would have
  failed the build, which is what such a floor is for.
- `540-examples.t` would have stopped linting `vw.hibr` and
  `catalogue.hibr`, which `examples/*.hibr` used to catch.
- **The path has two spellings**: `os.path.join(ROOT, "examples",
  "desktop")` in four test files is invisible to a search for
  `examples/desktop`.

And a measurement that had to be corrected: 68 published bundles appeared
to have changed, which was a `git mv` artifact. Comparing **content**
rather than paths, 9 files changed and 5 of them are published bundles,
each by exactly one comment line naming the path.

## 0.99.210

**`cd` in a strict function is the shell's own bookkeeping, not a global
the script created.**

Gitea #251, split out of #229, where it was four lines in the owner's
own `desktop.log`:

    hibr: .../apps/Office/sheet.hibr:685: OLDPWD is not declared
          -- local OLDPWD, or declare -g OLDPWD (strict vars)     (x4)

`v_shellvar` is the list of names the shell manages for a script, and
`strict vars` passes every one of them: `RET`, `M`, `BASH_REMATCH`,
`PIPESTATUS`, `REPLY`, `OPTARG`, `MAPFILE` and five more. **`PWD` and
`OLDPWD` were not on it**, and `cd` and `pushd` set both.

It does not merely log. The refusal **fails the `cd`**, so

    out=$(cd "$tmp" && env -i ... "$HIBR" ... )

short-circuits and `out` comes back **empty** -- and that is how
`ss_recalc` runs every formula in Sheet. On any desktop whose session
had no `OLDPWD`, **every formula in the spreadsheet was empty, always**,
with the reason only in a log. A desktop started from the **login
screen** has none: PAM makes a fresh environment, and `OLDPWD` is
something a shell's own `cd` creates.

Measured, with the name stripped:

    cd:    []        before          cd:    [ran]     after
    pushd: []                        pushd: [ran]

and `getopts a v` still refuses `v`, so the exemption corrected strict
vars rather than blunting it. A sweep of the rest -- `source`, `read`,
`=~`, `&`, `SECONDS`, `LINENO`, `PIPESTATUS` -- found them all already
exempt, so these two were the whole family.

**Why nothing caught it.** Every suite inherits the shell it was started
from, and a developer's shell has `OLDPWD`; `Term`'s `env` could only
ever *add* a name, so a bug that needs one to be **absent** was
untestable. It takes `None` to remove one now, and `tests/apps.py`'s
Sheet section works a formula out in a session that has none.
`tests/209-strict-cd.t` records `cd` and `pushd` in a strict function
beside a global the script really does create, and fails against
0.99.209.

**And a test the gate caught on the way**, unrelated to the fix:
`936-hold-upgrade` named its held session `v` and found the server by
scanning the whole of `/proc` for a `comm` of `hold:v`, keeping the
**last** match. No `TMPDIR` of its own can narrow a scan of `/proc`, the
server outlives a failed run for its own `sleep 60`, and `/proc/[0-9]*`
globs as *text* -- so the last match is the lexicographically greatest
rather than the newest, and pid 998000 beats pid 1002000. It then
measures the wrong process and says `nothing was replaced [0]`, which
reads as `hold upgrade` being broken. The session is named after the
process now, and the scan stops at its own: **3 of 4 concurrent copies
fail with the old name, 4 of 4 pass with the new**, and the recording is
byte-identical, which is what says the conversion changed nothing but
the hazard. Whether that is what failed the gate is **not** proven --
only that the weakness is real and is gone.

## 0.99.209

**Pasting into a terminal window does something.**

Gitea #250, split out of #229, where it was ten lines in the owner's own
`desktop.log`:

    hibr: term_paste: missing argument op                     (x6)

`<app>_paste` was called from two places with **different argument
counts**: `wm/files.hibr`'s Copy/Cut/Paste path passes three -- the text
and, third, `cut` or `copy`, which is what its own comment says the
contract is -- and `wm/input.hibr`, where a **bracketed paste from the
host terminal** arrives, passed two.

Twenty-two of the twenty-three `_paste` callbacks survive that, because
they are declared `op = ""`. **`term_paste` was the one with a bare
`op`** -- and a declared function refuses an argument it is missing
without a default, so its body never ran. Pasting into a terminal
window did nothing at all, silently, and the terminal is the thing
anybody actually pastes into.

Both halves are fixed, because the contract should be kept by the caller
*and* no callback should be the only one of twenty-three that cannot
take the short form: `wm/input.hibr` passes `copy` as the third argument
(the line above it has just said so to `dt_clipset`), and `term_paste`
takes `op = ""`.

`tests/apps.py` drives it, in the terminal section, next to the check
that typing reaches the shell -- Stickies, Files and the calculator were
already driven with a bracketed paste and the terminal was not. It fails
against 0.99.208 **twice**: the paste check, and "no session printed a
shell error" catching the refusal in the log.

`tests/540-examples.t` gained `paste`, `copy` and `cut` in its callback
arity table, which had never named them; with it, a `_paste` that names
two of three fails the build.

## 0.99.208

**A write to a peer that has gone no longer kills the desktop.**

Gitea #229. Two lines in the owner's own `desktop.log`, there the whole
time:

    desktop: the desktop died on signal 13 after 93205s
    desktop: the desktop died on signal 13 after 86122s

**Signal 13 is SIGPIPE**, and its default ends the process -- which for
the desktop is every window and every terminal's shell with it. The
supervisor then starts it again, which is what "it crashes multiple
times when being used" looks like from outside.

Reproduced in six lines, against a server shaped exactly like
`wm/ctl.hibr`: a client connects, sends a request, and gives up before
the answer lands.

    reply through a redirection   server exit 141   (killed by SIGPIPE)
    reply through `send`          server exit 0     ("status 1", carries on)

So **the desktop's control socket could be killed by any client that
gave up mid-answer** -- and `desktop ctl` is what Blit, `catalogue
remove` and the test harness all use.

`hibr_wrall` is the fix: write everything to a descriptor the caller
owns, with SIGPIPE held off for that write and a pending one drained
before the mask goes back, so nothing is left for a later wait to find.
A socket is told per send with `MSG_NOSIGNAL`; anything else has the
signal blocked. The **disposition is never changed**, because an ignored
signal survives `exec` and every program the desktop starts would
inherit it -- `yes | head` would stop ending.

Now used by `send` (so `wm/ctl.hibr` answers that way rather than with a
redirection), the TLS relay, `mods/web`'s pipe to Chromium and its tap's
pipes to the player, `mods/http`'s request and `mods/dav`'s `dv_sink`.
Each already had the `EPIPE` handling written around it; each was
correct and unreachable, because the signal fired first.

**What is deliberately unchanged**, and measured to be so: an ordinary
redirection still raises the signal, so `hibr ... | head -3` still ends
with 141 and a pipeline stage still dies when its reader goes.
`tests/208-send-epipe.t` records all of it side by side, and fails
against 0.99.207.

**What is not claimed.** The report that prompted this ticket was the
Browser and the YouTube player being unstable, and that attribution is
**not** confirmed. A probe that kills Chromium under a live page and
then drives six `web` verbs survives on 0.99.207 as well as on this
release -- every one of those verbs asks `wb_alive()` first, which closes
the pipe before anything writes to it. What is left in `mods/web` is a
real race (`wb_req` checks only `wb.in < 0`, so a browser that dies
between the check and the write still reaches it) and the tap's writes
to the *player's* pipes, which ask nothing at all. Both are covered by
construction. The control socket is the path that is proven.

`tests/desktop.py` gained the end-to-end check: a client that
`shutdown(SHUT_RD)`s -- so the request stays readable and the answer has
nowhere to land, which makes it certain rather than a race -- leaves the
desktop alive and answering. It fails against 0.99.207, and takes the
rest of that section with it, because the desktop is gone.

`hibr_wrall` is additive, so `HIBR_ABI` has not moved; the package ships
the shell and its modules together, as it did for `hibr_schemef`.

## 0.99.207

**A bitmap is never committed on a guess.**

Gitea #249, reported live: a desktop resumed with `-r` from a kitty
session onto a plain xterm had a **black wallpaper for the whole
session**, with the window shadows still in colour, and About This
Computer saying `xterm-kitty ... pictures blocks only, no pixels`.

Both halves were telling the truth about different moments. `cn_gfx`
decides the picture protocol from `HIBR_GFX`, then from what the
terminal **answered**, then from a **list of terminal names** -- and a
held session's `$TERM` is the one the shell that ran `hold new` had,
frozen for the session's life. So in the window before the terminal's
reply arrives, the stale `xterm-kitty` wins the name list and a kitty
bitmap goes to a terminal that draws none. Measured, holding a session
named `xterm-kitty` and attaching a client that answers device
attributes without sixel and never answers kitty's query:

    gfx at once: kitty 6 13
    gfx later  : none 6 13
    kitty payloads the xterm was sent: 1

One is enough. The console claims the cells that region covers and skips
them in the diff, the xterm drops the payload, and an under-text region
is kept for as long as its caller places it again (ADR 0037) -- which a
wallpaper does on every frame. Nothing ever repaints those cells.

Every symptom follows from it: the shadows are ordinary cells and
outside the region; half blocks cure it because forcing cells changes
`dt_wallsig` and drops the region; and About asks *later*, after the
answer has arrived.

`cn_gfxsure` is the state that was missing -- whether the protocol
`cn_gfx` names was **answered** or **forced with `HIBR_GFX`**, as
against read off a name. `cn_image` refuses while it is not sure, and
`img draw` falls through to the cells path every caller of `dp->image`
already keeps. A frame or two of half blocks at startup costs nothing; a
wallpaper that is black until the session ends costs everything.

`console gfx` is unchanged: it is asked what a terminal most likely
draws, and a name is a fair answer to that. What changed is only what
may be put on the wire.

`tests/holdpix.py` gained the check, beside the two capability checks
from #216: zero `a=T` payloads at a client that answered it draws
neither, with the picture placed on *every* pass so the window cannot be
missed. 22 passed and 1 failed against 0.99.206; 23 and 0 with the fix.

The same family as #112, #118, #165, #172, #175, #184, #215 and #216 --
and #216 most of all, which made the capabilities be asked and left the
answer's own latency unexamined. **Asking is not knowing, and the gap
between them is a state the code has to have a name for.**

## 0.99.206

**One wallpaper per display, so a stacked union has one at all.**

Gitea #238, which is #240 part 3. hold forwards a picture only to a
client whose rectangle **contains** it, because a bitmap cannot be
clipped -- so a wallpaper drawn across a stacked union is held whole by
neither client. Measured against 0.99.205, primary below a join:

    primary (lower, rows 10+)  images: 0
    joined  (upper, rows 0-9)  images: 0

and after:

    primary  images: 1
    joined   images: 1

Worse than the ticket claimed, which was that the *join* was missing the
wallpaper: at a stacked union **neither** display had one.

**The rule is kept rather than reversed.** The ticket asked hold to clip
per client; the owner chose the other answer, and it is the better one.
A picture fitted to each display is contained by its own client **by
construction**, so nothing is clipped anywhere and `render.c`'s
containment test is right as written. Clipping in hold would have put
image encoding inside its renderer, which has never done any: a sixel is
paint with no placement and must be re-encoded per client, and while the
kitty protocol can crop a placement, `mods/console/kitty.c` emits no such
parameters -- new encoder code either way, plus a per-client image-id
space.

`DT_DISPLAYS` carries every client's rectangle, filled by `dt_size`
beside the primary -- `hold clients` is a round trip and a frame must not
pay for one, and a client attaching or leaving is a resize, which is when
`dt_size` runs. `dt_wallfit` takes a display rectangle now instead of
reading `DT_ROWS`/`DT_COLS`, and `dt_wall` loops. A desktop with one
display places exactly one picture, as before.

Two things found on the way. `dt_size`'s loop **broke** on finding the
primary, which was right while the primary was all anybody read and
silently drops every display after it; it collects them all now. And the
version before this cached the fit on "the image, the mode and the screen
size" -- one answer, which two displays would have thrashed. The
expensive part is `img size`, a full decode to learn two numbers, and
that depends on the **image alone**; it is cached on the path and the
per-display arithmetic is free.

**The pixel cache grows.** `mods/img` kept one entry on purpose, because
a screenful is 6.3 MB where a cell grid is 130 kB. One is wrong the
moment a desktop spans two displays: they alternate and miss every time,
which is the 330 ms resample the cache exists to stop. It is a slot per
`(path, w, h)` actually being placed now, and a slot nothing has asked
for in `IM_PIXIDLE` calls is freed -- "one per display in use" without a
number anybody has to pick, falling back to one on every desktop that is
not stacked. The cost is named rather than hidden: 6.3 MB per
screen-sized display, paid only while that display is attached and
placing.

And the module gained a finaliser. It had none, so both caches leaked on
`mod drop`; a fixed array leaking its contents is one thing, and a
growing allocation introduced here is worth freeing rather than matching
the old habit of.

`tests/desktop.py` goes 613 checks to 614. The new one needs `HIBR_GFX`
and a cell size, without which the console answers "no pictures" and both
counts are 0 for a reason that has nothing to do with this -- which is
how the first version of it passed against the fix and the control
alike.

## 0.99.205

**The primary display is a rectangle, not a column.**

Gitea #240, part 2, asked for by a display server that stacks two panes
with the primary **below** a join. `hold clients` has answered `name row
col rows cols primary` all along, and `dt_size` read that line, kept the
column and the width, and **threw the row and the height away**. Every
placement in the desktop then began at a literal row 1 and ran to
`DT_ROWS`, however far down the union the primary actually started.

So a stacked arrangement could not work, and the measurement is blunt.
With the primary occupying union rows 10 onward, against 0.99.204:

    DT_PRIMARY_ROW  does not exist
    bar pane        [0 80]        <- union row 0, inside the join
    bar on the primary: none

and with the row kept:

    DT_PRIMARY_ROW  10
    bar pane        [10 0 80]
    bar on the primary: its own row 0

The bar was being drawn on somebody else's display and the primary
showed none at all.

`DT_PRIMARY_ROW` and `DT_PRIMARY_H` join the two that were already
there, and `dt_ptop` and `dt_pbot` are what anything placing something
reaches for -- the primary's first content row, and one past its last.
Fifteen call sites across thirteen files stopped assuming the top of the
union: the bar's pane and its shadow, window placement and tiling,
`dt_place`'s centring, the confirm box and the screenshot chooser
(centred on the primary in **both** directions now -- half of `DT_ROWS`
is the middle of another display when the primary is a stacked pane),
the icon grid and its clamp, the Control Strip's "80% down" and its
clamp, menu drops and their two `dy` fallbacks, and the wallpaper's
cache key.

Nothing changes for a desktop that is not part of a stacked union: the
primary is the whole screen at the origin, `dt_ptop` is 1 and `dt_pbot`
is `DT_ROWS`, exactly as the literals were. The 612 existing desktop
checks pass unchanged.

The new check is worth its own note, because building it found two
things that had nothing to do with the fix and would have wasted a day
each. A copy of `session.hibr` outside the desktop tree dies instantly,
because it finds the window manager with `d=${0%/*}`. And a held desktop
whose session name does not match what `hold new` called it **re-execs
into a session of its own**: `dt_autohold` compares `HIBR_HOLD`'s
basename against the name it was asked for, so the client is left
watching the session the desktop walked out of -- which looks exactly
like a desktop that will not draw. `--session` has to match, and the
check says so where somebody will read it.

`tests/desktop.py` goes 612 checks to 613; the new one fails against
0.99.204.

## 0.99.204

**The last two from the catalogue audit: a reason that could go stale,
and a fetch that could go anywhere unsaid.**

**#246 -- `installed` named a needer that may have gone.** A module
fetched to satisfy somebody's `needs` records why, and 0.99.199 started
displaying it as *"automatic: needed by apps/contacts"*. That name is
written once by `ap_getmod` and never updated: with two apps needing one
module -- the second finds it already loadable and fetches nothing --
removing the first leaves the line naming an app that is no longer
installed.

It says `automatic, not asked for by name` now, which is true whatever
happens afterwards. Making `auto` a maintained *list* of needers was the
other option and is the one this project has already argued against in as
many words: *"prefer a count the console takes to a flag each caller
sets: the list of overlays is the thing nobody can be relied on to
maintain."* `catalogue purge` answers the question properly instead,
computing it from each installed entry's own `modules` at the moment it
matters -- and it may still name the app, because it lists only what
nothing needs **now**, so "was needed by" is past tense about one that
really did.

Nothing was ever removed wrongly: `ap_wanted` has always read the index
rather than this field, so only the line was false.

**#247 -- a bundle could be fetched from any host, over plaintext, with
nothing said.** `ap_bundleurl` hands back an absolute address verbatim,
so an index may send the fetch anywhere -- while `ap_setindex` warns when
the **index's own** address is `http://`. The two halves of one question
were treated differently.

Said once per run, naming the host, when it is not the host the index
came from or when it is plaintext. It is a warning and not a refusal, and
the message carries the reason: the `sha256` is in the index, the index
is signed, so what arrives is verified wherever it came from. What an
absolute address costs is privacy -- a host you did not choose learns
your address and what you install -- and availability. A mirror
legitimately gives absolute addresses, so refusing them would break the
thing the relative form exists to make easy.

`tests/appmgr.py` goes 121 checks to 127; four of the six fail against
0.99.203, and the two that do not are the guards that the new behaviour
does not over-reach.

Three detours worth recording, all the same shape -- a check that passed
for the wrong reason. The host checks first rewrote the **signed**
`index221.json`, which broke its signature and took five unrelated checks
with it. Moved to the ordinary index, they then read a **cached** one, so
the install used the old relative address and the check passed for having
changed nothing. And the move itself cut at the first `remove demo` of
three, carrying a third of the block and silently dropping two checks --
caught only because the suite counts what it plans against what it made.

## 0.99.203

**`show` had no check at all, and it is the verb the trust rests on.**

Gitea #248, from the catalogue audit. `tests/appmgr.py` drove `install`
46 times, `installed` 25 and `remove` 16; it drove `show` **zero** times,
`refresh` zero and `help` zero.

`show` fetches a bundle, verifies it and prints it **without placing
anything** -- it is how somebody reads a script before running it, which
is the whole of ADR 0041's promise that an app is a script you can read
first. Nothing would have caught it breaking.

Seven checks: it prints the bundle's own bytes; it places nothing, which
is the point; a name the index does not have fails rather than printing
nothing and succeeding; and **a bundle whose bytes are not what the index
promised is refused rather than printed**, which is the property that
makes reading-first worth anything. Plus `refresh` and `help`.

Nothing changed in the manager: these are checks for behaviour that was
already correct, so unlike the rest of the audit they do not fail against
the release before. What they buy is that it stays correct.

The tamper check is worth a note, because the first version passed for
the wrong reason. It rewrote `bundles/demo.hibr` -- and an earlier
section of the suite repoints `demo` at `demo-2.hibr` to drive `update`,
so it was tampering with a file `show` no longer fetches and watching it
print happily. It resolves the bundle from the **served index** now, and
the transition from that false pass to a real refusal is what says the
check has teeth.

Also here: `lang/ar.json` to 1.4 and `system/appstore.hibr` to 1.3, the
version bumps 0.99.202 owed the catalogue. `./stale` is what said so --
and it said so only because this same pass taught it to notice, which is
the other half of the audit (#245, fixed in the catalogue repository: an
app found by name matches in any folder, so comparing bytes alone could
not see one that had **moved** between submenus while the index went on
naming the folder it had left).

## 0.99.202

**The Catalogue window shows what the command knows.**

Gitea #244, from the catalogue audit. `catalogue installed -j` has
answered `defunct`, `shadows`, `scope` and `paths` since 0.99.197, and
the window read **none** of them -- grep found zero occurrences of each
in `appstore.hibr`. The JSON was built for it: `ap_installed`'s own
comment on `paths` says it is *"what a window needs to say what removing
it would take away"*, and the window never asked.

So in the window a **defunct** copy -- one the machine also has, read
before yours, which therefore can never run -- looked exactly like a
working app; one that **shadows** the shipped copy was unmarked; and
removing an app took its Control Panel pane with it with nothing having
said so. Two views over one state that disagree about what is true, which
is #237 one level up.

The window reads `list -j` rather than `installed -j`, so the fields went
there: `defunct`, `shadows`, `scope` and the list of `parts`, computed
only for an entry that is actually installed -- `ap_shipped` globs the
desktop's folders and there is no sense paying that for the eighty an
entry the person does not have. An entry now wears **⚠** where it wore a
tick, and the detail pane says *"shadows the one hibr ships"* and
*"2 files with it"*.

**Showing only, deliberately.** Acting at system scope from the window is
a privilege question rather than a display one -- the window runs as the
person and an install for the whole machine needs root -- so it stays
`sudo catalogue --system install` at a terminal, which is what an image
build does anyway.

Two things in the suite are worth keeping. The new marks are read off the
screen rather than spelled out, because the glyph set is the person's
choice (`DT_GLYPHSET`) and a check that writes `⚠` in is a check of the
default. And the fold-count check next door, which asserted a literal
`Applications (2)`, broke the moment the fixture grew an entry -- it
counts the fixture now, which is the same brittleness one level along.

`tests/apps.py` goes 625 checks to 627, both of which fail against
0.99.201.

## 0.99.201

**`catalogue check`: the record against the files, both ways.**

Gitea #243, from the catalogue audit. Everything the manager offered read
the record and believed it, so the two ways a scope can be wrong were
both invisible: a path the record claims that is not on disk, and a file
in the scope that nothing claims. The second is what 0.99.200 fixed the
causes of -- a damaged record, and one install erasing another's entry --
and the person either had happened to had no way to see it.

    /home/you/.local/share/hibr
      missing  apps/beta.hibr     claimed by apps/beta
      orphan   apps/alpha.hibr    is apps/alpha, which the record has lost
      orphan   apps/mine.hibr     nothing in the index matches it

    1 claimed and missing, 2 present and unclaimed, of 4 files

It exits non-zero when anything is wrong, and **needs no network**: the
comparison is the record against the files, and the index is consulted
only to recognise an orphan and only when it is already cached. A check
that could not run without the catalogue would be useless on exactly the
machine something had gone wrong on.

An orphan is recognised by its `sha256`. A catalogue of plain files --
what `addapp` publishes -- stores the installed file itself, so its hash
names the entry it came from; a tarball's hash is the tarball's, so an
orphan from one is reported as unknown rather than guessed at.

**`--repair` makes the record true and never touches a file.** It drops a
claim on a file that has gone and takes back one it can recognise, which
is the whole of recovering from a lost update: the files were always
right, it was the record that forgot them. Taking an entry back claims
its parts too, so a whole install comes back whole -- measured on an
entry with a Control Panel pane, where `installed` showed `+1 part` again
and `owns` resolved the pane to its owner.

One rule fell out of building it and is worth stating, because the first
version got it wrong. **An entry's own file is what says it is
installed.** An entry whose own file has gone is dropped even when a part
of it is still there -- a pane on its own is not an app -- and the next
check names that pane as an orphan, rather than keeping a dead entry
alive on the strength of it. Nothing loops, because taking an entry back
needs its own file present.

What it will not do is delete. A file nothing recognises may be one
somebody wrote, and `check` has already said where it is.

This also makes 0.99.199's `21 entries, 30 files` honest. That line counts
what the record claims, which equals the disk only while nothing has
drifted; `check` is what turns it into something anybody can verify in
one command.

`tests/appmgr.py` goes 106 checks to 115.

## 0.99.200

**One manager writes at a time, and the record is replaced rather than
rewritten.**

Gitea #242 and #241, both from the end-to-end audit of the catalogue the
owner asked for. They are one write path and are fixed together: a lock
without an atomic write still truncates, and an atomic write without a
lock still loses the update.

**#242 -- two installs at once kept only one of them.** Each run read the
whole record at its start and wrote the whole record back at its end,
with no lock: a plain read-modify-write race. Both placed their files and
whichever finished last erased the other's entry, leaving a file on disk
that `installed` could not see and `remove` could not take away.

Measured rather than argued, on two installs started 0.3 s apart:
**10 rounds out of 10 lost one** before the fix and **0 out of 10** after.

A verb that writes now takes a lock **before** it reads, which is the
part that matters -- taken after the read it would serialise the writes
and lose the update all the same. The mutex is a directory, because
`mkdir` either creates or fails in one syscall and leaves no gap to race
in. A lock whose owner has died is taken over and says so; one whose
owner is alive is waited for and then named, rather than written over.
`AP_LOCKWAIT` is the wait, ninety seconds by default since an install
holds the lock while it fetches, and it is read from the environment so
it can be turned down. **Reading takes no lock and never waits**:
`list`, `installed`, `info` and `owns` answer while an install runs,
which the rename is what makes safe.

**#241 -- a crash during a write erased the whole record, silently.** The
state was written with `printf > "$AP_STATE"`, which truncates in place,
and read with `json parse ... || AP_ST=()`. So a Ctrl-C, a crash or a
full disk in that moment left a partial file that the next run reported
as *nothing installed*, with exit 0, while every installed file was still
on disk and owned by nobody. Recovering meant installing everything
again, and nothing anywhere said so.

It is written to a temporary file beside the real one and renamed over
it, so what is on disk is always the whole of one record or the whole of
the other. And the silent half is gone too: a record that will not parse
is **damaged**, which is a different answer from an empty scope, and it
now refuses, changes nothing, and says what to do about it. An empty file
is still an empty scope, which is what a fresh account has -- a check that
could not tell those apart would stop anybody installing anything.

The settings file is written the same way, for the same reason: half of
it would lose the index address and let the default quietly take over.

`tests/appmgr.py` goes 100 checks to 106, and five of the six fail
against 0.99.199 -- including the race, run four rounds per suite run.
The sixth is the guard that the new refusal does not over-reach, and it
passes against both, as it should. The suite grew a runner for several
managers at once, which is the only way the race can be driven at all.

## 0.99.199

**Twenty-one entries, thirty files, and nothing said where the other nine
came from.**

Gitea #237 and #239, both reported by the owner looking at their own
machine: *"check what's installed in the catalogue, it's untrue, themes,
control panels, apps are all installed? ... do we have a mark that does
what's been installed explicitly vs what's automatically installed?"*

No theme was installed. What they saw was
`~/.local/share/hibr/control-panel/` with eight files in it, and inferred
that Control Panel panes had been installed as entries. They are `part`s,
brought by the apps that own them -- and the state knew exactly that:
walking the scope and resolving every path against it found **30 of 30
claimed, 0 orphaned**. The bookkeeping was right and the only
human-readable view of it answered a different question from the one the
folder raises.

Three things can put a file in a scope -- you asked for it, an app brought
it, something needed it -- and `installed` showed the first alone. So it
says what each entry brought, and ends with the two numbers that let a
person check the folder against the command:

    apps/prayer      1.0        apps           user    +2 parts
    apps/snake       1.0        apps           user

    21 entries, 30 files

A module fetched for somebody's `needs` says whose on the same line, which
finally shows the `auto` mark that `purge` has read since it was written
and nothing ever displayed. `info` gains a `brings` row, from the index
where the catalogue is one of plain files -- so it answers *before* an
install, which is the more useful moment -- and from the state for a
tarball, whose parts nothing can read without fetching it.

And `catalogue owns PATH` answers the other direction, which is the
question actually asked and which nothing could answer before short of
reading `apps.json` by hand:

    control-panel/pimset.hibr
      a part of apps/calendar 1.0, in your own scope
      a part of apps/contacts 1.1, in your own scope
      kept until every one of those is removed

It names every owner of a shared part, because removing any single one of
them leaves it where it is; and it answers for a path in any of the four
scopes, since the same name can be in all of them with one of them running.

**#239: `--system` without root said `mkdir: cannot create directory
'/var/lib/hibr/apps': Permission denied`.** The symptom rather than the
thing to do about it, and from the middle of the work -- after the index
had been fetched and a bundle verified. An ordinary account asking for
`--system` is the *expected* way to find out it needs root, since ADR 0045
makes that directory root-owned on purpose, so it is answered with the
command they meant to type. Reading stays ungated: `--system list` and
`--system installed` are what somebody runs before reaching for sudo, so
the check is on the verbs that write and nowhere else.

Two things found while checking it, both pre-existing. `info` never showed
the `installed` row for a catalogue serving only the older `apps` section,
because the state is keyed by the canonical `kind/name` and a resolved
name there is the bare one -- three lookups in that function asked with the
wrong key. And the parts count cannot be `paths` minus one: a bundle
carrying resources of its own records its resource **folder** last, which
is a directory rather than a part.

`tests/appmgr.py` goes 88 checks to 100. The totals are checked against the
JSON the Catalogue window reads rather than against literals, since the
suite has other entries installed by the time that section runs and a
hard-coded total would be a check of whatever ran before it.

## 0.99.198

**A module ships because it is bytes; the split is refused.**

Gitea #234 §1, reversed before a single module was published, by one
question from the owner: *"why do we want to support divergent paths,
meaning deb installs behave different than brew, than aur? it's clunky
or am I missing something?"*

They were not missing anything. The numbers that argued for publishing
twenty of hibr's own modules were right — 1247 kB, 37% of the package,
and 1.75 ms off the first unknown command. What was never priced is
uniformity, and checking rather than assuming is what settles it: the
Homebrew formula has no `bottle` block and its `install` runs `make`,
the AUR `PKGBUILD` runs `make`, and `make install` places every module
the build produced. **A module is bytes, and a source install compiles
its own.** So a Mac and an Arch box already hold all thirty-five before
a catalogue is ever asked, and only the deb would have fetched.

That leaves three outcomes and no fourth: **diverge** — the same
`catalogue install contacts` doing different work depending on how hibr
arrived, and every report beginning with "how did you install it?";
**chore** — the owner compiling on their Mac every release; or **do not
split**. There was also a blocker already recorded as a "last step"
rather than as a wall: a build per architecture needs hardware, a cross
toolchain or an emulator, and this project has none of the three.

**The rule that replaces it draws the line at the file's own kind.** A
script, a theme, a colour scheme and a language are arch-free, so they
are published. A module is not, so it ships — all of them, in every
packaging. There is no list of fifteen to keep in step with what the
desktop needs this month, no per-architecture matrix for this project
to own, and no new release rule: an ABI bump rebuilds the package and
the package carries the modules, so the two cannot drift apart.

The `mods` kind stays, for **somebody else's** module, which is what it
was for before any of hibr's were considered. The per-architecture
`builds` schema in the index is kept with it, because a third party
with two machines has exactly the problem it solves.

One planned piece of work got cheaper rather than dearer. Moving the
desktop itself into the catalogue was costed in ADR 0045 at "the
per-architecture matrix grows from twenty modules to about thirty".
Under this rule that matrix is **empty**: the desktop's `console hold
pty term img lang uni hcal salat media auth` ship with hibr like every
other module, and only its scripts are published.

The residual cost is named rather than hidden. The 1.75 ms stays, and
its remedy is to make `m_bixbuild` cheap — an index cached against the
module directory's own mtime instead of `dlopen`ing thirty-five objects
to rebuild what did not change — which helps every configuration,
including a machine carrying third-party modules, where shipping fewer
of ours would not have helped at all. Not built here.

No code changed. ADR 0045's Decision, Consequences and Alternatives are
rewritten with the measurements kept and the reversal visible, CLAUDE.md
records the scan order and the `mods` rule, and the Control Panel pane
and the Control Panel itself go to 1.2 — 0.99.197 changed both and the
version bump the catalogue needs to offer an update was owed.

## 0.99.197

**One copy for everybody: the system scope.**

Gitea #234, ADR 0045. Until now a catalogue install went into
`$XDG_DATA_HOME` and nowhere else, so a machine where the desktop *is*
the system -- Blit's images, a shared workstation -- had to install
twenty-one applications per account or seed a skeleton and hope.
`catalogue --system install apps/term apps/files` does it once:

    yours            $XDG_DATA_HOME/hibr          catalogue install
    the machine's    $HIBR_SYSDIR, /var/lib/hibr  catalogue --system, root
    shipped          $PREFIX/share/hibr/desktop   the package

read in that order, with `~/.config/hibr` above all three as yours by
hand. **An administrator's install is authoritative**: it is read
before anybody's own, so a copy of yours cannot override it.

That order was written the other way round first -- a person's own
scope above the machine's, by analogy with the config-then-data chain
hibr already had -- and it is wrong for the machine this exists for.
On a managed desktop it means somebody who installed Mail last year
goes on running last year's copy after the administrator installs a
current one: their folder is read first, the update never reaches
them, and nothing says why. The scope exists so an administrator can
decide what the machine runs; an order that lets any account quietly
opt out does not deliver that.

What a person keeps is `~/.config/hibr`, above everything, and it is
not the same thing: a file you **wrote**, not a copy of somebody
else's, which is where patching an installed app has always happened
and which no update overwrites.

Two things follow. Installing into your own scope something the
machine already has is **refused**, whatever the versions say -- a
newer copy in a folder nothing reads is worth less than no copy at
all. And a copy that was already yours when the administrator
installed theirs is reported as **defunct** rather than merely
shadowing, with the command that frees the space, since that is the
one way into the state and nothing else would mention it.

`/var/lib` is deliberately not under the install prefix: `make install`
does `rm -rf $(DESTDIR)$(DESKDIR)`, so a system scope there would be
deleted by the next source install, and `$(MODDIR)` is the package's --
a catalogue writing into it would put two package managers in one
directory.

**One variable, not two, and that is the part worth reading.** A script
needs to know where the system scope is and the module search needs to
look there, and writing `/var/lib/hibr` into both is exactly the
parallel ADR 0045 exists to refuse. So `$HIBR_SYSDIR` is both: the
shell sets it at startup from the compiled value unless the environment
already did, `catalogue --system` reads it to know where to write, and
`m_sysdir` reads it to know where to look. What makes one variable safe
for both jobs is a single rule -- **the default is checked and a path
you chose is yours**. The compiled path is used only when it is
root-owned and writable by nobody else; an overridden one is trusted
exactly as `$HIBR_MODPATH` already is, because it reaches only the
shell whose environment holds it; and **root never reads the
environment's at all**, which is what keeps this from widening ADR 0016
into a way in. Measured in all four states, including a `chmod 777` on
the compiled path, which is refused.

The desktop's **eight** scan lists -- applications, the desktop's own
apps, Control Panel panes, Control Strip modules, savers, colour
schemes, themes and languages -- ask `dt_sysdir` for the folder rather
than writing the path out eight times, and each is built with the
machine's entry in the right place rather than appended to, since
appending puts it last where nothing reads it.

That call is guarded the way every app already guards `dt_app`
(`command -v dt_sysdir > /dev/null && ...`), which the gate insisted
on rather than a reading: a pane or a saver sourced **on its own** in
a fixture has no window manager behind it, and the first version died
with `dt_sysdir: command not found` in a check about something else
entirely.

**Two things are stricter at system scope**, both for one reason: the
people who will run what you install are not the person who read it.
An **unsigned catalogue is refused**, where for your own scope it is a
warning -- ADR 0041's "you can read it before you run it" is an
argument about your own account and does not survive everybody's. And
a module directory must be **root-owned and private** or the shell does
not treat it as a system scope at all.

`catalogue installed` names the scope rather than leaving it to be read
off a path, because `/var/lib` is not where anybody would look -- the
price of the choice, named where it is paid. `catalogue --system purge`
says that it reads the system scope only and cannot see what somebody
has installed in their own home: a real limit rather than a safe one,
so it is stated rather than implied.

`make install` creates the root at 0755, which is where the ownership
the module search insists on comes from; `make uninstall` removes it
only if it is empty, since what a catalogue put there is an
administrator's and not the Makefile's to delete.

## 0.99.196

**Four defects in 0.99.195, three of them found by using it.**

**An app that moved never moved.** `dir` shipped in 0.99.195 and put
every application in the folder it belongs to -- and `catalogue update`
compared **versions**, so for anybody who already had the applications
installed it said *"everything is up to date"* and left all twenty-one
flat. The one thing #233 was for reached nobody who was already here.
Found by upgrading this machine and looking, not by a test.

`ap_update` asks the question the feature is about: *where would this
land now, and is that where the state says it is.* A path, not a
version, so nothing has to be kept in step -- and an entry whose
recorded paths predate 0.99.183 has none to compare, which reinstalls
it once and records them.

**And the fix had a worse defect inside it.** `ap_puttar`, the path for
a bundle that is a tarball, placed flat and recorded flat. Give such an
entry a `dir` and the new check would see it in the wrong place,
reinstall it, place it flat again, and report *"moves to"* on **every
update, for ever**. No catalogue serves tarballs today, which is
exactly why it would have sat there until one did. It honours the
folder now, like every other placement path.

**`catalogue purge -y` could hang where it had nothing to ask.**
`ap_remove` calls `ap_windows` -- which asks a running desktop over its
control socket -- *before* testing `-f`, so under force the answer was
computed and thrown away. A desktop wedged mid-request is a connect
that neither fails nor completes (#180), so for `purge -y` in an image
build the only possible effect of that question was to hang. It is
asked only when the answer can change what happens.

**A module that declared needs was silently ignored.** `mkindex` writes
no `modules` for a module -- what a module needs is `hibr_require`'s
business, resolved when it loads -- so the field is empty for every
entry this catalogue publishes. A hand-written index could say
otherwise, and `ap_getmod` collected the answer and dropped it on the
floor: the module installed, the thing it asked for did not, and
nothing said so. Refusing by name is the honest answer of the three,
since installing would mean recursion with no bound.

Nothing else changed. `tests/appmgr.py` is 83 checks.

## 0.99.195

**What an app needs, where it lands, and what the command is called.**

Gitea #234, #233 and #235, which are one question asked four ways:
*how do I install Contacts, with its Control Panel pane and maybe a
module?*

**`apps` is `catalogue`.** The window has been called Catalogue since
0.99.192 and the command was still `apps`; one thing with two names is
the parallel this release exists to remove. A clean break, no alias --
with two things done so the break does not cut anybody. `make install`
removes a `bin/apps` it finds, because dpkg takes a file the new package
no longer ships and a **source** upgrade does not, which would otherwise
leave a command that exists and fails. And the postinst names both
names once: 0.99.194's own postinst printed `apps install apps/`, so
anybody upgrading from it has that sentence in their scrollback.

**An entry says what it needs.** A `# modules:` line in a bundle's own
header, beside the `# part:` lines, carried into the index and resolved
at install: already loadable -> satisfied; in this catalogue as
`mods/NAME` -> fetched **before** the thing that needs it, so an app is
never on disk in a state where its first `need` fails; neither -> the
app is refused by name and nothing is placed.

The field it fills is `modules`, which `ap_entry` has copied and
`ap_needs` has checked since `apps` was written, and which **nothing had
ever filled in** -- 0 of 85 entries. The same shape as #157: the layer
below already understood a thing the layer above had no way to say. A
part and a module are different relations and that is why they get
different words -- a part is a file an entry *owns and brings*, removed
with its owner; a module is owned by nobody, and `pim` serves Calendar,
Contacts and anybody's script.

**`catalogue purge`** is the other end of it. Removing an app says
nothing about its modules: the count this program keeps can only see
what it installed, and a script of your own that calls `need pim` is
invisible to it, so deleting native code behind that script's back is a
worse failure than leaving a megabyte on disk. Purge lists what was
fetched *for* something now gone, says which app wanted it, and takes it
away on a yes. A module installed **by name** is never listed. Run from
a desktop window there is no terminal to answer on, so it refuses and
names the flag rather than hanging on a read that cannot return.

**An app lands where it belongs** (#233). `bundles/apps/Internet/mail.hibr`
carries `dir: Internet` -- read from the bundle path exactly as the kind
already is, so the two cannot disagree -- and installs as
`apps/Internet/mail.hibr`, which `dt_apps` turns into a submenu because
it already recursed. A restored desktop is now `diff`-identical to
`examples/desktop/apps`: Accessories, Games, Internet, Office, where
0.99.194 gave twenty-one applications in one list.

That found an older wart. Reinstalling has always written the new paths
over the old record and left the old **files**, so an entry that stopped
bringing a part orphaned it for ever, invisibly. `dir` made it reachable
rather than merely untidy -- an app that used to land flat now lands in a
folder, and both would be on the scan path. An install now takes away
what the last one placed and this one did not, keeping anything another
installed entry also brings.

**OSTYPE, HOSTTYPE and MACHTYPE** (#235). hibr set none of bash's three
machine variables, so `case $OSTYPE in darwin*)` -- the idiom `bashbug`,
`gawkbug` and `gcore` use on this box -- read an empty string and took
the wrong branch. They are set at startup from `uname()` and on Linux
agree with bash character for character. They are bash's names rather
than one of ours because every script already knows them, and
`HIBR_ARCH` was taken: it is the arena's chunk size.

The catalogue reads them, which is the other half of #234's question
about architectures: `ap_arch` is a string operation where it was a fork
-- and a fork per entry declaring an arch, which `catalogue install
apps/` makes twenty-one of. Measured: **zero** `uname` executions. And a
module built for another machine is no longer listed, nor in `-j`, which
is what the Catalogue window reads; `catalogue info NAME` still answers
for one and says which machine it is for, because asking by name is a
different question from browsing.

**And a missing app says so.** `dt_launch` returned 1 in silence for an
app that is not installed -- which since 0.99.194 is the ordinary state
of a fresh desktop, and the window manager launches **ten** apps by
name, four of them fetched: `term` at nine call sites, `files` at seven,
`vault` and `imgview` at one each. So a bare desktop had a Home icon
that did nothing when double-clicked, a trash that did nothing, a New
Terminal on the wallpaper's own menu that did nothing, and a file that
opened nowhere. One note names the app and the Catalogue, in the one
place rather than at sixteen call sites -- and it *removed* a message
as well: the Date & Time pane's own "No terminal app to run it in",
which would have been a second note for one failure and said less.

Known, and its own release: the applications still live under
`examples/`, which holds three products and nine samples and is
therefore lying about what it contains. They move to `programs/` with
the desktop in 0.99.196 (Gitea #236), so the 305 paths that name
`examples/desktop` change once rather than twice.

## 0.99.194

**The desktop ships bare, and one command puts the applications back.**

Gitea #220. A `hibr` package placed the window manager and twenty-one
applications, and the catalogue placed the same twenty-one from a
different route -- two copies of one thing, free to drift, with nothing
anywhere saying which was the real one. From this release there is **one
package and one route**: the package is the window manager, the
desktop's own `system/` apps, the Control Panel panes, the Control Strip
modules and the savers, and nothing you open -- not Terminal and not
Files, which is the line the owner drew when asked where to stop.

`APPS ?= none` is the Makefile's default, so a source install matches the
package by construction rather than by a flag somebody remembers;
`make install APPS=all` is what one `make install` did before, for a
machine that will never reach a catalogue, and `make install-apps` is the
same thing on its own. A second **`hibr-desktop-apps`** package was built
and then dropped on the owner's own call -- *"drop hibr-desktop-apps,
catalogue only"* -- because a package of applications is the second route
again wearing a different hat, and `Recommends:` would not have reached
anyone upgrading anyway: **apt installs a new `Recommends:` on a fresh
install and never on an upgrade**, so every existing desktop would have
lost its applications silently and no new dependency would have put them
back.

What makes a bare default liveable is that restoring is **one command,
not twenty-one**: `apps install apps/` -- a kind with a trailing slash --
fetches, verifies and places every application in the catalogue, and
`apps install themes/` every theme. `ap_ofkind` is the whole of it.
Measured against the live catalogue: 21 applications and the 8 Control
Panel panes they bring with them, from one command.

The catalogue was **ten bundles behind** this tree, which is the thing
that would have made the transition a downgrade rather than a move: the
four apps the search sweep rewrote (Contacts, Files, Mail, YouTube), the
Control Panel and its Appearance pane, the three themes that gained a
tree style and the Arabic catalogue. All ten are republished with their
own versions bumped, so `apps update` offers them to anyone who already
had the old ones. **A bare package and a stale catalogue is worse than
neither**, and nothing in either repository's ritual would have caught
it. The check is `cmp` between every bundle and the file it was copied
from, and it is `./stale` in the catalogue now -- run before a release
that touches anything the catalogue carries, and checked against a
deliberately altered bundle before being believed.

Known, and its own ticket: an application installed from the catalogue
lands **flat** in `apps/`, so a restored desktop lists all twenty-one
under Applications rather than in the Accessories, Office, Internet and
Games submenus the shipped tree arranged them in. The index records a
kind and not a folder; that has always been true and was invisible while
the package shipped the folders too. Gitea #233.

Three things beside it.

**A module is in the Catalogue window now, and there is one list of
kinds rather than two.** 0.99.191 taught `apps` the `mods` kind and
nothing taught the window, so for three releases a module could be
fetched by hand and was invisible in the one place a person goes
looking -- `AS_KINDS` was written out again there and still said eight.
Adding `mods` to it would have fixed the symptom and left the cause, so
the list is gone: `apps list -j` sends the kinds it declares, in its own
order, and the window groups by those. A kind the answer does not name
but an entry claims is put on the end, which is what an `apps` older
than the window gets -- the same groups, in the index's order instead of
the declared one, rather than an empty window with no error in it.

The catalogue publishes no module yet, since every module hibr builds
ships in the package and the kind is there for somebody else's. That is
exactly why nothing noticed: the gap cost nothing visible and nothing
would have said so.

**And a heading says `Applications (21)`.** The count was right-aligned
against the list's own right edge, which is the column every version
below it fills, so a lone number there read as a version of the group.
In brackets beside the name it can only be a count. The brackets are
drawn rather than translated -- `%s (%d)` is a catalogue key with no
words in it. The check that was already named *"the fold still says how
many it holds"* asserted only that the heading was still on screen, so a
fold that lost its number would have passed; it reads the number now.

**Refresh Desktop is a key** (`ctrl-alt-r`), from #232's investigation.
The desktop's context menu has had that item since it existed and
`View > Refresh` with it -- but that bar is the *desktop's*, and with a
window focused it is the app's, with no Refresh on it. So the one moment
it is wanted, a screen gone stale where you cannot see what you are
clicking, let alone right-click the wallpaper behind a window, is the
moment it cannot be reached. A key can be pressed blind. It was called
`Refresh Screen` first, which `992-strings` refused as a catalogue entry
nothing draws -- rightly: a second name for one act is a second thing to
translate and a second thing to learn, and the context menu's own name
was already there.

## 0.99.193

**Six searches became one, and it is not drawn until you ask for it.**

Reported plainly: *"your search is inconsistent, the contacts is in the
title bar, and so on ... I see search in control panel, and in catalogue,
and in contacts, and all."* The survey behind that:

| | where it was | what it said |
|---|---|---|
| Mail | the **title bar** | `/ Search mail` |
| Contacts | the **title bar** | `/ Search contacts` |
| Files | row 1, right-aligned | `Search  /` |
| YouTube | row 1, left | `Search:` |
| Control Panel | inside its own list, dimmed | `Search…` |
| Catalogue | a label beside a field | `Search` |

Four placeholders, three places, the slash shown three different ways and
typed into in two different ways -- so knowing how to search one window
taught you nothing about the next.

`widgets/search.hibr` is the one search now, and the owner chose its
shape: **nothing is drawn until `/` is pressed**, then a bar on the
window's own bottom row, gone again on escape. No window spends a row on
a search nobody is making, which is what decided it over a permanent
first row -- a list is what the window is for. Enter closes and keeps the
query; the bar stays up while a query is still in force, because a
filtered list with nothing on screen to say so is a window lying about
what it is showing.

The cost of an invisible feature is that nobody finds it, and the answer
is the **Edit menu**, which every bar already carries: an app that
defines `<app>_find` gets a Find row there naming the key, and one that
does not gets it dimmed -- the same contract and the same dimming Copy
and Undo already follow, so no new mechanism.

What each window gave back: Mail and Contacts their title bars, Files the
sixteen columns its box reserved on the path row, YouTube its whole
` Search:` row, the Control Panel the row its permanently empty `Search…`
sat in, and the Catalogue the row its label and field took -- all of it
to the list, and the bar only there while searching.

**A key the desktop holds is not text, and that rule is the widget's
now.** Files and the Control Panel each had their own loop refusing a key
bound in `DT_KEYS`; the widget has one, so every window gets it. Without
it a search swallows the quit key, which the suites found at once: a
teardown's `qy` landed in the box and the check read `FILE1qy`.

**Mail and YouTube act when the bar is left, not on every keystroke**,
because one asks a mailbox and the other a server. Same widget, same
keys; only *when* the search is acted on differs, and it differs for a
reason.

Beside the sweep, the Catalogue and the tree:

- **The divider is yours to drag, and it is the list's scrollbar.** A
  scrollbar's track is the same vertical line a divider is, so one column
  does both rather than two side by side (`widgets/split.hibr`, which the
  Control Panel and the file dialog already used).
- **The detail pane scrolls**, with a bar of its own and the wheel over
  that half. Prose longer than the pane used to be cut with nothing
  saying so -- and a reader cannot scroll what was never laid out, so it
  is wrapped into lines first and drawn from a top.
- **A child sits under its parent's mark**: `[-]` at column 1 and `├─` at
  column 1 read as two things in a list. The connectors are one column
  in, which is what was asked for.
- **The tree is themeable the way the window buttons are.** It was a
  Control Panel setting in 0.99.192 and nothing more; a theme carries
  `tree` and `treelines` now, the bundled themes each pick their own, and
  the rows moved from Windows to **Appearance**, where a look belongs.
- **And a style change had done nothing at all**, which was reported and
  was a real bug in 0.99.192: the marks were computed once, lazily, and
  never read again. They carry the style and glyph set they were built
  for and rebuild when either moves, so nothing has to remember to ask.
- The `dots` style drew `▥`, which is a shaded square: `GL[disk]` is not
  a dot. It is a hollow box open and a filled circle shut.

Two things the checks taught, both this file's own lessons in new
clothes. **`sc.find("/gad")` found `apps/gadget`** in the detail pane
before the search bar -- a string search is a computed column wearing a
different hat, so the bar is found by the window's own edge. And
**adding two rows to the longest pane made it scroll**, which spends its
last column on a bar, which cut `construction` to `constructio` and
failed four checks about themes and shadows: a pane that outgrows its
window is a real consequence, and those checks get a window tall enough
to hold it.

## 0.99.192

**The mouse had never worked anywhere `dt_hit` was asked and told no**,
and the Catalogue is where that finally showed.

Reported as "the mouse does not work in the application catalogue". It
was one line in a widget every app shares: `dt_hit`'s miss path was
`ret ""` with no `return 1` after it, and **`ret` ends a function with
status 0** -- so for the life of the file it reported a hit for every
click ever made. An app whose `_click` is written

    tag := dt_hit "$id" "$r" "$c" && { case $tag in ...; esac; return 0; }

therefore swallowed every click that was not on a widget, which in the
Catalogue is the whole list: a click on a row did nothing at all,
measured. The two apps written `|| return 0` were luckier and still
wrong -- the Vault answered a click on empty space with "Nothing is
selected", because an empty tag reached `vault_do`, and the contact
editor cleared the focused field for one. Both stop now.

This is the trap this project's own notes already carry, in the widget
that every app asks: *a miss needs a bare `return 1` and nothing else --
`:=` has already cleared the slot before the call.*

**The window is called Catalogue**, which is what it was asked to be
called; it was *Applications Catalogue* for one release because it could
not be *Applications*, and the shorter name says as much.

**One button, and it flips.** Install when you have not got it,
Uninstall when you have: the same place, the same key, saying what
pressing it will do rather than offering two of which one is always
wrong. Update is a second button only while there is one to take, since
it is a different act and is not always there, and the App menu's own
row flips with the button -- written as two calls with a literal label
each rather than one with a variable, because a label that is a variable
cannot be found by `tools/strings.py` and would never be translated.

**A tree is a widget now** (`widgets/tree.hibr`), and it looks like one:

```text
[-] Applications                      3
├─ ✓ snake                      1.0
├─   retro_car                  1.2
└─ ↻ mines                      1.1
[+] Control Strip modules             1
```

The Catalogue drew its own `▼` and `→` before, which is two glyphs
nobody chose together and no way to change either. An app still owns its
rows -- what they are, what order they come in, which are open, because
that is its data -- and the widget answers one question: **what goes in
front of this row**. Control Panel > Windows > **Tree Style** picks
between `[+]`, arrows, triangles and dots, the same shape the window
buttons' own styles already are, and **Tree Rules** turns the lines
joining a child to its branch off.

Every style's mark is **three columns wide**, on purpose: a list lays its
other columns out from a fixed left edge, so a style that was two columns
in one and three in another would move every column in the window when it
was changed.

Two things in the harness, both the same lesson. `Term.keys` takes a
**callable** among its keys now, given the screen and answering the bytes
to send, so a click lands on a glyph rather than on a column somebody
worked out while writing the check. And the first use of it got that
wrong in the other direction: `sc.find("widget")` found `apps/widget` in
the **detail pane**, on the same line as the heading, so the click folded
the branch instead of selecting a row -- the search is bounded to the
list half now, and says why.

## 0.99.191

**A module can be installed from the catalogue, and the autoload index is
where the line is drawn** (Gitea #221).

[ADR 0041](docs/adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
rests on one sentence -- *an app is a script you can read before you run
it* -- and that sentence does not survive a `.so`, which is why `apps` has
refused one by name since it was written. The owner asked for modules in
the catalogue anyway, and was right to: a module is how hibr grows.
Three things stood in the way and the third settled the design.

**There was no user module directory at all.** `m_open`, `mod list`,
`mod avail`, `mod find` and `hibr_require` each walked the current
directory, `$HIBR_MODPATH` and the compiled-in `HIBR_MODDIR` -- five
copies of the same colon-splitting -- and nothing looked in
`$XDG_DATA_HOME/hibr/mods`, so "install a module" could not work whatever
the catalogue said. It is on the path now, after `$HIBR_MODPATH` and
before `HIBR_MODDIR`, through **one walk** (`m_wstart`/`m_wnext`) that all
five share. Root's rule has not moved an inch: `HIBR_MODDIR` and nothing
else, verified by running `mod avail` as root and counting zero.

**And the obvious version of that was wrong, which a measurement said and
the ticket did not.** The argument for putting the folder on the path was
*"no stronger than `~/.hibrc`, which hibr already runs"*. `m_bixbuild`
walks the whole path and `dlopen`s every candidate to read its descriptor
-- hibr's init is not called, the object's ELF constructors are -- and it
is built by any command that is not a builtin, not a function and not on
`PATH`. Measured here:

    $ cat /tmp/t.hibr
    typoooo 2>/dev/null; true
    $ strace -f -e trace=openat hibr /tmp/t.hibr 2>&1 | grep -c 'hibr/.*\.so'
    35

Thirty-five objects opened for one typo, in a **non-interactive script**,
on a box with **no `~/.hibrc` at all**. `.hibrc` is read by interactive
shells; this happens in a cron job. So the folder is searched when
something asks for a module **by name** and is left out of the index a
missing command builds. A bare `csv` says `command not found`; `need csv`
then `csv` runs it; the same object on `$HIBR_MODPATH` autoloads, which is
what says this is the folder's rule and not the module's.
[ADR 0044](docs/adr/0044-a-module-can-be-installed.md) is the reasoning,
and `tests/691-usermods.t` is the three-line demonstration.

**A module install is refused unless the index is signed.** The catalogue
now carries a detached signature over `index.json`, made with the key that
already signs the apt repository -- and since the index holds every
bundle's `sha256`, one signature covers every kind. `apps` verifies it
against a pinned fingerprint and refuses to install a **module** from a
catalogue that does not check out, where a **script** is installed with
one warning: there is a file to read and the hash says you are reading
what the publisher hashed. A host with no `gpg` is the same case. Without
this the rest would be decoration and `apps info` would be claiming a
check nothing makes; `apps info` prints which of the three holds.

**`arch` and `abi` are checked before the fetch**, as an app's `hibr` and
`modules` already were -- and `arch` *silently did nothing* on the first
try, because `ap_entry` copies a fixed list of fields out of the index and
`arch` was not on it. Measured, not reasoned about: a `darwin-arm64`
module installed on `linux-x86_64` and nothing said a word. That is this
project's own "an index field nothing enforces reads as a promise" in a
third costume, and the list has a note on it now.

Beside them, `apps show NAME` fetches a bundle, verifies it exactly as an
install does, and writes it to standard output without placing anything --
read an app before you run it, which is what a catalogue of scripts can
offer and a package manager cannot.

The whole path was driven end to end against a real signed catalogue
before any of this was written down: a module published with `addapp`,
indexed and signed by `mkindex`, served, installed, used, and removed.

    $ apps install mods/greet
    installed greet as /tmp/home221/data/hibr/mods/greet.so
    $ hibr -c 'need greet; greet Omar'
    hello, Omar
    $ hibr -c 'greet Omar'
    hibr: greet: command not found

and each refusal measured rather than assumed: an unsigned catalogue
(module refused, script installed with a warning), a tampered index
(refused), a module for another machine (refused, naming both), another
module ABI (refused).

`tests/appmgr.py` is 71 checks, six of them this, with a signing key of
its own -- which is possible because `HIBR_APPS_KEYFP` pins a different
fingerprint, for somebody serving a catalogue of their own. And
`tests/run.sh` gives every test an `XDG_DATA_HOME` of its own now, for the
same reason `tests/screen.py` already did: without it every test would
search whatever the person running it happens to have installed.

The catalogue side moved with it: `addapp` and `mkindex` take a `mods`
bundle, read its version, ABI and description out of the **descriptor**
(`mod avail` reads one without calling its init) and its machine out of
the object's own ELF or Mach-O header, so a module's entry is derived from
the bundle like every other kind's. `mkindex` signs the index when the
secret key is on the machine and says so when it is not.

Beside it, the Arabic catalogue is **1.1**: its strings have grown over
several releases with its declared version standing still, so nobody who
had 1.0 would ever have been offered them.

## 0.99.190

**The Applications window, promised by the documentation in 0.99.138 and
built now** (Gitea #219).

`docs/apps.md` has had a *From the desktop* section since the day the
application manager was first documented, and `examples/desktop/system/`
has never had such a file. 0.99.182 replaced the claim with the honest
holding note -- *"there is no window for this yet, and this page said
there was for several releases ... documentation that describes what was
never built is worse than silence, because it is the evidence somebody
would trust"* -- which stops the lie and is not a fix. Fifty-two releases
after the promise, this is the window.

**`apps` is the whole implementation and this is a window around it**, the
way the Vault accessory is a window around `vw`: anything one can do the
other can, because there is only one of them. The window runs `apps` as a
child and has no catalogue, no index reader and no verification of its
own.

**It reads JSON, never the columns.** `apps list`, `installed` and `info`
take `-j` now and answer the same walk as a document, so the window and
the command read one answer instead of one parsing the other's layout --
which is how a window comes to disagree with its own command. Each fact
is a field of its own: a window wants to know *whether* there is an
update, not to read "installed 1.0, 1.1 available" and work it out again.
What the ticket asked for was the result slot, and that is not available
here: `apps` is a separate program, so `:=` captures its standard output
(ADR 0038) and a child cannot fill its parent's slot. A builtin like
`mod list` can; the honest version of "one answer" for a command is a
machine-readable one.

**`apps show NAME`** fetches a bundle, verifies its `sha256` exactly as an
install does, and writes it to standard output without placing anything.
That is the thing a catalogue of *scripts* can offer and a package manager
cannot: read an app before you run it, which is what an app being a file
you can open is for (ADR 0041). `Read It` in the window is a window around
that.

**The list is a tree**, asked for directly: one branch per kind --
Applications, Control Strip modules, Control Panel panes, Themes, Colours
and the rest -- each saying how many it holds, `right` and `left` opening
and shutting, a branch with nothing in it left out rather than drawn
empty, and a branch with a search match in it opening itself. A tick marks
what you already have and the reload glyph what has a newer version
waiting. `tab` or `/` searches; `i`, `u`, `x` install, update and remove;
`r` reads an entry; `f5` fetches the index again.

**Every fetch is a job.** `apps install` is seconds of network and a
desktop that stops drawing while it waits is a desktop that looks broken,
so the command runs in a `( ... ) &` with its output in a file of its own
and the next frame picks up the status -- the shape `wm/remote.hibr` uses
for transfers, polled from `_draw` *and* `_idle`, because a window nobody
is looking at still has a job running in it.

Three things were found by the checks rather than by reading, and each is
a trap this project already records in other clothes.

**The window could not be called Applications.** The hibr menu already has
a submenu of that name -- every app you can open -- so a system app
called the same thing put two rows of one name on one menu, and `down`
from About This Computer landed on the wrong one: six checks in
`tests/desktop.py` failed at once, none of them about this window. It is
**Applications Catalogue**, which sorts to just under the submenu and
says what it is. That is the Control Panel picker's row-arithmetic trap
again: adding a row moves everything a check counts past it.

**`ret ""` before a `return 1` is a return that never runs**, and the lint
rule said so three times in one function. `as_at` answers "the entry the
selection is on, or nothing when it is on a heading", and each of its
three misses ended in `ret ""; return 1` -- `ret` ends the function on the
spot with status 0, so every caller's `|| ...` was dead code. The symptom
was not cosmetic, and was measured against the old lines rather than
reasoned about: with the selection on a *heading* the buttons read
`Install  Read It  Refresh`, and `i` asked the command `install ` -- an
empty id. They now read `Refresh` alone and `i` says "Nothing is
selected". A bare `return 1` is the form, because `:=` has already
cleared the slot before the call.

**A status line counted rows where a person counts entries.** Searching
`gad` in a catalogue of two drew one entry under one heading and said
`2 of 2`. The tree made "how many are shown" and "how many rows are
drawn" two different numbers for the first time, and the status was
reading the second.

And one found by running the suite whole: **`tests/apps.py` has been dying
in its own teardown**, after every check passed, because a check writes a
`jobnames` file into the session directory that `os.rmdir(S)` then refuses
to remove -- so it printed a traceback instead of `report()`. The four
parts hid it, since no one of them holds both that section and the
teardown. The check takes its own file away now.

`tests/apps.py` is 615 checks, seven of them this window's, with a stub
`apps` on PATH that answers the JSON and writes down what it was asked, so
"did the window ask for the right thing" is a fact rather than an
inference. `docs/apps.md` says what is there, with the window rendered
through the pty harness rather than drawn by hand.

## 0.99.189

**An app says which version of itself it is, the version leaves the
filename, and the tarball goes** (Gitea #218, #230, #231).

Three tickets, one change: the catalogue said `snake 0.99.93` -- the hibr
release that file last changed in -- in the filename, in the index and in
`apps list`, and asked, reasonably, *"versioning is all 0.99.xyz, i am not
sure what that means, does not each script/app have a real version?"*

**The thing declares it.** `app` takes a third word, so the declaration
stays one line and one concept:

    app snake "Eat, grow, and do not bite yourself" 1.0

`APP_VER` beside `APP_NAME` and `APP_DESC`, and the desktop's About box
shows it after the name, dimmed, placed by display columns since an icon
is one character and two columns wide. An app that declares none shows
none: this says what the app says, where a 1.0 nobody chose would be the
hibr release all over again.

All 28 applications and system apps say so, and **so do the 39 Control
Panel panes, Control Strip modules and screen savers**, which the ticket
did not account for: not one of them had an `app` line at all. They
register with `cp_pane`, `cs_module` and `sv_saver`, and a version could
have gone on those -- but `cp_pane(pane, title, icon, group = "system")`
already takes four, so a fifth positional would let a four-argument call
set *group* to the version, which is the `dt_app` trap this file records.
`app` is a declaration and those are registrations; nothing in the desktop
reads `APP_*`, so a pane declaring itself changes nothing and needs no
signature to move. Their descriptions are their own header prose, one
clause each. The three files that are helpers rather than installable
things -- `screenshots.hibr`, `control-panel/wallpaper.hibr` and
`savers/saver.hibr`, which *defines* `sv_saver` -- declare nothing, and
`addapp` refuses them for it, which turned out to be the publishing rule:
**publish what can say what it is.**

A theme, a colour scheme and a language have no line to put it on -- JSON
has no comments -- so they carry `"version"`, beside the `title` and
`about` they gained in 0.99.186. All 16, plus Arabic, and all of them
still read: `cp_themeread` and `cp_colourread` refuse a file whole on a
bad value, so a theme with an unknown key would be *missing from the
list*, and the count was measured rather than assumed.

**The version left the path, which was the owner's actual objection.** A
bundle was `bundles/apps/snake/snake-0.99.93.hibr`, and the question was
why, when git already versions a file. The answer that mattered is not
about history: nothing that installs has git -- `apps` fetches over HTTPS
from a static host -- and a versioned path is what makes an index entry
name an immutable thing. But the pool it kept was unreachable: `apps
install` takes no version argument, so **every older file was storage for
a feature that does not exist**. The path is stable now and git holds the
history, which is where anybody reverting would look.

The cost is named rather than hidden, and it was measured rather than
reasoned about. GitHub Pages caches the index and each bundle
independently for ten minutes:

    index.json                             cache-control: max-age=600  age: 384
    bundles/apps/snake/snake-0.99.93.hibr  cache-control: max-age=600  age: 0

so for a few minutes after a publish a client can pair an index from
before it with a bundle from after it, and the hashes cannot agree. The
first plan was to re-fetch the index and retry -- and that cannot work
here: three distinct query strings all answered `x-cache: HIT` on the same
cached object, so a query is not part of the cache key, and
`Cache-Control: no-cache` and `Pragma: no-cache` were both served from the
cache too. **No client can ask that host for a fresh index.** So the
mismatch is accepted and *explained*: the message names the cache first,
because saying only "changed in transit or on a mirror" reads as tampering
for what is a cache being a cache.

**And the tarball had no use left.** It existed only to carry an entry's
parts with an `app.json` saying where each went -- a manifest of files,
which is what the index already is, and a binary in a git repository that
nothing can diff. Every file is plain now:

    bundles/apps/mail.hibr
    bundles/control-panel/mailset.hibr
    bundles/themes/construction.json
    bundles/themes/construction.png

and the index names each part with its kind, the filename it lands as, its
path and its own `sha256`. `app.json` stops existing. The archive module is
no longer needed to read a catalogue. And the reproducible-tar dance the
drift check had just needed -- `--sort=name --mtime=@0` and `gzip -n`,
because `tar -czf` of identical content hashes differently every run --
went with it.

What the tarball gave *structurally* is now explicit: **every part is
fetched and verified before any is placed.** Measured with a part tampered
with on the server:

    $ apps install apps/mail
    apps: control-panel/mailset.hibr: the bundle is not what the index describes
    Refused, and nothing was written.
    $ find data/hibr -type f
    data/hibr: No such file or directory

Not even the app's own file, which had been fetched and verified by then.
A catalogue that still serves a tarball is still read -- `ap_puttar` stays,
since an index may be anybody's -- so this removes the need for one, not
the ability.

Three checks in `tests/appmgr.py` (60), two of which fail against
0.99.188; the third says in the file that it passes there, since an
install that placed no part had nothing to leave behind. `720-need.t`
covers a declaration with a version, without one, and name-only. ADR 0041
is amended where it said transport is a `.tar.gz` and that the index "has
no business knowing a bundle's internal layout" -- right about a tarball's
layout, wrong about the parts, since where a pane lands is the whole fact
anybody needs and keeping it inside a blob is what let eight applications
install half of themselves.

The catalogue is **84 entries from 87 files**, the three extra being theme
pictures: only a file with its kind's own extension is an entry, and the
rest are what an entry brings.

## 0.99.188

**Every picture the desktop did not take a palette from was sent at a
quarter of the area its cells cover, so a film read as blocks** (Gitea
#228).

`cn_image` ends by working out how many pixels to send:

    tw = w * cw;
    th = h * chh;
    if (im->id && tw > 2 && th > 2 &&
        (!(flags & DP_IMG_CHOSEN) || (im->over && cn_imghalf))) {
            tw /= 2;
            th /= 2;
    }

The comment above it says what was meant: halve **the wallpaper**, because
it sits behind text as a background, is re-sent to every client that
attaches and is never looked at closely -- and `console imgdetail full`
turns that off. What the condition says is "halve anything whose caller did
not set `DP_IMG_CHOSEN`", and that flag means **"a palette was taken from
this picture"**. This file already records that those are different
questions, in as many words, from the release that got it wrong the other
way round. The media module passes no flags at all, so every frame of every
film, on every terminal, had always gone out at half the resolution in each
axis.

Measured on the owner's own session with a probe attached to it -- `hold
attach -m -n probe`, the window on the visible workspace, the video
playing:

    131 placements in 14.1 s, every one of them
    a=T f=24 408x209 px  into 68x19 cells  i=1269547038

and the client's own pty, read rather than assumed, gives the cell:

    /dev/pts/5  64 rows x 213 cols, 2556x1408 px -> cell 12x22

68 by 19 cells at 12x22 is **816x418** device pixels, and 408x209 is
exactly half of it in each axis. Then reproduced with no desktop, no
`hold`, no ssh and no YouTube in it -- a bare player on a pty told its cell
is 12x22 -- which is the version that can be fixed and checked:

    before   sent 444x209 px into 74x19 cells -> the rectangle is 888x418 px
    after    sent 888x418 px into 74x19 cells -> the rectangle is 888x418 px

**The setting is three-state now, because the two kinds of picture want
opposite answers.** `console imgdetail auto|full|half`, `auto` by default:
a picture drawn *under* the text is halved and every other is sent at the
cell's own resolution. The knob reaches both rather than only the
wallpaper, which is what gives a slow link a way back -- full frames are
four times the bytes, and on that session the halved ones were already
3.3 MB/s.

| `imgdetail` | a picture looked at | the wallpaper |
|---|---|---|
| `auto` | 480x440 -- full | 240x220 -- half |
| `full` | 480x440 | 480x440 |
| `half` | 240x220 | 240x220 |

Control Panel > Pictures calls it **Picture Detail** and says which
pictures each value reaches, rather than a bare `Half`. `DT_WALLDETAIL` was
named for the wallpaper and had not governed only the wallpaper for as long
as it existed; it is `DT_IMGDETAIL`, and `DT_SETVER` 11 carries a saved
choice over -- `full` stays `full`, and `half`, which was the default,
becomes `auto`, since that leaves the wallpaper halved exactly as before.

Three checks in `tests/kitgfx.py` (77 now). The first fails against
0.99.187; the third passes there and says so in the file, because `half`
is what every picture used to get and it is in the suite to keep the way
back rather than to prove the change.

**What this release does not fix.** The same report says the picture
"shivers (blinks) on each refresh", and that is not settled: the ids are
*reused* for all 131 frames and the only `a=d` in the sample is the
probe's own attach, so it is not the placement being removed between
frames, which was the obvious theory. The player was also emitting 2
frames in 10 seconds at one point, so a "refresh" there is a discrete
event rather than a 13 fps flicker. `tools/` is not where it belongs yet;
the isolation test written for it is three passes over one picture in a
real terminal -- full and moving, half and moving, full and alternating in
place -- which separates the size from the replacing from the changing.
Whatever it turns out to be, it is its own ticket.

## 0.99.187

**`${#m["k"]}` counted bytes where `${#x}` counts characters, so every
length the desktop takes of its own text was wrong for a wide glyph**
(Gitea #226).

ADR 0021 settled this years ago: `${#s}`, `${s:i:n}`, `str len`, `str
slice` and `str index` count **characters**, and `str pad` and `str width`
count display columns, because padding exists to line columns up. The
plain form obeyed it; a **subscripted** one did not.

    plain      3
    map key    9
    array idx  9
    nested     9
    str len    3
    slice map  [日本]

bash answers 3 for all of its three. The tell that this was one missing
call rather than a design question is in that table: the plain form is
right, `str len` is right, and **slicing a subscripted value is right** --
only the length of one fell through to `strlen`. `xvar`'s own subscripted
branch had `strlen(v_getp(...))` where `xvar2` five hundred lines below it
has `u8n(v, strlen(v))`, which is the rule.

It is not academic, and the measurement is why: **81 places** in
`examples/desktop`, `mods` and `examples/*.hibr` measure a subscripted
value with `${#...}`, and an app's state lives under its window id by
rule, so that is where the desktop keeps all of its text. The clearest are
the text area's own `${#TB[$id][$i]}` -- a line of a document -- so a
cursor position or an end-of-line computed from one was out by two per
wide character, in Write, Note Pad, Stickies, Sheet, Mail's compose and
every one-line field that routes through `dt_textdraw`. This is the bar's
own "measured its own titles two different ways" trap, in the core rather
than in one widget.

Found by checking that a theme's `"wallpaper": "░"` reads back as the
literal glyph it replaced (it does -- same character, same three bytes,
equal) and noticing that `${#j["wallpaper"]}` answered 3.

`tests/198-len-subscript.t` is compared against bash and fails against
0.99.186 on four of its nine lines -- the map, a mixed-width string, a
dashed key and an array index -- while the slice and the `[@]`/`[*]`
counts beside them are unchanged, which is what says the fix is the length
and nothing else. The nested-map form is hibr's own, so it is asserted in
`tests/self.hibr` (92 now) rather than compared.

**And the other half of 0.99.186: the desktop did not look where an
installed theme lands** (Gitea #224 again).

`apps install` places a theme, a colour scheme or a language in
`$XDG_DATA_HOME/hibr/<kind>`, exactly as it places an application --
`examples/apps.hibr`'s own header says every one of the desktop's eight
scan lists has that folder after the person's own -- and it was true of
the five kinds that are **scripts** and false of the three that are
**data**: `CP_THEMEDIRS`, `CP_COLOURDIRS` and both of `wm/lang.hibr`'s
scan sites (one for a language by code, one for the list) named the
config folder and the bundled one and nothing in between. So the whole of
0.99.186 worked and the pane would never have shown what it fetched: the
bundle verified, the file placed, the theme's picture put beside it, and
nothing anywhere to see. Half a feature, and the half the install's own
output cannot reveal -- it says `installed retro_car as .../themes/retro_car.json`,
which is true.

Found by asking the Appearance pane for its list rather than by reading
the install, which is the only question that was ever in doubt. Four
entries added, in the order every other list keeps -- the person's own
folder first, so a file of theirs shadows a fetched one of the same name
and an update cannot overwrite their copy -- and two checks in
`tests/apps.py`, the first of which fails against 0.99.186 and the second
of which says in the file that it does not.

## 0.99.186

**Themes, colour schemes and languages can be published: a kind knows its
own extension** (Gitea #224), **and an entry's own name is checked before
it becomes a path** (Gitea #225).

The catalogue carried the five kinds that are *scripts*. The desktop scans
three more that are **data** -- `themes`, `colours` and `lang`, each read
from `$XDG_CONFIG_HOME/hibr/<kind>` before the shipped copy exactly as a
script is -- and none of them could be published, because `ap_dest`
appended `.hibr` to everything. A theme is `midnight.json`: there was
nowhere for it to land.

A kind now says what its files are called (`AP_KEXT`), and four things
follow it: the destination, the choice between "the bundle is the file
itself" and "the bundle is a tarball", the search for which member is the
thing, and the refusal of a bundle whose own extension is not its kind's --
which would be a file nothing ever reads.

**A theme can be two files**, which is what made this more than a table: a
theme's picture has to land *beside* its JSON, in the same kind under
another extension, because `cp_themeread`'s own `image` reads it from
there. A part named a bare name and had `.hibr` appended, so it could not
say that at all. A part names the **filename** now, with `as` -- and `name`
still means "with this kind's own extension", so every bundle in the
published catalogue installs unchanged on 0.99.183 and 0.99.184.

And the looks say what they are. `classic.json` was `{"colours":
"midnight"}` and nothing else, so a catalogue of nineteen looks would have
been nineteen rows of a filename. Both readers were checked rather than
assumed -- `cp_themeread` reads `colours` and then each key of `CP_THDEF`,
`cp_colourread` each of `CP_COKEYS` and `CP_COROLES`, and **neither refuses
a key it does not know** -- so every theme and colour scheme gained a
`title` and an `about` written from its own colours, and all 16 still read
with none refused. *"An amber monochrome on near-black, after a VT220 at
night"* rather than `amber`; the Appearance pane can show the same words
whenever somebody wants them to.

**And an entry's own name was never checked, which is how this release
became a security fix as well.** A part's path has been checked since a
bundle could carry one -- no climbing, nothing absolute, no `.so`, the
whole bundle refused rather than the bad member skipped -- and the thing
those rules exist for, the *name the file lands as*, went straight into
`ap_dest` from the index. Measured, not reasoned: an index saying
`"name": "../../../climbed"` with kind `apps` left a real file three
folders above the data directory, `installed ../../../climbed as
.../data/hibr/apps/../../../climbed.hibr`, with the **sha256 matching** --
which says the bytes were not changed on the way and nothing whatever
about where they go. `../../.hibrc` is the same hole with the shell's own
startup file on the end of it, so the cost of a wrong index was running
somebody's code at the next prompt.

`ap_nameok` is the rule, in `ap_entry` -- the one place an index entry
becomes the record every verb reads, so no path can be built from a name
that was never checked. The entry is **refused rather than the run
ended**: a bad one is missing from the list, the way a theme with a bad
value is missing from Appearance's, instead of taking the catalogue with
it. Gitea #225, found while reading this family end to end because a
theme's picture made a part's filename matter -- the shape is this file's
own: the rules were written for a part's filename and the *same question*
about the entry itself was never asked.

Seven checks in `tests/appmgr.py`, six of which fail against 0.99.185 --
the fifth guards the invariant rather than the change, and says so, since a
`.json` bundle was refused there too by failing to open as a tarball. And
the control needed the same fix as last time first: a `show` argument is
evaluated whether or not its check failed, so listing a folder that was
never made killed the run and took the checks below it with it. Twice now,
in the same suite, which is why it is written down in the test rather than
remembered.

## 0.99.185

**The TLS relay read OpenSSL once per `select`, so the tail of a reply was
stranded whenever the peer kept the connection open** (Gitea #223).

Found by the catalogue growing. `apps list` against the published index
stopped working the moment it reached 87 kB, and took **30.7 seconds** to
say `the index could not be fetched (0)` -- which is `DV_TIMEOUT` exactly.

A TLS record carries up to 16 kB of plaintext and `SSL_read` answers at most
what it was asked for, so the rest of the record stays inside OpenSSL --
where `select` on the socket can never report it, because those bytes have
already arrived. Measured with the relay instrumented:

    PROBE relay read 4096, pending 1386
    dav: PROBE fill read -1 (errno 11), buf 0/8192 rp 0
    dav: PROBE body gave up with 1386 left

The last 1386 bytes of the reply were sitting in the relay's own process
while `dav` waited its whole receive timeout for them.

**A peer that closes hides it**, which is what made this look like anything
but what it was: the next `select` then fires on the end of file and the
buffer drains by luck. The same 87402 bytes fetched whole through
`/dev/tls` by hand, because that probe sent `Connection: close`; `dav` keeps
connections and never saw them. And it is size-dependent only in that the
final record has to leave a remainder after a 4 kB read -- a 60 kB bundle
and a 264 kB `Packages` both arrived from the same host, and that one never
did, which cost an hour of looking at the content rather than the framing.

The relay drains what the library holds now, on **both** buffers:
`SSL_pending` counts plaintext already decrypted, and `SSL_has_pending` also
counts bytes taken from the socket and not yet made into a record -- either
leaves data `select` cannot see, and the second is the same trap one layer
down. Both bound with `dlsym` and optional, like the error strings. 87402
bytes in **0.358 s** where it used to time out in 30.7.

This reaches everything that speaks TLS through the relay -- `dav`, the
`http` module, `/dev/tls` in any script -- wherever the peer keeps the
connection open, which is every HTTP/1.1 server by default.

Two things beside it. `dav` reported a receive timeout (`EAGAIN` from
`SO_RCVTIMEO`) as *"did not answer"*, which is what sent an hour of hunting
at the server for a reply that was already here; a timeout is told apart
from a dead peer now and says so, with the number and the variable that
sets it. And the guard is `tests/999-tlsdrain.t` rather than the large-fetch
check added to `tests/dav.py`: **that one passes against the unfixed
relay**, because the test server's records drain evenly. The guard sends one
write of a size deliberately not a multiple of 4096 and keeps the
connection open -- `86016 of 90067` before, `90067 of 90067` after. A check
that cannot fail against its own bug is coverage, not a guard, and the two
say which they are.

## 0.99.184

**The catalogue has five namespaces now, because the desktop always had**
(Gitea #222).

Found by publishing the whole ecosystem rather than one kind of it: **54
bundles went in and 53 entries came out.** `clock` is an application
(`apps/Accessories/clock.hibr`) and a screen saver (`savers/clock.hibr`),
both installable, both called `clock` -- and the index was keyed by name, so
the second one written silently replaced the first. One collision today is
not the measure of it: `wallpaper`, `theme`, `cursor` and `shadow` are
Control Strip modules with names a pane or an app could easily want.

An entry is identified by an **id**, `kind/name`, in a new `entries`
section. `apps install clock` still installs it where the name means one
thing; where it means two it is **refused and both are named**, because
whichever the index happened to hold is not an answer to "install clock".
`apps list` shows the id, so the ambiguity is visible before it bites, and
both clocks can be installed at once -- which is the whole point, since the
desktop has always been able to hold them.

Three things that are not decoration:

- the id is **computed** as `kind/name` rather than taken from the index's
  key, so the state is keyed the same way whichever catalogue was read.
  Without that, a name-keyed index and an id-keyed state disagree and every
  installed app reads as not installed -- which is exactly what the suite
  caught when the first version inherited the key;
- an index keyed by name is still read, and everything above this in
  `tests/appmgr.py` runs against one, so that compatibility is **proved
  rather than asserted**. A new index also carries the old `apps` section
  for an older client, which then gets the 52 entries whose names are
  unique and misses only the two it could not have named;
- and the state is migrated on the way in: a key with no `/` can only have
  been an application, because the catalogue published nothing else until
  0.99.182.

`mkindex` refuses to build an index with two bundles of the same id, and
says which two. A silent overwrite is how this arrived, and it was only
found by counting 54 against 53.

Eight checks in `tests/appmgr.py`, six of which fail against 0.99.183. One
existing check was adapted rather than kept: `remove` says `removed
apps/demo` now, the id the catalogue calls it. And the control for the new
ones needed a fix of its own first -- the suite died at a state edit whose
key shape differs by release, taking the eight checks below it with it,
which is a control that proves nothing.

## 0.99.183

**An app can bring its own Control Panel pane, and what an entry says it
needs is checked** (Gitea #217), asked after the first catalogue went up:
*"we should have mods and we should have control panel and control strip
components all part of the hibr catalogue, right? why a half baked
solution?"*

Eight of the twenty-one published applications installed **half of
themselves**. A Control Panel pane is a file in another of the desktop's
five scan roots -- `filesview`, `mailset`, `notes`, `pimset`, `prayerset`,
`terminal`, `tube`, `vaultset`, and Prayer Times has a Control Strip module
as well -- and `ap_puttar` placed every member of a bundle under the app's
*own* kind, so a pane shipped with an app landed where nothing looks.
Install Mail and there was no Mail pane: no accounts, no offline setting, no
signature. It failed quietly, because `cp_pane` sits behind a `command -v`.

`app.json` names the parts now:

    {"app": "mail.hibr", "kind": "apps",
     "parts": [{"file": "mailset.hibr", "kind": "control-panel",
                "name": "mailset"}]}

and each is placed in the root it names. A part's `kind` must be one of the
five and its `name` lower case letters, digits and dashes, because **a part
decides a destination** -- the rules the member paths already followed had
to hold for where a file lands as well, and a bundle with a bad part is
refused whole rather than placed in part.

Two consequences, and the second is the interesting one. The state records
**every path placed**, not just the app's own, since a folder on disk cannot
say what it came with -- so `apps remove` takes away what was installed
rather than what the kind and the name imply, and an app installed before
this still removes the old way. And one pane serves **Calendar and Contacts
both**, so removing one of the two must leave the other's pane alone: the
state answers that by counting claims, and the removal says `kept
control-panel/pimset.hibr, which another installed app brings too`.

**And what an entry says it needs is no longer decoration.** `apps info` has
printed `needs hibr X, module ABI Y, modules: Z` since the command was
written, and `grep 'ENT\["modules"\]'` found exactly one reader: that
`printf`. So an app needing the db module installed happily on a machine
without it and failed at its first call, in a window, as a shell error in
`desktop.log`. All three are checked **before the bundle is fetched** --
`$HIBR_ABI` exactly, `$HIBR_VERSION` as a minimum through `sort -V`, and
`need` for each module named -- and the module refusal says outright that a
module comes with hibr rather than from a catalogue (ADR 0016), so there is
nothing the command could offer to install instead. An index field nothing
enforces reads as a promise.

In hibr's own tree an app declares its parts under the `app` line that gives
its description, which is what a publisher reads:

    app mail "Email, offline, labels and conversations as Gmail has them"
    # part: control-panel/mailset.hibr

Thirteen apps carry those lines now, including the four `system/` ones whose
panes are `abouthibr`, `cliphist`, `taskmgr` and `notifications`. Vault and
Prayer Times gained an `app` line at the same time: they were the two
without one, so the catalogue had been reading a paragraph of their header
comment where every other app offers a sentence somebody wrote.

Thirteen checks in `tests/appmgr.py`, nine of which fail against 0.99.182 --
the four that pass there assert a refusal that already happened for another
reason, or the removal path that has not changed.

**And a reason that had expired.** Asked, on seeing the first multi-part
bundle: *"why are we using a tarball? We have an archive module, which
mounts a tar as a directory, right?"* -- which splits in two, and only one
half was ever open. Transport, how several files travel together, is what
the tarball is for. **Residence** -- unpacked on disk, or mounted and
sourced through `/dev/archive` -- was decided in ADR 0041 on the measurement
*"`source` cannot read a scheme"*, and that stopped being true in
**0.99.140**: ADR 0043 taught `b_src` to ask `net_sch`, so
`. /dev/archive/b/app.hibr` sources today and `BASH_SOURCE` is the scheme
path. The premise had been dead for 43 releases, in a code comment and in
the decision record, and nobody had revisited it.

Re-examined, and unpacking kept -- on a different measurement:

    img size /tmp/x.png              ->  4 4
    img size /dev/archive/b/x.png    ->  hibr: cannot open /dev/archive/b/x.png

An app's resources live at `"${DT_SRC%.hibr}/"`, and a module that opens
its own path cannot see a scheme: `mods/img` calls `fopen` in three places,
and media and web read the same way. A `read` or a `source` through a
mounted bundle works and a **picture** in one does not, so mounting is not
one change but four modules' file handling -- and the app that broke would
be the one with a picture in it. The comment in `ap_puttar` and ADR 0041
both say this now, with the numbers, so the next person does not
rediscover it; nothing about the behaviour changed, which is why a green
gate was not re-run for the two of them -- `tests/run.sh`, which owns the
documentation checks, was.

## 0.99.182

**The catalogue exists, so `apps` points at it and the page stops saying it
does not.**

<https://osakka.github.io/hibr-apps> is published: twenty-one applications,
each a hibr script, each with the description its own `app` line declares.
The index is derived from the bundles rather than written -- the title from
the app's own `dt_app` line, the prose from its `app` line and header
comment, the kind from whichever registration it makes, the sha256 from the
bytes as they will be fetched -- so the catalogue cannot drift from the code
it offers. It is a git repository served as a page, the same arrangement as
the apt repository beside it, and a bundle's address in the index is
relative, so any copy of that one folder is a catalogue.

`AP_URL_DEFAULT` was
`raw.githubusercontent.com/osakka/hibr-apps/main/index.json`, which answered
404 for every release since ADR 0041: a fresh install's `apps list` has
never found anything. It is the page now. Measured end to end against the
real host before the default moved -- `apps list`, `apps info`,
`apps install` with the sha256 verified, `apps installed`, `apps update`
from an aged state -- in a scratch home, since an install otherwise lands in
the desktop of whoever ran it.

`docs/apps.md` opened with a section headed *"Before anything: there is no
public index yet"*, which is now false, and it twice described an
**Applications** window -- down to how it would run the command as a child,
the way the Vault accessory runs `vw` -- that nothing in
`examples/desktop/system/` has ever done. The first is replaced by the
catalogue; the second says plainly that there is no window yet and names
the ticket for it (Gitea #219). **Documentation describing what was never
built is worse than silence, because it is the evidence somebody would
trust** -- the mirror image of Sheet's lesson, where the work existed and
the documentation did not.

Four things the first catalogue made visible are filed rather than fixed
here, in the order they will be done: an app is more than one file and
eight of the twenty-one install only half of themselves (#217), an app has
no version of its own (#218), there is no Applications window (#219), and
installing hibr installs every application (#220).

## 0.99.181

**A reattached client's terminal is asked what it can draw, instead of the
session being stuck with the name of the terminal it was started from**
(Gitea #216), asked from a live session:

> when i reattach, the term capabilities are not checked, and you can get a
> hibr desktop thinking it supports pixels, but the terminal emulator cannot
> and vice versa?

Both directions, and the code said how far it went. `hd_cellpx` makes the
**cell size** the primary client's and pushes it into the program's pty on
every attach, so that much did follow whoever was watching -- which is why a
reattach could turn pixels off (`console gfx` answering `kitty 0 0`, the
state #211 taught `dt_imgpix` to read). The **protocol** followed nobody:
`cn_gfx` decided it once per process from `HIBR_GFX` and then `$TERM`,
`$TERM_PROGRAM` and `$KITTY_WINDOW_ID` -- every one of them an environment
variable of the program's own process, which inside `hold` is whatever the
shell that ran `hold new` had, frozen for the life of the session. `grep TERM
mods/hold/*.c` finds `SIGTERM` and nothing else. So three things were live:

1. started in kitty, reattached from a sixel-only terminal: kitty escapes
   into a terminal that drops them, in cells the console has already skipped
   in its diff -- blank rectangles;
2. started in a sixel terminal, reattached from kitty: sixels, which kitty
   has never drawn and says so -- the 0.99.72 bug, reachable by reattaching
   rather than by a wrong name list;
3. started anywhere whose name says nothing -- a plain `xterm-256color`, a
   shell inside `screen`, or a terminal window of another hibr desktop,
   which sets `TERM=xterm-256color` deliberately -- and `CN_GFX_NONE` was
   decided and kept: attach from kitty afterwards and every picture was half
   blocks for the session's whole life.

A name list cannot answer this. It says what a *family* usually does, where
the question is what this terminal, right now, actually does -- so the
display asks, through the path 0.99.180 built for the colours: the kitty
protocol's own query (one pixel, `a=q`, no `q=` so the reply is not
suppressed), which kitty answers `OK` and a terminal that does not speak it
answers not at all, and primary device attributes, whose parameters carry 4
where sixel is supported. Re-asked after any resize, which is exactly what a
reattach is.

Three details are the whole design:

- **the kitty query goes out first**, and a terminal answers in the order it
  was asked, so an attributes reply with no `OK` ahead of it is a terminal
  that does not speak kitty's protocol. That is the only *negative* either
  query can give, and without it a session could follow the terminal it was
  started from into one that draws nothing;
- **a positive is kept across a re-ask** rather than forgotten, or a plain
  window resize would drop to the name list for a frame and draw the wrong
  thing on the way back;
- and **the emulator in the middle knows it is a relay** by whether anybody
  drains what it passes on (`term pass`): `hold` does, every frame, and a
  window of the desktop never does. So a program in a window is still told
  what the emulator is (`CSI ?62;22c`), and a display inside a held session
  is told what the terminal it is really drawing on can do. Observed rather
  than declared, and rather than overloading `hasrgb`, which answers a
  different question.

`HIBR_GFX` still wins over everything, being a person's own choice, and a
terminal that answers neither query is read by name exactly as before.

Eight checks in `tests/kitgfx.py` and two in `tests/holdpix.py`, seven and
two of which fail against 0.99.180. `tests/screen.py`'s terminal model
answers these queries too, given `caps` -- and `caps=[]`, "a terminal that
answers and draws neither", is a different thing from `caps=None`, "a
terminal that says nothing at all", which is the distinction the negative
rests on.

Three existing assertions were wrong the moment the display sent an APC of
its own: they read "no APC at all" where they meant "no picture was sent as
a bitmap". They ask about placements now.

And two paragraphs of 0.99.180's own documentation are restored here. The
control run that proved #215's checks fail against the release before it did
`git checkout HEAD -- mods/console mods/term`, which reverted the READMEs in
those directories along with the code; restoring afterwards put back only
the files the compiler cares about. The code and `docs/display.md` shipped
correctly, the two README paragraphs did not.

## 0.99.180

**The display asks the terminal what colour it is, so a cell can be
painted a shade off it** (Gitea #215), reported as *"when I open aptitude
or other ncurses apps in terminal, their background is transparent, so
the wallpaper shows through (of course this is only in wallpaper pixel
mode)"*.

Measured on the owner's own running session rather than reproduced,
which is what settled it: a second display attached read only (`hold
attach -m -n probe`, the technique #175 cost six refuted mechanisms to
learn), at the session's own size and with no cell size of its own so
nothing about the session changed. Every cell of their aptitude window
arrives at a client with a background of its own -- 414 of 630 cells
`48;5;0`, the rest `48;5;4` and the window's own `DT_FACE` -- and the
wallpaper arrives at `z=-1073741825`, the layer below a cell's
background. So hold was not dropping it, the emulator was not dropping
it, and the `z` was the right one: hibr sends exactly what the protocol
says makes a cell opaque.

What is left is the terminal's own test for "has this cell a background",
and kitty's is in its own shader:

    uint bg_as_uint = resolve_color(colors[bg_index], default_colors[bg_index]);
    float cell_has_default_bg = 1.f - step(1.f, abs(float(bg_as_uint - crd.bg_colors0)));

a comparison of the cell's **resolved value** against the window's own
background colour, not of its attribute. Palette 0 resolves to black, the
owner's kitty background is black, so those cells count as having no
background and the picture is composited through them -- while palette 4
differs and stays solid, which is exactly what they see. It is why
*ncurses* programs and nothing else showed it: they paint indexed black,
where every window hibr draws paints a theme's own RGB.

A cell meant to be opaque therefore has to carry a colour that is not the
terminal's, and the display has never known what that is. It asks now:
OSC 11 for the background and OSC 4 for the sixteen palette entries an
index can name, at open and again after any resize -- which is also how a
session reattached from another machine arrives, and how the first ask of
all is recovered, since a held program opens its display before any
client exists. The replies come back as input and are swallowed by the
key decoder, where before this every one would have been typed in as
`alt-]` and its own text. `console termbg` answers what was learned and
whether it was answered or assumed; black is the assumption, because the
cost of being wrong is a cell drawn a shade off rather than a cell that
disappears.

Then one cell in a frame changes: over a picture drawn under the text, a
background that resolves to exactly the terminal's own is written with
one in the last channel -- the same colour to look at, and not the one
being compared against. Everything else is written exactly as it was
asked for, including an index, which keeps a themed terminal's own
colours; a cell with no background at all is still let through, which is
what the wallpaper's own cells rely on.

A **translucent** write goes through the same test, which is the half
that would have been easy to leave out: its colour is the picture's own
at that cell, scaled, so it can land on the terminal's background by
arithmetic rather than by anybody choosing it -- a black patch of a
photograph under a window's shadow scales to black. One cell of a frame,
and it would have shown as the picture at full strength exactly where a
shadow was meant to be.

`mods/term` carries the question: an emulator with no colours of its own
passes a colour query on instead of dropping it, and hold already sends
what the emulator passes to every client. So the query leaves a held
session, the real terminal answers it, and the answer arrives back the
way a keystroke does -- no new message anywhere, and a terminal window's
own program still gets its window's colours from `term colors` as before.

Twelve checks in `tests/kitgfx.py` and two in `tests/holdpix.py`, eight
and one of which fail against 0.99.179; the four that pass there are the
ones asserting nothing changed. `tests/screen.py`'s terminal model
answers a colour query now, given one, which is what makes the held pair
possible at all -- a model that could only ever parse an OSC is a model a
display can ask and never hear. One existing check was this release's to
fix: `tests/console.py` tested that a bare bell survives having every
OSC string taken out of the stream, with a pattern that knew only the
BEL ending, so the ST-terminated queries made it swallow the bell too.

## 0.99.179

**Redraw Skip says what its number counts** (Gitea #212), which is the
part of that ticket that turned out to be real.

The ticket said `DT_DRAWSKIP` had been renumbered without a `DT_SETVER`
migration, so every saved `1` now meant "skip one drag frame" where it
had meant "skip none". Two measurements refuted the premise:

- the default went **1 to 0 in 0.23**, the same release that introduced
  the setting, and the loop's own off-by-one was fixed in **0.25** --
  both of them 47 releases *before* `DT_SETVER` existed at all (0.72), so
  there was never a version to bump;
- and a sweep of **every** setting's default at `v0.72` against its
  default now found **two** that changed -- `DT_DRAGMOD` and
  `DT_TERMBAR` -- and both are already named in `dt_load`'s own
  migration. The mechanism has no gaps since it arrived.

So nothing is migrated. The owner's own `DT_DRAWSKIP=1` was a deliberate
choice, asked rather than assumed: with the default at 0 for 155
releases, a `1` in a file is far more likely chosen than stale, and
nothing in the file can tell the two apart -- a migration would have
reset somebody's own setting on a guess.

What is worth fixing is why it needed archaeology at all. The row read
`Redraw Skip (1)`, where the neighbouring one reads `Frames For Output
(70ms)`: a number with no unit, which can be read as a count of frames or
as a mode. That ambiguity is not incidental -- it is what let the
off-by-one live, since `Redraw Skip (1)` looked right while the loop
skipped nothing, and what later made a saved 1 unreadable without
`git log`. It is `Redraw Skip (none)`, `(1 frame)` and `(4 frames)` now.

## 0.99.178

**A text field has a clipboard whether or not its app wrote one**
(Gitea #214), reported as *"not all text boxes have copy/paste/cut... as
an example in control panel, when I was filling fields for the servers, I
could not copy/paste, and I am sure it's a common all rounder. Can we
root cause fix this?"*

It was not the widget and not a key binding. `wm/input.hibr` takes the
three keys **before any app sees them** and `dt_edit` acts only through
the focused window's own `_copy`/`_cut`/`_paste`, so no callback meant a
note saying the window cannot. Which window happened to host a field
therefore decided whether it had a clipboard: twenty files draw one with
`dt_textdraw`, **eleven** had ever called `dt_textpaste`, **none** could
copy or cut -- the widget has no selection, so every app that offered
`_copy` had written its own -- and `system/panel.hibr`, the Control
Panel, offered none of the three. The window with the most fields in the
desktop was the one that could not copy into them.

**The write path is typing.** A field's value belongs to its app, by
design -- the same as a checkbox's does -- so there is no way for the
desktop to write into one, and it turns out not to need one: every one of
those twenty routes keys through `dt_textkey`, so a paste is the
clipboard's own characters handed to the focused window's `_key`, exactly
as a person pasting by hand would, and a cut is ctrl-u then ctrl-k, which
`dt_textkey` already means as "to the start" and "to the end". No new
per-app contract, no `eval`, no signature change, and all twenty gain the
three operations at once.

Two things that had to be got right, and the second was wrong first:

- **The record of which field has focus is the widget's**, since an app
  keeps its own value and only tells `dt_textdraw` which field to draw
  inverted. `DT_FIELD` holds the window, the text and the cursor.
- **Its text is cleared every frame and its window is not.** Several apps
  draw a **placeholder** in place of the field while it is empty -- the
  Control Panel's picker says `Search…` -- so a field that has just been
  cut calls `dt_textdraw` not at all, and clearing the window with the
  text made pasting into an empty field impossible, which is the
  commonest paste there is. The window is dropped the moment focus moves
  elsewhere, so one that never had a field focused is never typed into.

A one-line field has no selection, so Copy is the whole of it and Cut is
the whole of it and empty -- stated rather than apologised for, since
every other Copy in the desktop copies a selection. A paste longer than
`DT_FIELDMAX` (1024) is cut to it and says so.

Three checks in `tests/apps.py`'s panel part, driven on the Control
Panel's own picker search box -- `dsv`, the Network Servers dialog, had
all three already and would have proved nothing. Each reads the box
rather than the note beside it, since a note times out. And the third
had to be rewritten after being run against the previous release: "the
box is back to `mail`" **passed** there for the wrong reason, because the
cut had done nothing and so nothing needed putting back. It types an `x`
between the cut and the paste now, so `xmail` can only mean both
happened, in that order.

## 0.99.177

**Every process a desktop runs says which session it belongs to and what
it is within it** (Gitea #213), asked for as *"we have a process called
desktop, but I am not sure that makes sense to me, it should be desktop
[session::session_name] and terminal should be term
[session_name::5:terminal_name]"*.

What `ps` showed before, on the owner's own machine:

```
hold:desktop       hibr [hold: desktop]        the hold server
supervisor         desktop [supervisor]        the supervisor
desktop            desktop                     the desktop itself
term:Terminal:5    desktop [Terminal 5]        a window's shell
desktop            desktop                     ← a dav job
```

and now:

```
desktop:work       desktop [session::work]
supervisor:work    supervisor [session::work]
term:Files:1       term [work::1:Files]
sync:dav.home.a    job [work::sync:dav.home.arpa]
```

Three separate things, and the last is the one the report was really
about. **A job the desktop forks wore the desktop's own name**, so a dav
sync alive for hours with two sockets open was a second process called
`desktop` with nothing to tell them apart -- `pe_job`, Mail's jobs, the
remote transfers, the vault sync and the Mail pane's own Test and Detect
all forked anonymously. `dt_jobname` names each from inside its own
subshell, leading with what the job *does*, since the word "job" would be
the same for every one of them.

**The desktop names its session even when that is the default.** The old
code left it off deliberately -- `desktop [desktop]` is a word twice --
and that was the wrong call once there was more than one session on a
machine: `desktop:desktop` is fifteen bytes exactly, so even the stutter
fits, and a stutter that says something beats a word that says nothing.
An **unheld** desktop says `desktop [not held]` rather than naming a
session it does not have, and the inner forms drop the session part
entirely rather than stand a placeholder in for one: `term [1:Files]`.

**And the format lives in one place.** `dt_procfull role sess handle
detail` builds all four, because it was written out at each call site and
a sixth site would have invented a sixth spelling. The two-name rule is
unchanged and is the reason the long form can be a sentence at all: the
argv region is as long as the command line it overwrites, where
`/proc/<pid>/comm` is fifteen usable bytes and is what `top`, `pgrep` and
`killall` read.

Verified by having each process report its own two names rather than by
racing `ps` against a child that may exec in place -- which is also how
`tests/apps.py` checks the job, since a `ps` snapshot of a subshell is a
bet on timing.

## 0.99.176

**Closing a terminal that still has something running in it asks first,
with a Control Panel switch to turn that off** (Gitea #209), asked for as:

> we need to make sure that we have a setting to on/off closing a
> terminal. if something is running, it should ask are you sure? if not it
> can exit, but this should be a setting in the control panel.

Almost all of it was already there, which is the part worth writing down.
`dt_closereq` -- the close button, Close on a menu, the Close Window key,
the context menu -- has called `DT[$id]["canclose"]` since it was written,
documented for exactly this ("an editor with unsaved changes asks first,
and closes it itself if told to"), and `dt_confirm` is Quit's own box.
Write was the only app that had ever used either. So this release is a
callback, a test and a row, not a mechanism -- the same shape as #157,
where `email account set` had taken a login all along and the desktop
simply could not say it.

**"Is something running" is not "is there a process".** The shell in a
terminal window always is one. The question is whether the pty's
*foreground process group* is still the program hibr started, which an
idle prompt leaves it and a running command does not. Measured on a real
pty rather than reasoned:

```
idle shell:         tpgid=879108 pid=879108
with sleep running: tpgid=879113
after it finished:  tpgid=879108
```

`tw_busy` reads that from `/proc/<pid>/stat` where there is one and asks
`ps -o tpgid=` where there is not -- and only when a window is being
closed, so the fork costs nothing a frame could notice. `comm` is in
brackets and may hold blanks and brackets of its own, so the fields are
taken from past the **last** `") "`. A background job leaves the shell in
the foreground and so does not count, deliberately: what this guards is
the thing that would lose work by being killed.

`DT_TERMASK` is on by default -- a terminal is the one window whose close
can lose work that is nowhere else -- and Control Panel > Terminal > Ask
Before Closing turns it off. A new setting needs no `DT_SETVER` branch: a
file written before it existed simply has no value for it and takes the
default, where a *changed* default is what cannot reach a saved file.

Four checks in `tests/apps.py`: a terminal with `sleep 30` running asks,
Yes closes it, one at its prompt closes without a word, and the setting
off closes it with something running. The first reads the box and its
buttons rather than the window's own title, because the box is drawn over
the screen at an absolute position and the title bar is under it -- which
is the same claim the second check makes from the other side.

## 0.99.175

**`dt_imgpix` answered "yes, pixels" for a terminal that names a protocol
and measures no cell** (Gitea #211), decided on the first word of
`console gfx` and nothing else:

```sh
case $g in
kitty* | sixel*) return 0 ;;
esac
```

`img draw`'s own gate is `cw > 1 && chh > 1`. So the desktop and the
module it is predicting answered one question by two rules, and where
they disagreed the desktop was wrong: `dt_wall` blanked the rectangle it
was about to put a bitmap in -- with the *default* pen, deliberately,
since a cell with a background colour paints over a picture the terminal
composites below the glyphs -- and `img draw` then drew half blocks into
it. Four things read that answer, and two of them are apps that choose
their whole drawing mode from it: `browser.hibr` (`dt_imgpix && bwm=pixels`)
and `youtube.hibr`, whose own comment notes that `media` has no `auto` of
its own, so the answer is final there. Both were being told to place
pixels that cannot be placed.

It requires the cell size now, which is the module's rule rather than a
second one.

**The state is narrower than it looks, and that is the useful part.**
`cn_gfx` answers `none 0 0` for a terminal that never gave a cell size,
and keeps the protocol's name once it has seen one -- so `kitty 0 0` is
not "a terminal that cannot do pictures", it is **a terminal that could
and has stopped saying**, which is what a resize or a reattach from
another machine produces. Measured as a sequence rather than from a
standing start, which is why a first pair of checks written the obvious
way passed against the bug and had to be thrown away: driven through
`Term.resize`, `tests/desktop.py` reads `gfx kitty 8 16 pix=yes` and then
`gfx kitty 0 0 pix=no`, where 0.99.174 reads `pix=yes` for the second.

## 0.99.174

**`console fill` never took `-p`, so five callers were filling rectangles
at the top left of the screen with their own arguments** (Gitea #208),
reported as:

> why is the contacts menu 20202020202020202020202020 (it's broken, and
> somehow corrupted the menubar) ... it's the contacts app, when it's
> opened, it overwrites the menubar with that, even when it's not the
> foreground application, but if I click on the menubar, I get the actual
> menu entries!

`console put` has handled `-p pane` since panes existed. `console fill`
never did -- and its usage line did not mention one either, so nothing
said no. Every caller that passed one therefore read its positions two
words late:

| what was meant | what was read |
|---|---|
| row `1` | `-p`, through `atoi` → **0** |
| col `34` | `w2`, the pane's name → **0** |
| h `16` | `1` |
| w `1` | `34` |
| glyph `│` | **`16`** |

So Contacts' list divider, asked for as
`console fill -p w2 1 34 16 1 "│"`, was one row of 34 columns at row 0,
column 0, filled with the text `16`. Which is a window writing over the
menu bar -- and **staying** there, because the bar repaints only when its
own key changes, so a click was what put it back. The digits are the
fill's own `h - 2`: a 20-row Contacts window says `16`, and the owner's
24-row one says `20`.

Reproduced in five lines, which is what named it after the menus, the
wallpaper, the icons, the Control Strip, the `MB` table and printed output
had each been measured and cleared:

```sh
console pane w2 14 4 20 70
console pen '#718096' '#120428'
console fill -p w2 1 34 16 1 '│'
```

`cn_pfill` is the paned fill, in `cn_pput`'s own shape: clipped to the
pane, in the pane's coordinates, with `cn_behind` turned off for the write
because a pane's own drawing must land even while the wallpaper is
painting behind the windows. All five callers were already written against
those semantics, so none of them changed -- three in Contacts (the search
box's background and both list dividers) and two in **Mail**, where an
inline image's row had the same fault and nobody had reported it.

`tests/console.py` gains four checks, all four of which fail against
0.99.173: the fill lands in the pane's own coordinates, a rectangle larger
than the pane is clipped rather than refused, **nothing it does reaches
the top left of the screen**, and a pane that is not there is an error
rather than an absolute write.

## 0.99.173

**A page drawn as pixels had no words in it** (Gitea #210), reported as
*"the browser shows pictures and proper rendering ... Interestingly, the
browser does not have text, so pictures without any other content :) that
makes it useless."*

One line, and the two halves of the module disagreeing about what a
screenshot is for. `wb_pixels` took it like this:

    wb_hidetext(t, 1);
    r = wb_call("Page.captureScreenshot", b, t->session);

`wb_hidetext(t, 1)` makes every letter on the page `color: transparent`,
and it ran for **both** of the paths below it. For the **cells** path that
is the whole design -- the picture carries the backgrounds and the images
and the text is drawn again over it as real cells, which is what
`mods/web/README.md` describes. For the **pixels** path it is the bug: that
branch keeps the picture whole and says in its own comment that there is no
text layer to build, so the page a terminal was handed was its backgrounds
and its images with no words anywhere in it. `wb_hidetext(t, !t->pixels)`
is the fix -- hide the text only for the path that draws it again.

Measured both ways on a page of black words on white: **0** of its 161,024
pixels are dark against 0.99.172, and 7,602 with the fix.

The reason no check caught it is worth more than the fix. `tests/web.py`
asserted that in pixels mode the page's text is **not in the cells** --
which is correct, is still correct, and is satisfied exactly as well by a
picture with no text in it at all. A check on an absence passes for the
wrong reason as soon as something else goes missing too. The new one reads
the picture: the harness keeps a kitty payload for exactly this purpose
(`Screen.imgdata`, from Gitea #129), so the page is decoded and its dark
pixels counted. kitty rather than sixel, because a DCS's bytes are counted
and dropped where an APC's are kept.

## 0.99.172

**The Mail pane's Detect covered the only way out of the dialog, and its
Test had nowhere to put the one sentence that answers the question**
(Gitea #207, both asked for from a live session while setting an account
up).

`mlad_draw` wrote `MLAD[$id]["dnote"]` at row `h - 1` -- the row
`dt_dlgbtns` had just put Save, Cancel and Detect on, and *after* them, so
a note of any length drew over them. It is a notification now, and two of
them rather than one, because the two answers want different lifetimes:
what Detect picked is already in the fields above, so that is a plain
`dt_notify` and confirmation is all it is; a **warning** -- "this upgrades
to TLS with the same certificate that was refused above, so Test it before
trusting it" -- is the reason not to trust what was just filled in, so it
goes at a priority with the Test window as its click action, which makes
the note the way to the thing it is telling you to do.

The Test opens a window of its own. The pane drew the ladder as rows with
each rung's detail in the **value** column, which is a word wide, and what
a failing rung has to say is a sentence from the server:
`imap.example.com refused the login for pat: NO [AUTHENTICATIONFAILED]
Authentication failed.` is the whole diagnosis and none of it fitted. The
window wraps it under its own rung, measured with `str width` rather than
`${#s}` -- the marks are glyphs wider than a cell and so is a translated
"incoming" -- and the pane keeps the button with a verdict beside it.

Three faults found in the building of it, each worth more than the feature:

- **The ladder's own reason never reached the rung.** `mt_one` captured the
  connect's error as `err=$(exec {fd}<>"/dev/tcp/$host/$port" 2>&1 >&-)`,
  and redirections are made left to right: the failing `{fd}<>` reported
  itself while stderr was still the real one, so `err` came back **empty**,
  the port rung read `fail` with nothing after it, and the message went
  past the script to whoever was reading its stderr. `in port fail` with no
  reason is the rung a person needs most. `2>&1` comes first now. The same
  trap this file already records for `read x < f 2> /dev/null`, found a
  second time.
- **A shell error in the job's output was parsed as a rung.** The job
  redirects stderr into the same file deliberately, and
  `hibr: connect 127.0.0.1:9: Connection refused` has four fields, so a
  filter on "at least three fields" accepted it: it was drawn as a rung
  called `hibr: connect`, wearing the dot that means a skip, counted as
  neither passed nor failed. Only `in` and `out` lines are rungs now;
  anything else is shown as what it is, where it happened, which is often
  the answer.
- **A window's pane has the frame at row 0 and column 0.** The new window
  drew from `0 0`, so every row came out one up and two left with its own
  first columns clipped: each rung lost its mark, each detail its indent,
  and the footer lost the number it opens with -- which reads as three
  separate bugs in composing the text rather than one wrong origin.

`tests/mailapp.py` drives the window against a stand-in that refuses the
password -- the case it exists for -- and asserts the mark, that the
sentence is on a later row *and* that the frame survives it, the footer,
and that the Detect's own answer arrives above an intact row of buttons.
`tests/mail.py` asserts the port rung carries its reason.

## 0.99.171

**One held session in trouble stopped everything that asked after it, a
desktop's own startup first** (Gitea #204, and Gitea #170 with it).

`hold list` dials every socket in the session directory in turn, and
`dt_holdnames` calls it from `dt_autohold` -- which runs at
`session.hibr`'s line 63, where `dt_open` and with it `desktop.log` arrive
at line 100. The dial was a plain blocking `connect` and the reply a plain
`hd_rall`, so one socket that was **listening with a full accept queue and
nobody accepting** was a desktop that printed its name and hung for ever,
with no error, no log and nothing to look at. That is exactly how #170 was
reported from a Mac -- *"no output, no log, indefinitely"*, and it started
fine once the sessions were killed -- and it stayed unreproducible for
twenty-nine releases because reproducing it needs a listener that fills its
own queue from inside itself and then stays alive. With one of those
planted, a 0.99.170 desktop drew **0 non-blank rows in eight seconds and
wrote no `desktop.log`**; with the bound in, 24 and one.

Every request to a session is bounded now: the dial 1000 ms, the reply
5000 ms, and `HIBR_CONNTIMEO` overrides both. That is a **default** where
`net_conn`'s own knob is opt-in, and the asymmetry is the point -- a
`/dev/tcp` script may mean to wait, and nothing asking a local session a
question does, so an opt-in bound would have shipped without reaching the
one caller that was hanging. The receive bound is `SO_RCVTIMEO` on the ask
socket and nowhere else: `hd_rall` already answers `EAGAIN` as an error, so
that is enough to end a read on a server that accepted and went quiet,
while the server's own reads and an attached client's stream go through the
same function and must still wait for ever.

A timeout is not a refusal, and two callers had to learn the difference
before the bound was safe to add. `hd_serve` **unlinks the path before it
binds**, so a `hold new` that read a timeout as "that name is free" would
take a running server's only socket away and leave it alive and
unreachable -- it refuses now, by name. And `hd_list` removes a socket it
gets no answer from, which is right for one that is gone and would be
deleting a live session for one that is merely busy, so it is narrowed to a
refusal. `hd_dial` leaves `errno` saying which it was.

The shape is **not** `net_conn`'s, and that was measured rather than
reasoned: a non-blocking AF_UNIX connect against a full backlog answers
`EAGAIN` -- nothing is in progress -- and a poll for writable then returns
at once with `POLLOUT|POLLHUP` and `SO_ERROR` **0**, which by TCP's rules
reads as a successful connect and hands back a descriptor connected to
nothing. So the connect itself is what is retried, against a
`CLOCK_MONOTONIC` deadline, since only a connect can learn whether the
queue has room; and each try that fails this way enqueues nothing, where
the blocking call left another connection on the queue every time anything
retried.

`tests/937-hold-dial.t` is the guard and fails against 0.99.170 three
times over.

## 0.99.170

**A Versions pane, so which image each part is running is one glance rather
than six commands** (Gitea #42, the half of it that was asked for first:
*"show the versions of the different components"*).

About This Computer cannot answer that question, and today made the cost of
it plain: it draws `hibr v%s` from `$HIBR_VERSION`, which is the **desktop
process's** own version, and a restart replaces that process and every
module it loads while leaving the hold server's image exactly as old as the
session. Three states that move independently, so the pane asks and names
them separately:

```
This Desktop
  Shell            0.99.169 (abi 16)
  Desktop          0.66
  Running Image    current
This Session
  Name             desktop
  Server Image     current
Modules
  console          1.0
  ...
```

**How a replaced file is seen, and why not by its inode.** The kernel
appends ` (deleted)` to a mapping whose file has been unlinked, which is
what apt and `make` leave behind -- so one read of `/proc/$$/maps` says
which of this process's own images are no longer the file they came from.
The exact comparison was measured and refused: `/proc/<pid>/map_files` is
`Operation not permitted` without `CAP_SYS_ADMIN` even to `stat`, so the
old inode's content cannot be reached at all, and `stat` per module would
be a fork per module on a pane drawn every frame. The cost of the cheap
answer is named rather than hidden: **a file rewritten in place keeps its
inode and is invisible here**, which no package manager does and a
build might.

`mod list`'s answer reaches the pane at all only because of 0.99.167: until
then it printed instead of filling the result slot, so `r := mod list` was
empty and the module table went to standard output -- in a pane, across the
middle of the screen.

Read once a second rather than once a frame, since a pane is drawn on every
frame it is open and a desktop's maps are tens of kilobytes. `vp_scan`
takes the maps file rather than reading one, so the "replaced" path is
checkable without a module of the real tree being replaced underneath the
suite.

`tests/apps.py` is 591 checks to 597, six in the Control Panel's own
section: the three headings, the shell's version with its ABI, a session
that is not held saying so, a held one named with its server asked
separately, every loaded module carrying the version `mod list` gives it,
and a mapping its file no longer backs being what marks one replaced. The
two that come with any new pane moved with it -- the picker's order (by
title within its group, so Versions sits between Task Manager and Windows)
and the system group's count.

#42 stays open for the other half of what was asked: a button to refresh or
update, which is the live reload the ticket's own open questions have to be
answered first.

## 0.99.169

**The Vault's two-factor login could not be completed: every emailed code
was dead by the time it was submitted** (Gitea #206). Reported from a live
Vaultwarden:

> I tried to login to vault, I get a ask for otp, when I provide it asks
> again, pings the otp endpoint multiple times, then gets back saying too
> many failures.

Every part of that is one ordering mistake. The desktop runs `vw login` as
a **fresh child per attempt**, handing it `password\ncode` on standard
input, so each attempt is a whole login from the start: the grant goes
without a code, the server answers 400 with `TwoFactorProviders`, provider
1 is email, and **vw mailed a code** -- *then* read the code off standard
input and submitted it. On the second attempt that mail issues a new code,
which is the moment the one the person has just read dies: `401 Two-step
token is invalid`, the dialog asks again, that attempt mails again, and
Vaultwarden ends it with too many failures.

Measured against `tests/bwserve.py`, before and after:

```
run 1 (no code):       rc=4 mailed=1     asks, mails a code -- correct
run 2 (code supplied): rc=1 mailed=2     401: Two-step token is invalid
run 2, with the fix:   rc=0 mailed=1     unlocked
```

Where a code may already be waiting (`VW_STDIN`, which is how the desktop
runs it) the code is **read first** and the mail goes only when there is
none to use. On a terminal the order stays mail-then-prompt and has to:
nothing is waiting to be read there.

**And a passing check sat on top of it.** `tests/vw.py` did drive provider
1 with a code and assert that it logs in -- because the stand-in's emailed
code was the constant `123456` and nothing ever invalidated it. **A
stand-in whose mailed code never expires cannot catch a client that mails
a fresh one before submitting the old.** That is #185's lesson in a second
costume: there the stand-in replied 200 without reading the body, here it
accepted a code the real server had already replaced. `STATS["code"]` is
the code valid *now*, each send issues the next one, and `/stats` carries
it so a check reads it the way a person reads their inbox. With the
ordering put back, the two new checks fail and nothing else does.

The terminal path is deliberately unguarded and cannot be driven from that
harness: with `VW_STDIN` unset the *password* is read from a tty too, so
such a run dies at "the password could not be read" long before two
factor. What pins the behaviour is the pair that can be driven -- a mail
when there is no code to use, and no mail when there is one.

One thing to know: a retry that carries a code now never mails, which is
the point, so a code that has **expired** cannot be replaced by submitting
again. Emptying the Code field and submitting asks for a fresh one. That
is correct and not discoverable, so the dialog may want to say so or to
grow a button -- a change to what the desktop shows rather than to the
protocol, and left out of this release deliberately.

## 0.99.168

**Every window title and nine messages were built by interpolation, so a
translated desktop read them in English** (Gitea #202).

`dt_retitle "$id" "Mail [$acct]"` cannot be a catalogue key: the key would
carry the person's own account name. So every app whose title says what it
holds -- Mail, Sheet, Write, Files, Terminal, Task Manager, YouTube,
Browser -- had an English frame in an Arabic desktop, and nine sentences
with it: `Sync failed:`, `Open With:`, `About`, `Screenshot saved:`, the
trash notes, the shortcut notes, and four `${VAR:-a literal}` defaults
whose fallback was a string nobody could translate.

`tools/strings.py` had counted them from the beginning and nothing failed
on the count: `template 37`, its own word for "text with an expansion in
it". **The count is the check now.** 37 went to 11, and the 11 that remain
are a `VALUEONLY` list with a written reason each -- a filename, a letter,
a value a dropdown shows, a server's own words -- so a *new* interpolated
string fails `992-strings.t` with the file, the line, the text and the fix
rather than reading English for ever:

```
a string with an expansion cannot be a catalogue key, so it will never be
translated: examples/desktop/wm/shot.hibr:262 Screenshot saved: ${f##*/}
make it a format and pass the value: dt_tr "...%s..." "$v"
```

That was run against this release's own `before` -- the guard fires on
exactly the line it was written for, and is clean once the line is a
format. Its boundary is worth knowing: the tool sees an expansion **at the
sink call**, so `t="x $v"; dt_note "$t"` still slips past it.

**One helper rather than eight keys.** `dt_wintitle app [detail]` is what
the titles go through: the app's own name is a key it already has from its
`dt_app` line, the detail is left alone, and an empty detail is the app's
name by itself -- which is what a Browser with no page open wants, and
what `Browser${tt:+ [$tt]}` was doing by hand.

**And the brackets are not a key.** The first version made `%s [%s]` one,
and `993-lang-ar` refused the Arabic for being byte-identical to its
English -- correctly: a frame with no words has nothing to translate.
`dt_wintitle` builds `"$a [$detail]"` directly, `%s %s` went back to
interpolation with a reason in the allowlist, and `%s %s; %s` stayed a key
because Arabic's semicolon is its own character (`؛`).

Seventeen new keys, all seventeen with Arabic, including a `dt_trn` pair
for the trash note -- Arabic has a dual as well as a plural, and that
branch is every count above one. `dt_note` and `dt_notep` already took
format arguments; a *clickable* note does not, since its own arguments are
the action, so those pre-translate with `dt_tr` and pass the result, which
is what `dt_confirm` next to one of them already did.

`tests/desktop.py` is 608 checks to 609: a window's own title comes out
translated with the detail in it left alone, in a run of its own because
the F10 in the check above it drops the hibr menu over the title bar.

## 0.99.167

**`mod list` and `mod avail` printed instead of filling the result slot**
(Gitea #205), so `x := mod list` left `x` empty with status 0 and wrote the
table to standard output -- in a full-screen program, into the middle of the
screen under the console's own cell drawing:

```
$ hibr -c 'r := mod list; printf "bound=[%s]\n" "$r"'
console      1.0      abi 16  a text display: cells, panes and decoded keys
bound=[]
```

That is #167 exactly -- `json emit`, `keys`, `len` and `type`, fixed in
0.99.141 -- and the `:=`-took-nothing-from-a-program trap of 0.99.89 before
it: a silently empty variable *and* output where nobody asked for it.

The whole family was tried rather than the one verb, which is what this
tree's own rule asks for: `lang info`, `hcal list`, `pty list`, `hold list`,
`dav servers`, `email accounts`, `db query`, `csv read` and `json` all honour
the slot. **Every module does, and the shell's own `mod` builtin was the only
thing that did not** -- visible in one grep, since `hibr_ret` or `->bind`
appears in all thirty module files and nowhere in `src/mod.c`.

`m_putln` writes a listing's line to the screen or into a string, and
`m_bound` hands a built listing over without its last newline, the way every
other answer is bound. `m_list` binds the **plain** form whatever stdout is,
since the fitted one is laid out for a terminal nobody is reading there --
and it is the form with stable columns that `tests/090-module.t` already
reads. `mod avail` prints from two calls down (`m_avail` -> `m_scan` ->
`m_probe`), so the string is threaded through both, NULL meaning "print as
before"; nothing in the ABI moves, since both are internal.

`tests/207-mod-bind.t` reads both halves for each verb separately -- it binds
its own rows, and it prints nothing while bound -- because one verb binding
correctly says nothing about the other, and a check that only read the
variable would pass a version that still printed. It fails against 0.99.166
with the entire `mod avail` table in the diff.

Found building a Control Panel pane for #42, which wants `mod list`'s answer
in a variable and would have had the module table across the screen and
nothing in the variable.

## 0.99.166

**Restart Desktop takes the session's own server with it** (Gitea #198),
which is what the owner had always understood it to do and what it had
never quite done.

Asked what had changed, the honest answer was: the reporting. About draws
`hibr v%s` from `$HIBR_VERSION`, the *desktop process's* own, and a restart
re-execs the desktop on whatever `$HIBR` is now -- so that number has
incremented on every restart since restarts existed, which is what was
watched happening. What a restart has never done, in any release (`git
log -S` over `wm/restart.hibr` finds no commit), is replace the **hold
server's** image: the server is the parent, it owns the pty and runs the
emulator every display is rendered from, and the restart happens *inside*
it. 0.99.137 began saying so, with a mark a restart cannot clear, nine
releases after the gap itself. So the complaint was right about the
experience and the reporting was right about the facts, and what was left
was to close the gap rather than describe it better.

`dt_restart` calls `dt_upsession` now, before `dt_ctlclose` -- where
stderr is still the desktop's own log, since after the `exec 2>&$DT_ERR`
below it is the real terminal and a message there would be drawn over the
screen.

**Four gates, and each is a case where upgrading would be wrong rather
than merely unnecessary.**

Nothing stale (`dt_sessionold`), which is almost every restart: an
ordinary one must not touch the session at all.

`HIBR_HOLD` names no session, so there is nothing to ask.

**The session named is not this desktop's own.** `HIBR_HOLD` is inherited
by everything a held terminal starts, so a desktop opened from a terminal
window inside another session names that session, and its program, its
name and its server all still agree -- every identity comparison passes.
What tells them apart is **how far down this process sits**: the session's
program is the desktop itself (one hop below the server) or the supervisor
above it (two), where a desktop inside somebody else's session is three or
four. `dt_holdsrv`'s own walk leaves both answers behind now
(`DT_HOLDPROG`, `DT_HOLDHOPS`), so there is still one walk.

**A module the resumed server would load is gone.** That is the only
failure here with no way back: the exec has happened by the time `mod
load` fails, and the master closes with the image that failed. The
` (deleted)` the kernel appends to a *replaced* inode is stripped, because
a replaced file is what apt and `make` leave behind and is the case this
whole thing is for; a path with nothing at it is a `make clean` in a build
tree, and it is refused while it still can be.

Measured in a real held session, not reasoned about:

```
server=487413 program=487414 self=487414
stale before: yes
deleted mappings before: 11
desktop: session dsk taken onto .../bin/hibr
dt_upsession rc=0
deleted mappings after: 0
stale after: no
still running as 487414
```

`tests/desktop.py` is 603 checks to 608: a stale session's server is taken
onto the new hibr, a restart with nothing stale leaves it alone, a desktop
inside somebody else's session upgrades nothing, a module replaced under a
running session is still loadable, and one that is no longer on disk
refuses the upgrade. The stubs stand in for the session, so none of it
touches a real one.

**One cost, once.** Only a session started on 0.99.159 or later has a
server that understands the request at all, so a session older than that
restarts exactly as it did and says why in its log. Every session from
here on upgrades on a click.

## 0.99.165

**A program spawned on a pty could read its size as 0x0 and lay itself out
for eighty columns** (Gitea #203), which is in this release because it is
what the release's own gate found.

`tt_spawn` set the window size **after** the fork, so a child that reached
its own `ioctl(TIOCGWINSZ)` first read `0x0` and `ed_cols()` answered its
fallback of 80. Its callers are the line editor's layout -- a shell's first
prompt, wrapped for the wrong width in a narrower window -- `select`'s
display, `mod list`'s fitting, and the console's size where there is no
terminal, which retries and heals where the other three do not. Everything
spawned on a hibr pty is reached, which is every terminal window in the
desktop and every held session, since `hold` spawns through the same
`py_api`.

The gate is what caught it, as `090-module` under `asan --quick`'s 32
suites:

```
FAIL 090-module         (recorded)
  -36 columns: fits
  +36 columns: TOO WIDE 71
```

It passed 8 of 8 alone under the sanitizer build and 16 of 16 under six
spinners, so rather than wait for the load, the window was made certain:
an 80 ms `usleep` standing in for the scheduling that normally loses this
race.

```
widest 71   widest 71   widest 71      3 of 3, the size set after the fork
widest 35   widest 35   widest 35      3 of 3, the same delay, set before it
```

The size goes on the **master** before the fork -- which needs no slave
open -- and the post-fork `tt_resize` goes with it, since `p->rows` and
`p->cols` are already assigned there and the record is memset, so it was
only re-sending the same size and a SIGWINCH with it. That removes the
dependency on scheduling rather than narrowing the window.

**A stale session is told what to do about it, and a failed upgrade no
longer half-serves** (Gitea #198).

The note a session with an older server shows said this:

> this session still runs the older hibr: quit and start the desktop again
> to take the new one. Restart Desktop cannot, and it keeps your terminals'
> programs where a new session will not

Every clause was true when it was written and the first one has been false
since 0.99.159, which gave the server a way to replace its own image and
take the session with it. It now says:

> this session still runs the older hibr: `hold upgrade desktop` takes it
> onto the new one without losing your windows or your terminals' programs.
> Restart Desktop cannot, since it re-execs the desktop inside this session

It names a command rather than offering a gesture on purpose: wiring it into
Restart Desktop is one line and a decision about a live session, which is
the owner's to make. The sentence is also the first version of it that can
be **translated** -- the format is the key and the session's name is an
argument (`dt_tr`, then `dt_noteq`, which does not look a finished sentence
up again) -- and Arabic has it.

**`dt_notify` and `dt_notifyp` were missing from `tools/strings.py`'s sink
list** for as long as they have existed, which is the `dt_mark` omission of
#137 again: both translate their text at runtime, so a literal note would
have been translatable and was simply never collected to be translated.
Every call today passes interpolated text, so adding them brings one key and
**six more templates** -- strings that carry a value and therefore cannot be
catalogue keys, counted and listed in #202.

**A server that is older than the verb now says so, which is the first
thing this will say to anybody.** `hold upgrade` sent its request and asked
for no reply, and a server from before 0.99.159 has no case for that request
and closes the connection without answering -- so against the session you
are most likely to try it on, the command **reported success and did
nothing**. It asks for the reply now and tells the two refusals apart with
`HD_INFO`, the oldest question in this protocol, because they are different
things to be told:

```
$ hold upgrade old
hibr: hold: old: this session's server is older than `hold upgrade` and
cannot take itself onto a new hibr: end this session once and the next one can
$ hold upgrade nosuch
hibr: hold: nosuch: no such session
```

Measured against a real 0.99.158 server built from the tag, not reasoned
about. This matters more than it looks: every session running today was
started before the verb existed, so the one-time cost of a session ended is
owed once per session, and nothing could have told you that before.

**And the half-served session.** `hd_upgrade` releases the pty before the
`execl` so the new image can adopt it, and when the exec does not happen the
loop carried on with an id the pty module no longer knew. Measured with a
`$HIBR` that is present but not executable:

```
hold list before:  u detached 383913 0x0
hold list after:   u detached -1 0x0
```

The session still drew and still relayed, which is what made it look fine,
but `hold list` answered a pid of `-1`, a client attaching at 24x80 never
resized the program again, and its exit would never have been noticed -- for
a desktop, also no cell size, so no pictures, for ever. The comment above it
claimed a failure "costs a log line"; that was half true. `hd_upgrade`
answers the id to carry on with now and takes the terminal back
(`hd_pty->adopt`) when the exec fails, so a refusal really does leave the
session as it was, twice over.

`tests/936-hold-upgrade.t` is the guard and it fails against 0.99.164 on
exactly those two lines. It checks the other direction too, which had no
check at all: the modules are copied so they can be replaced underneath a
running session, and a replaced mapping reads `(deleted)` in
`/proc/<server>/maps` until the server has loaded the new file at the same
path -- 9 such mappings before the upgrade and 0 after, with the program's
pid unchanged. **The pid alone is not evidence**, since it is the same
whether the exec happened or not, which is what made an earlier
verification of this feature worth nothing; and `/proc/<pid>/environ` is
worse than useless here, because `pt_rename` overwrites the environment
block, so the server's own `HIBR_HOLD_RESUME` is absent even when the exec
plainly happened. Three readings of "it did not happen" came from that.

`mods/hold/README.md` gains the verb, which was missing from its table, and
what the failure modes cost: a module that is **replaced** is safe, which is
what apt and `make` do; one that is **removed** with nothing in its place
loses the session unrecoverably, since the exec has already happened by the
time `mod load` fails.

The ticket's own smaller suggestion -- comparing module inodes so the mark
means "a fix you want is not in this session" rather than "something was
upgraded" -- was measured and **refused**: a deb gives every file it ships a
new inode on every install, every module relinks on every release in a build
tree since `HIBR_VER` lives in a header they all include, and the old
mapped file's content cannot be compared at all, because
`/proc/<pid>/map_files` is `Operation not permitted` without
`CAP_SYS_ADMIN`. An inode test answers "something was installed", which one
`-ef` already answers.

## 0.99.164

**A module whose init loads another module and then fails crashed the shell**
(Gitea #201). `m_load` adds the record, calls the init, and on failure took
the record out by **position**:

```c
	v_add(&s->mods, m);
	if (d->ini && d->ini(s) != HIBR_OK) {
		s->mods.n--;
		dlclose(h);
		free(m->path);
		free(m);
```

That names this module's record only while nothing else was added in
between, and an init that calls `hibr_require` adds one: hold's asks for
`pty` and then for `terminal`, so with pty present and term missing the list
is `[... hold, pty]` by the time the init fails. The `n--` dropped *pty's*
record; hold's stayed, was freed, and its object unmapped. The next walk of
that list dereferenced it -- `m_need`'s own tail walk first, which is why
`need` is where it landed:

```
$ mkdir /tmp/d && cp build/mods/hold.so build/mods/pty.so /tmp/d/
$ HIBR_MODPATH=/tmp/d ./build/hibr -c 'need hold; hold list'
hibr: hold: needs the term module
hibr: mod: hold: init failed
Segmentation fault
```

Three runs out of three, and `m_need` at `src/mod.c:940` under gdb. The
record goes by identity now -- the swap-with-last `m_drop` and
`hibr_unprovide` already use. The module the failing init loaded stays
loaded, which is right: it initialised fine and is nobody else's to unload.

It is this tree's own trap in a second costume -- *a descriptor lives inside
the object, so read it before dlclose* -- except that here it is the record
that outlives the object.

`tests/999-mod-initfail.t` is the guard, with `tests/modfail.c`: a module of
the suite's own whose init loads another through the same `m_need` that
`need` uses, then fails. A fixture rather than hold.so and pty.so on
purpose, since a check built from real modules passes vacuously wherever
`HIBR_MODDIR` happens to hold a `term.so` -- which is how 0.59's lint check
came to pass for the wrong reason. It exits 139 against the shell before the
fix and is clean after.

Found while measuring what `hold upgrade` does when a module cannot be
loaded, which is #198's own question and is answered there.

## 0.99.163

**The Edit menu's items can be translated** (Gitea #200), and the shortcut
beside them lines up whatever it is.

`dt_editmenu` built each label by padding the shortcut into it:

```sh
printf -v lu '%-11s%s' Undo "${DT_KEYS["undo"]}"
dt_item "$lu" u dt_editdo undo
```

So what reached `dt_tr` was `Undo       alt-z` -- a string no catalogue can
hold, since it carries the person's own key. Undo, Redo, Copy, Cut and Paste
all stayed English in a translated desktop, and `Select All`, the one built
from a literal, was the only one of the six that did not. Measured under the
pseudo-locale, which wraps every string the catalogue holds.

**Padding was not the answer, and the fix does not keep it.** A row carries
its shortcut as a field of its own now (`dt_rowaccel`, set on the row just
added, so it reads under `dt_item`, `dt_dim` or `dt_mark` alike). The label
is padded to `w - 3 - str width "$accel"` and the shortcut appended, so every
row's shortcut ends in the same column whatever its length, and the menu's
own width grows by the longest `label + accel`. Nothing is a fixed eleven
columns any more:

```
as shipped                  paste rebound to ctrl-alt-shift-v
  Undo      alt-z             Undo                 alt-z
  Redo      alt-y             Redo                 alt-y
  Copy      alt-c             Copy                 alt-c
  Cut       alt-x             Cut                  alt-x
  Paste     alt-v             Paste     ctrl-alt-shift-v
  Select All                  Select All
```

The menu goes 20 columns to 31, every shortcut still ends together, and a
row with none is untouched. The mirrored layout puts it on the left, where
that layout's own right-hand side is.

One check elsewhere was asserting the padding rather than what it says:
`tests/apps.py` looked for the literal `Copy       f5`, the `%-11s` spelling
exactly. It reads the Edit row and asks whether `f5` is on it now -- the
same lesson as a check that asserts a half block where a bitmap is drawn.

`tests/desktop.py` is 598 checks to 601: that the Edit menu's items come out
translated, that each still says which key it is, and that a longer chord
widens the menu with every row still ending in one column -- which is the
question a fixed `%-11s` could not have answered, and is the one a reader
asked when this fix was proposed.


## 0.99.162

**Every checkmark item in every menu was untranslatable** (Gitea #137).

`dt_mark` puts its label through `dt_row`, which translates it like any
other menu item -- and `tools/strings.py`'s list of functions whose first
argument is text named `dt_menu`, `dt_item`, `dt_sub` and `dt_dim`, and not
`dt_mark`. So no checkmark label was ever in `examples/desktop/lang/
strings.txt`, `dt_tr` found nothing, and every one of them stayed English in
a translated desktop: Desktop Icons, Hidden Files, Tile Workspace, Pass
Every Key, Full Command Line, Blank, Everyone, Mine, Trust This Sheet.

**And nothing could notice**, which is the part worth keeping: `992-strings`
asks whether every string the scraper *found* is in the catalogue, not
whether everything that reaches `dt_tr` was found. A sink missing from that
table is invisible to the check built on it.

One line in `tools/strings.py`, nine strings into the catalogue and the two
pseudo-locales, nine into Arabic. The pseudo-locale is the proof rather than
the catalogue: `xx.json` wraps every string the catalogue holds, and
`"Desktop Icons": "⟦Desktop Icons⟧"` was not in it before this release and
is now.

`tests/desktop.py` is 597 checks to 598: the View menu under `DT_LANG=xx`,
asserting the checkmark item's own label comes out wrapped. That is the
check the ticket was missing -- a label that reaches the screen rather than
a line in a file.

**Found while there, and filed rather than fixed** (Gitea #200): the Edit
menu's five items cannot be translated either, for a different reason.
`dt_editmenu` pads the shortcut into the label (`printf -v lu '%-11s%s' Undo
"${DT_KEYS["undo"]}"`), so what reaches `dt_tr` is `Undo       alt-z` and no
catalogue can hold it -- the `Tiled Main ($DT_TILEMAIN%)` trap of 0.99.154
from the other end. Under `xx` the Edit menu shows five plain items and one
wrapped `⟦Select All⟧`, which is the one built from a literal. The fix is
the same shape as that one and touches the menu widget every app draws
through, so it is its own ticket rather than a passenger here.


## 0.99.161

**The player drew blocks and never said why** (Gitea #199).

`YT_MODE=pixels` and `follow` both resolve through `dt_imgpix` -- can this
display place a bitmap at all -- and when it cannot, the player quietly drew
half blocks. The status bar said `follow:half`, which reads as the setting
being ignored rather than as the terminal not offering one. It says
`follow:half, no pixels here` now, and only where pixels were actually
wanted: a person who chose `half` is not told their choice happened.

### What the measurement said, which is why this is all that changed

The report was "does not render except blocks even in pixel mode, and it
blinks like mad", on a held session with a picture wallpaper. Both halves
were driven here first.

**Held and unheld are the same.** The same clip, the same session file, once
under `Term` directly and once inside `hold new` with a client that reports
what a cell measures -- the comparison the ticket asked for, built out of
`tests/youtube.py`'s own session and `tests/holdpix.py`'s own holding:

```
unheld  (images, placed, deleted, half blocks)
        (22, 22, 0, False)  (36, 36, 0, False)  (51, 51, 0, False)  (65, 65, 0, False)
held
        (28, 28, 0, False)  (46, 46, 0, False)  (63, 63, 0, False)  (78, 78, 0, False)
```

About fifteen pictures a second either way, **no deletes at all**, and no
half block anywhere. So the pixel path works through `hold`, and nothing is
being placed and dropped and placed again, which is what a blink would look
like.

**And the player's own path was already right**, measured at three levels on
a local desktop: the app chooses the mode and sizes for it (`md=pixels`,
`sized=78 19 pixels`, 594 of 636 draws returning 0), the module's clipping
guard passes (`fit=1`), and the display places it (`image=1`).

So what is left is the display the report came from. `DT_IMGMODE=auto` and
`YT_MODE=follow` there, so both questions become "can this terminal place
pixels right now" -- and the one thing that makes that false is a terminal
that reports no cell size, which is a thing this desktop already knows
happens on one of the owner's. That is what the new words on the bar are
for: they turn an invisible fallback into an answer, in the one place
somebody is already looking.

`tests/youtube.py` is 30 checks to 31. The held comparison above is **not**
in the suite: it was measured rather than guarded, because held pictures are
`tests/holdpix.py`'s subject and a player inside a held session is a second
copy of that machinery for a case this release did not change. Said here
rather than left as a gap nobody wrote down.


## 0.99.160

**Nothing had ever driven the YouTube player's pixel path** (Gitea #199),
which is how "it does not render except blocks even in pixel mode" could be
reported against code no suite had asked a question of.

`tests/youtube.py` ran every one of its twenty-eight checks on a terminal
that reports no cell size, so the player could only ever draw cells -- and
its own `playing()` said so outright:

```python
return "playing" in sc.text() and sc.find("▀") is not None
```

A half block is what the **cell** modes draw. A picture is a bitmap and the
cells under it are left blank on purpose (ADR 0037), so there is no glyph to
find at all: the check asked about the mechanism rather than the outcome,
and asking for pixels would have hung it on an assertion that could never
come true. It asks for either now -- a half block **or** a picture on the
screen model -- and `yrun` takes a cell size and an environment, so a check
can say what the terminal can do.

Two checks added with it: on a terminal that can place pixels the frame goes
out as **one picture and no half block anywhere**, and the player says which
it is drawing. Both were watched failing first, from a run where the cell
size was given and the protocol was not: `images` empty, `▀▀▀` on screen --
the player correctly falling back to cells, and the check correctly refusing
it.

### What that settles, and what it does not

On a local desktop with a terminal that can place pixels, the player **is**
drawing pixels: measured through the module as well as the screen, with the
display's own `image` call answering 1 and the placement going out. So the
report is not about the pixel path being broken in general.

What is left is the owner's own arrangement, which this harness does not
have: a **held** session, where hold re-emits every picture to each client
from the emulator in its server process, and a **picture wallpaper** under
the text at the same time. Both are on the path and neither is covered by
`tests/youtube.py`; `tests/holdpix.py` is where a picture through hold is
checked, and it drives no player. The measurement that splits them is to run
the same clip in an unheld desktop and in a held one, which needs the
machine it was reported from.


## 0.99.159

**A session can be taken onto the new hibr without ending it**
(Gitea #198). Reported as an objection, and the right one:

> restart session is designed to allow me to upgrade my hibr session
> without losing my open apps and stuff

It is, and it very nearly did. `dt_restart` re-execs the desktop, so the
shell and every module the *desktop* loads come from disk fresh and the
programs in terminal windows survive, because they are children of the
process being re-exec'd. The one process it cannot replace is the `hold`
**server**: it is the parent, the restart happens inside it, and the
emulator every client is rendered from lives there. So a fix in `hold`,
`term` or `pty` -- and only those -- stayed out of a session for ever, and
the only remedy was to quit it and lose the terminals' programs.

Measured first, because the objection came with a claim: **nothing
regressed.** `dt_restart` has never touched the server, in any release;
what changed is that 0.99.137 started *saying* the session was old, two
days before the complaint. The behaviour is as it always was.

### `hold upgrade NAME`

The server re-execs itself on the hibr that is on disk now and takes the
session back:

```
server pid before: 22155    program 22156
stale modules in the server: hold.so, pty.so, term.so
  hold upgrade up
server pid after:  22155    program 22156
stale modules in the server: none
```

The program keeps running and keeps its pid; the server keeps its pid,
since an exec keeps it; every attached display stays attached; and what was
on screen is still on screen.

Everything it needs already existed. The listening socket, the pty master
and each client's connection are descriptors, and a process that execs
keeps the ones it has not marked to close -- `hd_keepexec` is the inverse
of the `hd_cloexec` this module already had. `py_api` gains `adopt` and
`release` (version 4) and `tm_api` gains `save` and `load` (version 5),
each of which is what the `pty adopt`, `pty release`, `term save` and
`term adopt` builtins have done since 0.99.60, for a module that has no
shell to ask. The emulator's own file format is appended to rather than
rearranged, which is what makes carrying a session **across** an upgrade
safe at all, and was already true before this needed it.

Three things that are not details, and the first cost the first run:

- **The modules are re-loaded by the paths they were loaded from**, read
  out of `/proc/self/maps`, not by name through the module path. A session
  started with `mod load ./build/mods/hold.so` has no module path, hold
  asks `hibr_require` for `pty` and `term`, the resumed server found
  neither, and the session was lost at the exec. All three paths are
  carried now.
- **Nothing is closed and the program is not released until the moment
  before `execl`**, so a failure anywhere earlier leaves the session
  exactly as it was and costs a line in the log.
- **No client's `front` is carried.** Each is sent the whole screen again,
  which is what an attach already does; a stale front is the one thing that
  would leave a display wrong.

### What this release deliberately does not do

**Restart Desktop is untouched**, and so is the notification that asks for
a new session. The verb is explicit and opt-in, so nothing on a live
desktop's own path changes until it has been tried on a session that does
not matter. Wiring it into the restart -- upgrade the server, then re-exec
the desktop, one action -- is the obvious next step and is a decision about
somebody's live session rather than a tidy-up.

`tests/790-hold.t` grows the check: the session is still listed, the
program is the same one, the server is the same process, and the screen
came through the exec. That the server's *modules* are the new ones is
measured by hand above rather than in the suite, because checking it means
replacing `build/mods` while the rest of the gate is loading from it.


## 0.99.158

**The mouse mode is the window manager's, and it puts its own back**
(Gitea #197).

Every place that changed a console mode set it to a literal on exit rather
than restoring what it found, so whichever component ran last would win.
Measuring the premise first narrowed it, and changed which fix is right:

- `savers/saver.hibr`'s `console mouse click` / `off` are inside `sv_main`,
  which that file runs **only when it is the program**
  (`[ "${BASH_SOURCE}" = "$0" ]`); a desktop's saver goes through
  `sv_begin`, which touches no mode;
- `wm/standby.hibr`'s two are inside `dt_sbjoin` and `dt_standby`, and
  `dt_standby`'s one caller is `wm/hold.hibr` under `--standby`: its own
  process;
- `login/login.hibr` is the login screen, also its own process, ending
  before the window manager starts.

Each owns the console it is setting, so none of them can reach a running
desktop, and `system/about.hibr` -- the only thing in the desktop's own
process that touches the mode -- merely **reads** it to say what it is.
There was no live instance, which rules out the two cheap answers: a
convention or a helper binds only the code written to follow it, and the
component this has to survive is the one that is not.

So the window manager owns it. `DT_MOUSEWANT` is what the desktop asked
for, `dt_mouseset` is the one place that changes it, and `dt_mousecheck`
reads the console once a frame and puts it back when it differs and nobody
has borrowed it. A read costs **2 us** against a frame budget of about 700,
measured before this was chosen, which is what makes a backstop affordable
-- and a backstop is the only version that survives a component killed
between its set and its restore.

Watched failing and then passing, with a window that clears the mode behind
the desktop's back on its second frame:

```
without                          with
draw 2 console=off want=drag     draw 2 console=off want=drag
draw 3 console=off want=drag     draw 3 console=drag want=drag
draw 4 console=off want=drag     draw 4 console=drag want=drag
```

`DT_MOUSEHELD` is the seam for a component that one day wants its own mode
for its own duration; no helpers are written for it, because nothing needs
one yet and an interface with no user is designed from a guess.

### The name cost a suite run, and the rule was already in CLAUDE.md

The first version called it `DT_MOUSE`, which `wm/state.hibr` has owned all
along for **the window the mouse is down on**. Setting it to `off` at file
level made `input.hibr`'s "a window already has the mouse" test true for
ever: **forty checks failed, every one of them a drag**, and the fix looked
innocent. "Before declaring a new `-gA`, grep the desktop for the name as a
whole word" is this file's own recorded trap, from Contacts naming its
window table `PW`; it applies to a plain global just as much, and the
control run that found it -- the same suite with the change reverted --
should have been the first thing run rather than the fourth.

`tests/desktop.py` is 595 checks to 597. The second of them asserts on a
frame the loop actually drew: the frame drawn inside the quit path never
goes round the loop, so the first version of this check was testing the
quit.


## 0.99.157

**Mail saves drafts** (Gitea #74), which has been open since the Mail
release in 0.99.24: a message being written was lost the moment its window
closed.

It is saved to the account's own **Drafts folder** -- the one whose role the
server declared, and `Drafts` where it declared none -- three ways:

- by hand, **File > Save as Draft**;
- by itself, once the writing has been left alone for **Save Drafts Every**
  seconds (Control Panel > Mail, 30 by default, 0 to turn it off);
- **on the way out**, when a window is closed with something still unsaved,
  which is the case the ticket was written for.

Opening one from Drafts **takes it up to be finished** rather than opening
it to read: the same window Compose uses, filled with its own addresses,
subject and words, no signature appended and nothing quoted. Decided on the
message rather than on which view is showing, so a draft found by a search
opens the same way. Discard throws away the window *and* anything already
filed for it.

### Each save takes the one before it away, and that needed the module

A draft saved five times is one message, not five. Nothing could say which
message had just been appended, though: `email append` answered a status and
nothing else. It fills the result slot with the **uid the server gave it**
now, read from `[APPENDUID <uidvalidity> <uid>]` in the tagged OK -- IMAP's
UIDPLUS, which nearly every server has. A server that does not answers
nothing there, so a caller tells "no uid" from "uid 0", and the older copies
stay, which is that server's limit rather than something to paper over.

**And the stand-in had the hole this release is about.** `tests/mailserve.py`
advertised UIDPLUS in its CAPABILITY and replied `OK appended` with no
`APPENDUID` -- a stand-in claiming an extension and not behaving like it,
which is exactly the shape of Gitea #185 in another protocol. It names what
it made now, and it has a Drafts folder with the `\Drafts` special-use flag,
as a real server does.

### A trap worth the paragraph

`email store "$h" "$uid" +\Deleted` inside a job's script **silently marked
nothing**: the backslash is an escape in an unquoted word, so the flag
reached the server as `+Deleted`, which is a perfectly good keyword that no
`\Deleted` search will ever match. The draft was appended and the old one
stayed, and nothing anywhere failed. `"+\Deleted"` is the fix -- inside
double quotes a backslash before `D` is not an escape, which is also why the
`"\Draft"` beside it had been working all along. Found by running the job's
own script by hand and counting what was in the folder, which is the only
thing that could have found it.

### The checks

`tests/mailapp.py` is 57 to 64: that Save as Draft files it with its words
and the `\Draft` flag, that saving again replaces rather than piles up, that
closing the window files what was unsaved, that opening one from Drafts
gives a window to write in with the text still there, and that a message
left alone saves itself. `tests/mail.py`'s stand-in changes are under every
mail suite already.


## 0.99.156

**A notification's full text and its action are reachable again**
(Gitea #195). Asked for directly:

> when I double click on a notification, it should popup with the full text
> right, and any associated action buttons/text/input should be available?

Two things were missing and the second is the one that mattered.

**The text.** A row in the Notifications window is the time, the priority,
the sender and then `w - 30` columns of the message, cut there. The only way
to read the rest of a long one was Copy and the clipboard.

**The action.** `dt_notify` has taken an action since it was written -- the
words after the message, kept as argv words rather than a string, for
`DT_OPENCMD`'s own reason -- and clicking the *live* note runs it. The
history copy kept the text, the priority, the sender and the time, and threw
the action away. So a note that came and went stacked behind something else
lost the one thing it was for, which is exactly the case a history exists
for.

`dt_notehist_add` keeps `argvn` and `arg0..argN-1` now, and `dt_notehist_run`
runs them. A record is a record: running the action does **not** clear the
note, where a live note's click dismisses it.

Enter on the selected row, or a second click on it (`dt_isdbl`), opens the
note in a window of its own -- who sent it, when, its priority, the whole
text wrapped by `dt_notewrap`, which is the same helper a live note is laid
out with, so a note reads the same in both places, and the action as a
button named after itself:

```
┌─┤ Notification ├────────────────────────────────┤_ x├┐
│ Desktop                    Mon 12 Oct 16:52  normal  │
│                                                      │
│ A message long enough that one row of a list         │
│ cannot hold it: the rest of what it says lives       │
│ past the thirtieth column and used to be             │
│ unreachable.                                         │
│                                                      │
│  dt_note ▄                                 Close ▄   │
└──────────────────────────────────────────────────────┘
```

**Input is a third thing and is not here**, because nothing in this desktop
makes a notification carrying a field to type into: there is nothing for a
window to show. That would be a new kind of note -- a prompt whose answer is
handed to the action -- and wants its own ticket once something needs one.

The opener is `nh_show`, never `notedetail_open`: `dt_new` calls
`<prefix>_open` on the window it is making, so an opener of that name opens
its own kind until the stack runs out, which `tests/540-examples.t` has
checked since 0.99.147.

`tests/desktop.py` is 592 checks to 595: that Enter opens the note with the
end of its text, that the action is offered as a button named after itself,
and that running it closes the window and leaves the note in the history --
the last of which is the half that would catch an action treated like a live
note's click.


## 0.99.155

**A terminal window in the real terminal's colours was see-through over a
picture wallpaper** (Gitea #194). Reported from a live desktop:

> when I start aptitude, or ncurses apps, the background is transparent? and
> does not honour the theme settings theme/terminal or the opacity?

Three different questions wearing one sentence, and the way to tell them
apart was to stop using aptitude. The probe is a program that does what a
curses application does and nothing else: paint a block in its own
background colour, then put the background back to the terminal's own.
Then read the **pens**, because a background is a colour and no glyph
comparison can see one. Over a picture wallpaper, in each combination:

| colours | shade | the program's own background | a cell it left default |
|---|---|---|---|
| theme | 0 | `#0000c8` | `#101820`, the window's face |
| theme | 60 | `#0000c8` | `#73013a`, the wallpaper dimmed |
| **terminal** | **0** | `#0000c8` | **none at all** |
| terminal | 60 | `#0000c8` | `#73013a`, the wallpaper dimmed |

The third row is the fault, and it is the whole of it. `DT_TERMCOLOURS=terminal`
hands the emulator `off`, so `tm_paint` leaves every cell the program never
coloured at `DP_DEFAULT` -- and the kitty protocol composites an under-text
picture **below cells with no background of their own** (`CN_ZUNDER`, Gitea
#175). So the picture came through the program's own window wherever the
program had not painted something, which is most of a curses screen.

What the owner's own settings say matters here: `theme` and `0`, which is
row one and correct. So the report was against one of the other three, and
only one of them is a defect. **Row two and four are the rule working:** a
program that paints its own panels stays opaque at any percentage while the
cells it leaves alone go translucent, which is half a window following the
setting and half ignoring it. That is a design question rather than a bug,
and it is left open deliberately.

### What "the terminal's own colours" can mean over a picture

Nothing. The real terminal's background is behind the wallpaper and
invisible, so honouring it literally is what produced the fault. The ink can
still be honoured, and is: `term colors` takes `-` for **either** half now.
A background of `-` has meant "leave it the terminal's own" since 0.99.150,
which is what a translucent window needs; a **foreground** of `-` is the
other way round -- keep the terminal's ink, name a background -- and
`tw_colours` answers `- $DT_FACE` when the colours are the terminal's, a
picture wallpaper will be real pixels (`dt_imgwill`) and the shade is off.
Without a picture nothing changes: `terminal` still means the real
terminal's colours, which is the whole point of the setting and is correct
when there is nothing composited underneath.

`$DT_FACE` rather than some other colour because the desktop already has an
opinion about this exact question: `console darkdefault "$DT_INK" "$DT_FACE"`
is what a cell left at the terminal's own colour should darken toward. One
answer, in two places, rather than two answers.

OSC 10 goes unanswered where the foreground was left that way, as OSC 11
already did for the background: there is no one colour to name.

### The checks

`tests/desktop.py` is 588 to 592 -- the probe above, in both colour
settings, asserting the untouched cell is the window's face **and** that the
program's own background is still its own, which is the half that would
catch a fix that flattened everything. It is in that file rather than
`tests/apps.py` because the fault needs a picture wallpaper and a pty that
says what a cell measures, which is that file's own harness.
`tests/term_diff.py` is 66 to 68 for the two OSC answers.

The saved-state format grew `nofg` **appended**, never inserted, and both
directions were run rather than reasoned about: this build saved a terminal
and the installed 0.99.154 adopted it and read the screen back, and 0.99.154
saved one (refusing the new form with its own usage message, which is the
other half of the proof) and this build adopted it.


## 0.99.154

**How much wallpaper shows through a terminal is a slider now** (Gitea
#193). Asked for directly:

> the drop down for the background opacity does not work and can we make it
> a slider from 0 to 100? it'll work better

Two things in that sentence, and the first was already fixed: a set row
opens its popup through the pane's own `_drop`, 0.99.150 added the row
without a case there, and 0.99.151 added the case (#186). It had not
reached the machine it was reported from -- installed at 0.99.150, and the
apt repository's own published copy stale at 0.99.149 because the Gitea to
GitHub mirror cannot resolve the name.

A slider is better anyway, and it also makes that whole class of bug
impossible here: `panel_click` never offers a slider to a pane's `_drop`,
so there is no second place to forget. `CP_TERMTRANS=(off 85 75 65 50)` was
five points somebody picked out of the hundred the console actually takes.

It is **set where it is clicked, and dragged**, which is what a slider is
for and the only thing that makes a hundred values usable: pressing a
stepper eleven times is worse than a list of five. `dt_slidepos` in
`widgets/slider.hibr` is the inverse of the position the same file draws,
so the two cannot drift, and `panel_click` hands the value to a new pane
callback, `<pane>_slide id key value`. A pane without one is untouched: its
slider still selects on a click and steps on the arrows exactly as before.
Both existing sliders have one -- the shade, and Windows' own Tiled Main,
which had never been clickable either.

Three details that are not details:

- **The value is clamped and snapped by the pane, not by the track.** A
  track eleven columns wide has eleven positions whatever its range is, and
  the step belongs to the setting: the shade moves in tens, Tiled Main in
  tens of a 30-70 range.
- **A number beside the label, not inside it.** `Tiled Main
  ($DT_TILEMAIN%)` as one string is a string `tools/strings.py` cannot see
  and no catalogue can hold, so that row has been untranslatable since it
  was written. A row carries `after` now, appended once the catalogue has
  been asked, and "Tiled Main" is a translatable string for the first time.
- **A value saved by the dropdown is snapped once.** 85, 75 and 65 would
  leave the dot between two of the eleven positions for ever, so an arrow
  key would look as though it had done nothing. `DT_SETVER` is 10 and
  `cp_setver10` rounds it, which is the mechanism this project already has
  for a default that must reach a file somebody has already saved.

0 is what `off` was -- nothing shows through -- and it is drawn as `(0%)`
rather than as a word, which keeps the label free of a term needing
translation and says the true thing: the setting is a percentage of the
picture's own brightness.

### The checks

`tests/apps.py` is 587 to 590 and the panel part's plan follows: that the
row is a slider over 0 to 100 with its percentage beside the label, that a
click on the track sets the value that column stands for, that a drag
across it goes on setting it, and that Windows' own Tiled Main is clickable
in its own units -- the one `_slide` that does something besides store a
number, since it retiles. The dropdown walk loses the row it gained in
0.99.151, since there is no dropdown there to walk any more.
`tests/540-examples.t` learns `_slide`'s arity, so a pane that declares one
with too few names fails the way every other callback does.


## 0.99.153

**Mail now connects** (Gitea #191, #192). 0.99.152 made the client able to
say what was wrong; this is the two things it then said, on the owner's own
server:

```
in tls  fail  its issuer is not trusted by this machine
out auth fail smtp.home.arpa refused the login for osakka: 535 5.7.8 ...
```

Two faults, neither of them a wrong password, and neither fixable from
inside hibr before this release.

### A certificate authority of your own (#191)

Their IMAP server presents a leaf signed by `gibnet Intermediate CA` --
itdlabs' own. The chain is on the machine, at
`/etc/ssl/certs/gibnet-im-root.crt`, with no hash symlink in that directory
and not in `ca-certificates.crt`, so nothing in OpenSSL's default verify
paths finds it:

```
$ openssl s_client -connect imap.home.arpa:993
verify error:num=20:unable to get local issuer certificate
$ openssl s_client -connect imap.home.arpa:993 -CAfile /etc/ssl/certs/gibnet-im-root.crt
Verify return code: 0 (ok)
```

So a person with their own CA had two ways to reach their own server and no
third: change the machine's trust store, which is root's decision and every
program's business, or `HIBR_TLS_INSECURE`, which verifies nothing at all.
In a desktop neither is reachable -- the Mail app, the Vault, the Calendar
and the Browser all connect through one layer with no environment of their
own.

`HIBR_TLS_CAFILE` names a file of certificates to trust **besides** the
system's own. It is applied after `SSL_CTX_set_default_verify_paths` with
`SSL_CTX_load_verify_locations`, in the one place a context is made, so it
reaches `/dev/tls`, `connect`, and the mail, WebDAV and vault modules alike
-- all of which speak through the same forked relay. Empty counts as unset,
which `HIBR_TLS_INSECURE` deliberately does not.

It is **not** documentation around OpenSSL's `SSL_CERT_FILE`, which that
call already reads: that variable *replaces* the default CA file, so on a
machine whose public roots live only in that file naming a private
authority silently stops every public certificate verifying. Added and
substituted are different things, and `tests/998-tls-cafile.t` is built
around telling them apart -- two authorities, a server signed by each, and
one named through each mechanism:

```
signed by the named CA: hello
signed by the other, not named: refused
one named each way: hello hello
named empty: refused
```

Run under 0.99.152's own shell it reads `refused`, `refused`,
`hello refused`, `refused` -- watched failing before it was trusted.

### The login a server wants (#192)

With the certificate trusted, the same account still failed:

```
hibr: email: imap.home.arpa refused the login for osakka:
      NO [AUTHENTICATIONFAILED] Authentication failed.
```

The login was `osakka` and the server wants `osakka@home.arpa`. Same
password, same host, same port:

```
$ email check T          # -u osakka@home.arpa
status=0
$ email check T -o       # smtp.home.arpa:25, AUTH PLAIN -> 235
status=0
```

Both directions had been working the whole time behind one field. That is
the common shape wherever mailboxes are virtual, and the field exists
precisely because a login and an address are two different things
(Gitea #157) -- but nothing told anybody which of the two their server
wanted, and "Authentication failed" reads as a wrong password.

`email check NAME [-o] -u LOGIN` asks with a different login and changes
nothing: the record is swapped for the one call and put back. The ladder
uses it for **one** extra attempt, only where the account's login has no
`@` and its address does, and reports rather than rewrites -- a server that
locks an account out after a handful of failures is the normal case, and
which login to use is the account's own to change:

```
out auth  fail  smtp.home.arpa refused the login for osakka: 535 5.7.8 ...
out login ok    the same password is accepted with osakka@home.arpa as the
                login: this server wants the whole address, so set the
                account's login to it
```

The TLS rung and detect's `cert` answer now name `HIBR_TLS_CAFILE` as the
fix. Neither ever suggests `-K`: a diagnostic that teaches people to turn
verification off is worse than one that says nothing.

### The checks

`tests/998-tls-cafile.t` is new, and `tests/mail.py` is 43 to 46: that
`check -u` logs in where the account's own login is refused, that the
account file is unchanged afterwards, and that the ladder grows the rung --
driven against a stand-in whose own user *is* the address, since the one
the other checks use accepts `pat`, which is the wrong way round.


## 0.99.152

**Mail could not say what was wrong with it** (Gitea #187, #188, #189).

From an afternoon of not being able to connect, where **two** things were
wrong at once and the client said neither:

> I am not sure how to troubleshoot email connectivity issue ... it keeps
> failing

### A port and a security setting that cannot work together (#187)

```
host imap.home.arpa    port 993    security starttls
```

993 is TLS from the first byte; STARTTLS is the port-143 method. Measured
against the real server:

```
plain connect to 143 -> * OK [CAPABILITY ... STARTTLS ...] Dovecot ready.
plain connect to 993 -> (nothing: it waits for a handshake)
```

`email account set` took that without a word. It refuses it now, naming the
fix -- 993, 995 and 465 against anything but `tls`, and 143, 110, 587 and 25
against `tls`. Only the known ports are judged: an unusual one is nobody's
business.

**And four errors became one cause.** A failed connection printed the
handshake failure, then "closed the connection or timed out", then "did not
greet as an IMAP server: (nothing)", then "the connection broke while
writing" -- one cause and three consequences, each reading as another
problem. A connection carries a `dead` flag now and says so once; the rest
go to the debug log. Four lines to two, and the two that remain are the TLS
layer naming the real cause and the module saying the connection broke.
**Not one**, deliberately: going further would mean inferring "the relay
died at the handshake", which cannot be told apart from a server that
accepts TLS and then stays silent, where going quiet would be worse.

### A Test that says which rung it stopped on (#188)

Asked for directly:

> Should we have on the mail control panel (test) that will test both
> incoming and outgoing for authentication and accessibility?

Accessibility and authentication are different questions, and that is the
point: one "it does not work" hides two faults, which is what had happened.
Control Panel > Mail > **Test Connection** runs
`examples/desktop/lib/mailtest.hibr` as a job and draws a rung per step for
each direction, filling in as the answers arrive:

```
  ✓ incoming name     imap.home.arpa
  ✓ incoming port     993 open
  ✕ incoming tls      the certificate does not verify here -- its issuer is not trusted by this machine
  · incoming greet    the TLS connection did not open
  ✓ outgoing greet    220 mail-2.uk.home.arpa ESMTP
  ✕ outgoing auth     smtp.home.arpa refused the login for osakka: 535 5.7.8 ...
```

The login step is the **real** one: `email check NAME [-o]`, which is
`im_open` and the same SMTP conversation `email send` has, stopped once
authenticated. Nothing is sent. A check that proves something near what
sending does is worth nothing the day the two differ.

Two things the ladder knows that a yes/no cannot. A TLS step **reads a
byte** before believing the handshake, because a connection that is about
to fail verification still opens -- reporting at the open is how a broken
certificate reads as working. And `auth ok` over an unencrypted port says
*"accepted -- but this port is not encrypted, so the password went in the
clear"*, because "ok" is exactly where somebody stops reading.

### Detect, so the pairing is found rather than typed (#189)

`--detect` probes the candidates and takes what answers, **ranked by what
the pairing protects** rather than by which replied first -- "the first to
answer" settles on the plain port whenever a server offers both. Three
things it had to learn, each because it was wrong first:

- a TLS port whose **certificate** is untrusted is not "nothing here": calling
  it that made detect choose the plain port, whose STARTTLS upgrade meets
  the very same certificate a moment later. `cert` is its own answer and
  beats a plain port that merely works;
- **a greeting is not an offer**: a port that greets is checked with a real
  `CAPABILITY` or `EHLO` before being written down as `starttls`, or detect
  would produce exactly the pairing #187 refuses;
- a `plain` pick says what it costs, and a `starttls` pick made where a TLS
  port was refused for its certificate says that the same certificate is
  waiting at the upgrade.

The candidates can be named (`-i`, `-o`), which is what a server on an
unusual port needs and what lets a test point this at ports it may bind.

### The checks

`tests/mail.py` is 27 checks to 43. The pairing refusals and that a refused
pairing leaves no half-made account; that a broken connection says so once,
watched failing against the old code; `email check` both directions, right
and wrong, and that the outgoing one delivered nothing; the ladder's five
rungs; and detect's ranking, its STARTTLS proof and its warnings.
`tests/apps.py` checks the pane offers the row.

### Honest about what is still not known

A `starttls` candidate is proved to *offer* the upgrade, not to present a
certificate this machine trusts -- that needs the upgrade itself, which
only the module does. Detect says so where it matters, and Test is the
thing that answers it.

## 0.99.151

**Email two-factor could never log in: the master password hash was sent
form-encoded inside a JSON body** (Gitea #185).

Reported from a live, self-hosted Vaultwarden: *"I am no longer sure how to
login, it's not giving me the 2fa error anymore"*. The message named the
endpoint, and that was the whole finding:

```
vw: https://vault.home.arpa/api/two-factor/send-email-login: 400:
    Username or password is incorrect. Try again.
```

`send-email-login` is the call that asks the server to **mail** a code.
Reaching it means the token request had already succeeded and the server had
answered "two-step required, provider 1" -- Vaultwarden offers the provider
list *only once the password is correct*. So the password was right, and the
failure was in the follow-up request. "It stopped asking for 2FA" was the
symptom of never getting there.

### One variable, two encodings

```sh
h := vwk hash "${o[@]}" ${VW_STDIN:+-s}
q := vwk form "${VA["email"]}"
h := vwk form "$h"        # h is now the FORM-ENCODED hash
...
grant="...&password=$h&..."                        # right: the grant is urlencoded
"{... \"masterPasswordHash\":\"$h\"}"              # wrong: this body is JSON
```

A base64 hash holds `+`, `/` and `=`, and form encoding turns them into
`%2B`, `%2F` and `%3D` -- measured, `abc+def/ghi=` becomes
`abc%2Bdef%2Fghi%3D`. The server checked a different string, failed its own
`check_valid_password`, and answered the same words it gives a bad login.
**Every** email-two-factor login has failed this way since the two-factor
work landed in 0.99.146.

`hf` carries the encoded hash into the grant; `h` stays raw for JSON bodies.
That is the `MLME` trap of #157 a third time: a value answering two
questions, right up until the day they differ.

### Why nothing caught it -- two holes, either enough on its own

**The provider was never driven.** `tests/vw.py` ran `--2fa 0`
(authenticator) and `--2fa 7` (WebAuthn). Provider **1** is the only one
that makes a request of its own *before* a code is typed, and no check had
ever reached that endpoint.

**The stand-in agreed with whatever it was sent.** `tests/bwserve.py`
answered `200` to `/api/two-factor/send-email-login` without reading the
body -- while its own comment said the endpoint "wants the master password
hash the client already has". The comment knew; the code never asked. A
stand-in that accepts anything cannot fail, and a check written against it
is a check of nothing.

### The checks

`bwserve.py` now verifies the email and the hash on that endpoint and
answers Vaultwarden's own 400 when they do not match. `tests/vw.py` drives
`--2fa 1`: that asking for an emailed code moves the server's own `mailed`
count, and that the code then logs in. Both were watched failing against the
old client -- 30 passed, 2 failed -- before the one-line fix made it 32.

### And the last release's own new row never opened (Gitea #186)

> the drop down for the shade level does not drop down, it's only on return
> it changes

**Wallpaper Shows Through** drew, held its value and applied it, and opened
no popup. `panel.hibr` gives a `set` row its popup only through the pane's
own `<pane>_drop`, and 0.99.150 added the row and its `terminal_do` case and
**not** its `_drop` case -- so the key fell through that `case` with no
branch, nothing opened, and the row quietly degraded to cycling on enter.
One line.

The guard is the point, since a row that silently does less is what nothing
notices: `tests/540-examples.t` walks every pane that offers a `_drop`,
takes each `set` row that is not a checkbox or a slider -- the two
`panel.hibr` itself excludes -- and fails when that row's key has no case.
It was wrong twice before it was right, each time found by running it: a key
belongs to **its own row**, so the rows are walked in order rather than the
keys grepped out (the first version called every `action` row a missing
dropdown), and a case label may be one of several, `locktime | syncevery |
clear)`, so it is followed by a `)` **or** a `|` (matching only the first
shape called two handled rows unhandled). With both right it finds exactly
one fault across every pane: this one.

### And a mark that named only what would not work (Gitea #190)

> what's going on, I can no longer restart successfully, this is quite
> recent?

The restarts were succeeding -- three in a row in the desktop's own log,
each keeping and restoring every window -- and the mark was telling the
truth:

```
hold server's image : /usr/bin/hibr (deleted)   <- the inode apt replaced
the desktop's image : /usr/bin/hibr             <- current, so it worked
```

`DT_UPSESSION` is right and was useless. *"only a new session takes it, not
Restart Desktop"* names the thing that will not work and never the thing
that will, so whoever has just restarted reads it as a failure. It says what
to do now, and what that costs -- a restart keeps the programs in terminal
windows, because they are children of the restarting process, and a new
session does not. That trade is the whole reason to put it off, so it
belongs in the sentence.

Which it could not be, because **a note was cut at the screen's width**, one
line, never wrapped: on an 80-column desktop the advice was exactly the half
that fell off. A note is now as tall as its own text (`dt_notewrap`, over
the textarea widget's own wrapper, which breaks after the last space that
fits), and the stack below it starts under the one above rather than a fixed
three rows down. A short note is three rows as before, so nothing that fits
has moved.

## 0.99.150

**Over a picture wallpaper a shadow was an opaque black band and a terminal
was see-through: both now take the picture's own colour, scaled** (Gitea
#184).

Reported from a live desktop:

> (1) the terminal has a transparent background ... (2) the shadow is black
> and not semi transparent (percentage)?

Both true, and both the same thing wearing different clothes.

### What was measured, before anything was changed

A loud two-colour wallpaper in pixels, a 9x18 cell, a Terminal window, and
the pens the console actually emitted:

| cell | `DT_TERMCOLOURS=theme` | `DT_TERMCOLOURS=terminal` |
|---|---|---|
| the text the shell wrote | `48;2;16;24;32` | `0;39;49` |
| plain wallpaper, outside the window | `0;39;49` | `0;39;49` |

`0;39;49` is default fg and bg -- **the same pen the wallpaper itself
writes** -- and the picture sits at `CN_ZUNDER`, which the protocol defines
as *under cells with non-default background colours*. So in `terminal` mode
every cell of the window's interior showed the picture.

And the shadow, in the same session:

| | pixel mode | half-block mode |
|---|---|---|
| shadow cell below the window | `48;2;9;13;18` | `48;2;138;116;50` |

`(9,13,18)` is `DT_FACE x 0.55`. `(138,116,50)` is the **wallpaper's own
yellow** `(250,210,90) x 0.55`. Half blocks had been right all along.

### One root

`wm/session.hibr` calls `console darkdefault "$DT_INK" "$DT_FACE"`, added so
a shadow over a terminal's plain uncoloured output darkens at all. In pixel
mode `wm/wallpaper.hibr` *deliberately* writes `console pen default default`
so the picture shows through. The two met.

**"Default background" answers two different questions** -- *"the real
terminal's colour, which we cannot see"* and *"let the wallpaper through
here"*. `darkdefault` answers the first and broke the second; the
`terminal` colour setting chooses the first and got the second. The same
shape as `MLME` in #157, in colours.

### The answer, which is one operation for both

**Paint the cell as the picture's own colour under it, scaled.** A cell has
no alpha -- the picture is composited below cells that have a background
colour -- so translucency here is a *computed* colour, not transparency, and
it has to be computed per cell because it is a different colour in every
one. That is also what makes it cost nothing elsewhere: what comes out is
ordinary RGB cells, so `hold`, the emulator and `tools/screenshot.py` needed
no change at all.

The console keeps **one averaged colour per cell** with an under-text
picture, worked out in the pass that already walks those pixels to hash
them -- about 49 kB for a full screen, hung off the picture's own region so
it survives an `imgkeep` and dies with it.

- `cn_darken1` darkens from that colour where a default cell sits over a
  picture; `darkdefault` still decides everywhere else, which is still right
  over a glyph wallpaper and for a terminal's own plain output.
- `console translucent pct [colour]` does the same for a *write*: while it
  is set, a cell written with no background of its own takes the picture's
  colour there scaled to `pct`, or `colour` where there is no picture -- so
  an app asks unconditionally and is still right on a glyph wallpaper, in
  half blocks, and on a terminal that cannot place a pixel. It ends at the
  next flush as well as on `off`, so no caller can leak it into whatever is
  drawn next.
- `term colors t #fg -` is new: a colour for the text and the background
  left default, because one colour for the whole emulator cannot say what a
  hundred cells should each be. `nobg` is a new field in the saved state,
  **appended**, and both directions were *run* rather than reasoned about
  (#157's rule): this build wrote a save carrying `nobg`, the **installed
  0.99.149** read it -- `term: ...: skipped nobg, not known here`, and the
  terminal came up -- and this build read a save written by that one.

### The setting

`DT_TERMTRANS` -- Control Panel > Terminal > **Wallpaper Shows Through** --
is off, 85, 75, 65 or 50: how much of the picture's own brightness is kept,
so the smaller numbers are darker and easier to read over a busy photograph.

It is **its own setting**, deliberately. `DT_TERMCOLOURS` says *whose*
colours the text and background are; this says how much of the wallpaper
shows. They are separate questions, and the day they were one value is what
this release is about.

Two percentages, not one: a shadow reuses `DT_SHADOW_PCT`, which is already
per theme, because a shadow wants to be harder than a window interior.

### The checks

Six in `tests/kitgfx.py`, all watched failing first, and the arithmetic is
exact because the fixture picture is one colour: a shadow over it is
`(5,100,5)` and not the theme's face darkened; the cell beside it still has
no background of its own, so the picture is still composited there; a
translucent write is `(8,150,8)`; the same write with no picture under it is
left at its default; with a fallback given it is the fallback; and a shadow
falling on a translucent cell is `(4,75,4)` -- the two compose, which is the
case that silently looks wrong if the substitution is ever moved to flush
time.

Half-block mode was the oracle throughout: it already produced
`(138,116,50)` for the shadow, and pixel mode now produces the same.

### Honest about what it is

A translucent window is opaque cells in a computed colour. Where the picture
changes under a window that is not redrawn -- a wallpaper swapped while a
terminal sits idle -- the window keeps the colours it last computed until
something redraws it; `term_dirty` notices the setting and the theme moving,
and a new wallpaper forces a frame of its own. And the ink is the theme's:
light grey on a bright photograph at 85% is hard to read, which is what the
lower numbers in the setting are for.

## 0.99.149

**A render cleared the flag that said the page it had just read was already
out of date, so a Browser window could keep a blank new document for ever**
(Gitea #182).

Found by 0.99.148's own release gate, as one failure in `tests/web.py` with
the other 41 suites green and nothing in that release able to reach it.

### The check's name was wrong about what failed

```
 1|··┌─┤ Browser [Page B] ├──────────────────────────────────────┤_ □ x├┐··
 2|··│ Page B ✕ │ +                                                     │··
 3|··│ ← → ↻  file:///tmp/hibr-webrep-f5gw27br/b.html                   │··
 4|··│                                                                  │··
   ... every page row blank, status line the idle one, not "Loading…" ...
```

The click *did* follow the link -- the title, the tab and the address all say
Page B. The drawing of page B is what never happened, and never happened
again. 0 of 6 unloaded, **3 of 16** under three spinners on four cores, with
the link at the same cell in every run including the failures, so it was
never the "a click aimed at a column worked out from text that has not
arrived yet" trap that the tab close box had.

### What the frames said

`browser_draw` instrumented to log what each of its own questions answered,
in a patched copy of the app so the tree stayed clean. All three failures,
identically:

```
DBG st=idle    dirty=dirty render=ok   draw=ok page=a.html
DBG st=loading dirty=dirty render=ok   draw=ok page=b.html
DBG st=idle    dirty=clean render=skip draw=ok page=b.html   <- and six more
```

The frame that learns the load has finished finds `dirty` **already clean**
and skips the render, so the empty document rendered on the frame before is
redrawn for ever.

### The cause, which is a rule this file already carries

`wb_call` waits for its own reply by calling `wb_pump`, and `wb_pump` hands
every message that is *not* a reply straight to `wb_event`. So an event that
arrives while a render's `Runtime.evaluate` is in flight is dispatched
**inside that render** -- `Page.loadEventFired` among them, which sets
`loading = 0` and `dirty = 1`. And `wb_render` ended with:

```c
	jv_free(v);
	t->dirty = 0;
	return HIBR_OK;
```

wiping the flag that had just been set. The next frame then had `loading`
false and `dirty` clean: nothing to re-render, and nothing to ask for
another frame with.

`t->dirty = 0` happens **before** the extraction now, never after, so
anything arriving during it survives -- the same rule as the `SIGCHLD` flag,
which is cleared before the wait or a child exiting during it waits for the
next one. The two failure paths put it back, since nothing else would.

### And the other half, in the app

With the module fixed the common case heals itself: the frame that rendered
an empty document was a `loading` frame, so it asked for another, and that
one finds `dirty` set and renders again. What it does not cover is an event
landing inside the **last** render, where `loading` is already false: the
flag is then set correctly and nobody asks for the frame that would act on
it, because a settled page sends nothing more to wake the desktop with.

So `browser_draw` asks for one when the tab is still dirty after it has
drawn. That is a condition, not a timer, and it cannot spin: the frame it
asks for renders, and asks again only if another event lands inside that
one.

### The checks

`tests/web.py` gained one, and **the window it needs is made certain rather
than left to luck**: the fixture page's last element is a 600 ms busy script,
so an eval issued while it spins cannot run until the script ends, which is
when `load` fires -- the event is guaranteed to land inside the render. It
reads `clean` 4 times of 4 against the old module and `dirty` 4 of 4 against
the new one.

It asserts `idle` beside `dirty`, which is the check's own test: without it
a run where the load event had *not* been consumed inside the render would
pass for no reason at all.

And it waits for the **navigation to commit** rather than pausing for a fixed
time before rendering -- which is this release's own lesson applied to its own
guard. The page spins in its `head`, so "the navigation committed" and "the
page is busy" are the same instant and no pause is needed between them; the
wait measures 30-40 ms in practice and the check prints how long it took, so
a run where the commit never arrived says so instead of reading as the bug.

### One check this release's gate caught, which was not this release

`tests/desktop.py`'s *and on to the one after that* failed with the menu bar
**shut** -- the screen kept, for the first time, because `all.py` defaults
`V=1` as of 0.99.148. The scenario is F10 then right twice, and the check
asks for the second menu's first item, so what went missing is the bar
opening at all. Unloaded it does not fail: 0 of 20 in the suite's own key
shape, and 0 of 10 with the screen read between each key, those reading
`Screen Saver -> Bump -> Bump Twice` every time -- the walk is not what
breaks. Filed as Gitea #183, with the already-recorded ~4% key-delivery
flake in that same suite named as a likely sibling, and left open rather
than papered over.

## 0.99.148

**`tools/screenshot.py` fed escape payloads to the grid as text, and could
not show a picture at all** (Gitea #181).

The tool makes every picture in the documentation, and its claim is a strong
one -- *"not a mockup ... provably a real render of real output"*. Its escape
handling ended with:

```python
if ch == "\x1b":
    i += 2
    continue
```

Two bytes, whatever the escape was. So an **APC** -- the kitty graphics
protocol, `ESC _ G <control> ; <base64> ESC \` -- had its control and its
tens of thousands of base64 characters written into the grid one at a time,
and an **OSC** likewise: the harness's own idle marker, `ESC ] 7777;idle;N;M
BEL`, became thirteen characters of text at the cursor. Measured, before the
fix:

```
old grid row 0: 'hello7777;idle;0;4\x07'
```

Which is worse than the text appearing, because the cursor advances with it:
**every cell written after the marker in that run lands thirteen columns
off**. The shipped `examples/desktop/img/desktop-strip.png` is missing the
word **File** from its menu bar for exactly that reason.

### And the larger half: a bitmap is not cells

Once the payloads are skipped correctly, a screenshot of a desktop with a
**pixel** wallpaper showed the windows over an empty ground -- because the
renderer drew cells, and the cells a picture covers are deliberately blank
(ADR 0037). A tool whose point is that the documentation cannot show
something hibr does not do was unable to show the thing hibr does.

A kitty transmission is now kept whole, base64-decoded, inflated when the
control says `o=z`, **scaled to the cells its own placement names** -- the
wallpaper is sent at half size by default and the terminal scales it, so a
faithful render has to scale it the same way -- and pasted under the text in
`z` order. A cell with no background colour of its own is left alone, which
is the contract the wallpaper itself depends on.

`--cell 9x18` is what makes pixels possible: it tells the pty what a cell
measures, which is what a terminal that can place pixels reports. Without
it, hibr draws half blocks exactly as on a terminal that cannot -- so the
flag is also how a screenshot of the fallback is taken on purpose.

A **sixel** is skipped rather than drawn, and that is deliberate: it is
paint, with no placement to read a rectangle from, so there is nothing
faithful to put on the page. The comment says so where the next reader will
look.

### The checks

`tests/shots.py`, ten of them, now in `all.py`. Six drive the renderer with
crafted streams -- the idle marker, an OSC ending in ST rather than BEL, a
kitty picture that must stay out of the grid and come back as a picture, a
**chunked** picture that must be one picture and not several, a zlib payload
that must be inflated rather than taken for pixels, and a sixel whose bytes
must not reach the grid. Three drive a real desktop with a picture
wallpaper, through the tool, and read the bitmap back out of the PNG it
wrote. All of them were watched failing against the old renderer.

### Not in this release

The eight images under `examples/desktop/img/` were made with the broken
renderer and are stale as well as corrupted -- `desktop-strip.png` predates
two Control Strip modules. **How each was made was never recorded**, so
regenerating them means inventing eight sessions and looking at each one,
and it wants a manifest beside them so the next person does not have to
invent them again. That is its own release, and it is worth more once the
tool it runs is the fixed one.

### And one check this release's gate caught, which was not this release

`tests/web.py`'s *a click on a link in the window follows it* failed the
gate, with the other 41 suites green and nothing in this release able to
reach it -- the changes are a tool, a suite, three documents and the version.
Eleven earlier gate logs read `web ok 18 passed` (Gitea #182).

**The check's name is wrong about what failed, which is the whole of the
finding.** The screen when its eighteen-second wait ran out:

```
 1|··┌─┤ Browser [Page B] ├──────────────────────────────────────────────┤_ □ x├┐··
 2|··│ Page B ✕ │ +                                                             │··
 3|··│ ← → ↻  file:///tmp/hibr-webrep-f5gw27br/b.html                           │··
 4|··│                                                                          │··
   ... every page row blank, and the status line the idle one ...
```

The click **did** follow the link: the title, the tab and the address all say
Page B, and the status line is not `Loading…`. What never happened is the
drawing of page B, and it never happens again either. Reproduced by running
that one scenario alone: 0 of 6 unloaded, **2 of 24** under three spinners on
four cores, with the link at the same cell (8, 27) in every run, failures
included -- so it is not the "a click aimed at a column worked out from text
that has not arrived yet" trap that the tab close box had.

What the code says about that screen, which is as far as this release took
it: `wb_render` fills a fresh blank grid before placing the text it
extracted and clears `dirty` only on success, so a blank page area is a
*successful* render that found no text -- a failed one would have left page
A's cells. That leaves the tab clean and `loading` false, and
`browser_draw`'s only re-arm is `[ "$st" = loading ] && dt_want 400`. No
frame is asked for, and a frame caused by something else would find nothing
to re-render. `wb_event` sets `loading = 0` and `dirty = 1` in the same
event, so the last render of a navigation is always the frame that learns
the load ended. Not root-caused; the ticket carries the two candidate fixes
and why the module-side one is preferred.

### A gate failure now keeps its screen

`screen.check` prints the screen a check failed on only when `V` is set, so
`build/test-logs/web.log` held one line -- the check's name -- and the screen
above had to be re-found by reproducing it. A release gate runs unattended,
which is exactly where a one-off needs to leave its evidence behind, so
`tests/all.py` defaults `V=1` for the suites it runs. It costs nothing on a
pass: the dump is printed only beside a `FAIL`.

## 0.99.147

**An account could hold only one address, so mail arriving at an alias read
as somebody else's** (Gitea #157, the second of its three parts; the first
was the login field in 0.99.145).

Reported in the same breath as that one: *"let's add other email addresses?
(I have multiple tied into that one)."*

`email account set -a` takes them, comma-separated and kept as given. `-e`
stays the address a message is sent from by default and the one the account
is listed under; `-a` is what **also counts as you**. `email accounts` hands
the lot back as `addresses`, an array with `-e`'s first, so the comma stays
the file's business and nothing downstream splits a string of its own.

### The file grows a field, and both directions were checked rather than
### reasoned about

The fourteenth tab-separated field is **appended, not inserted**, so the
thirteen an older build knows are where they were. `ml_confparse` reads
positionally and counts nothing, which makes the compatibility real in both
directions -- and both were run:

- this build reading a thirteen-field line: the account reads, with no other
  addresses;
- **the installed 0.99.146** reading a fourteen-field line: its thirteen
  fields come back right and the fourteenth is never looked at.

The second of those is the one worth doing by hand. A claim that an old
build tolerates a new file is the kind that is true until it is not.

### "Me" is now any of my addresses

The Mail app kept one address per account in `MLME` and compared the
sender's against it. That is still there and still means **the one I write
as** -- what changed is that the question "is this message mine" is now
asked of `MLMINE`, a set of every address of every account, keyed by the
lowercased address. The two questions were one variable and are now two,
which is the whole fix: a thread with a reply sent from an alias reads
`Zoë Ünal, me` rather than naming you as a correspondent, and your own
alias stops turning up in the senders Compose suggests from.

### Writing from one of them

Compose's From row is a dropdown when an account has more than one address,
and exactly what it always was when it has one -- the row is not even
registered as something to click in that case. Nothing in the module had to
change for it: `email build -f` has always taken the address, and the SMTP
envelope already follows the message's own From header, so a message written
from an alias leaves with that alias in both places.

The account dialog grows an **Other addresses:** row, comma-separated, under
the address it belongs to.

### The checks

Three in `tests/mail.py` -- the list comes back with the sending address
first, a line written before this release still reads, and clearing them
leaves the sending address alone -- and two in `tests/mailapp.py`: a message
from one of my other addresses reads as **me** on the screen, and the app
knows the whole set. The app pair was watched failing against 0.99.146.

And a third dialog row shifted the tab order a suite types through, exactly
as the Login row did in 0.99.145 -- two checks in `tests/mailapp.py` broke
again, in the same place, for the same reason. The walk follows the dialog's
own order, so adding a row means reading every check that tabs through it.

### What is left of #157

The third part: *"let's add mail"* read as adding **your** account, which
the login field unblocked in 0.99.145. Nothing further is owed there, so
#157 closes with this.

## 0.99.146

**An account with two-factor turned on could not log in to the vault at
all** (Gitea #156). Reported as: *the vault needs two-factor support.*

`vw login` posted the password grant and nothing else, so the 400 naming the
server's providers reached `vw_die` and the desktop's login box showed "Two
factor required." with nowhere to type a code. A password manager that
cannot log in to an account with two-factor on is one that encourages
turning it off.

### The protocol, read rather than remembered

The ticket's own table of provider numbers was **wrong**, and that is worth
recording because it is what a second reader would have built against.
Vaultwarden's `src/db/models/two_factor.rs` says:

| | |
|---|---|
| 0 | Authenticator |
| 1 | Email |
| **2** | **Duo** (the ticket said 3) |
| **3** | **YubiKey** (the ticket said 4) |
| 4 | U2f |
| 5 | Remember |
| 6 | OrganizationDuo |
| 7 | Webauthn |
| 8 | RecoveryCode |

Read from source along with it, and all of it is in `tests/bwserve.py` as
the server's own strings rather than plausible ones -- a stand-in written
from the same memory as the client would agree with the client and prove
nothing:

- **wanted**: 400, `{"error":"invalid_grant","error_description":"Two factor
  required.","TwoFactorProviders":["0"],"TwoFactorProviders2":{...}}`
- **a wrong code**: **401**, "Two-step token is invalid. Try again." -- not a
  400, which is what lets a client tell "ask" from "that was wrong"
- **remembered**: `TwoFactorToken`, capitalised, in the *successful* reply
- **the fields**: `two_factor_token` and friends, each with an `uncased`
  alias without the underscores, so the official clients' `twoFactorToken`
  spelling works too
- **an emailed code is not sent by the refusal**: the client has to ask, with
  `POST /api/two-factor/send-email-login`

### What it does

`vw login` reads the providers and asks for a code: the authenticator, an
emailed code or a YubiKey OTP, and `--recovery` for a recovery code, which
no server ever lists and every server takes. Duo, U2f and WebAuthn each need
a browser and are **named and refused** rather than half-tried. The code
comes from the second line of standard input when the password came that way
(`VW_STDIN=1`), and from the terminal otherwise -- with echo **on**, because
a code is not a secret worth hiding and a YubiKey types forty-four
characters a person needs to see go in.

A device the server was told to remember is sent as provider 5 on the next
login's first post, so an account with two-factor on logs in with nothing
typed.

### Where the remembered device is kept, and why that needed C

It cannot be kept the way the refresh token is. `vwk enc` uses the vault's
key, and **the vault's key arrives in the very reply this token exists to
obtain** -- so a token encrypted that way could never be read in time. In
the clear it would hand the second factor to anyone who can read the account
file, which is the one thing a remembered device must not do.

So `vwk` gains **`menc` / `mdec`**, which use the key the master password
makes -- held only between `vwk hash` and `vwk open`, which is exactly the
window the login needs it in. The order matters and is now explicit:
`vwk open` *spends* the master key, so the token is stored before it.

Two ways of locking somebody out, both avoided and both tested:

- a remembered token the server has stopped honouring (it drops them on a
  password change) comes back as **the providers again**, not as a wrong
  code -- so the client asks, and forgets what it had;
- a **wrong password** carries no provider list, which is how it is told
  from a dead token. Reading the status alone would throw away a good
  remembered device on a mistyped password.

### The desktop's own box

The login dialog grows a **Code** field -- only once a server has asked for
one, with the provider named, since an authenticator reads differently from
an emailed code. What it grows on is `vw`'s **status 4**, not its English: a
number the box can act on without matching a sentence.

And a code that does not work clears **the code and nothing else**. The
password was accepted to get that far; clearing both would make a mistyped
digit cost the password as well, which is how people end up turning
two-factor off.

### The checks

Eight in `tests/vw.py` and four in `tests/vault.py`, every one watched
failing first. No code and the provider named; a wrong code in the server's
own words; the code on the second line logging in; the remembered token kept
and not in the clear; the next login not asked at all; a wrong password
keeping the token; a forgotten token falling back to asking; a WebAuthn-only
account refused by name. Then the desktop's: no Code field until a server
asks, the field and the provider when one does, a wrong code refused in the
box, and the right one logging in without asking for the password twice.

Two bugs found by those checks rather than by reading, both this tree's own
traps: `${tf["TwoFactorProviders"]+x}` is **empty when the entry holds only
a map**, and a JSON array is a map -- so "two factor required" read as
"wrong password" until the first element was asked about instead. And
`|| { ok=0; st=$?; }` reads **the assignment's** status, not the command's,
so the dialog never saw status 4 until the two were swapped.

## 0.99.145

**The Mail account dialog had no login field, so an account whose login is
not its address could not be made from the desktop at all** (Gitea #157).

Reported directly: *"my username on mail is not an email address, it can be,
but not necessarily, so I cannot login using our integrated email client."*

**Nothing was broken underneath.** `email account set -u` has taken a login
for as long as the module has existed, and `conf.c` has always defaulted it
to the address when it is not given. The desktop simply never offered it:
`MLAD_ORDER` listed eleven fields and `user` was not among them, so every
account the dialog made logged in as its address, and a server wanting a
bare username refused it.

One row, labelled **Login:**, between the address and your name. Three things
about it are decisions rather than details:

- **Empty means the address**, which is what the module already does with an
  account it is not given a login for -- so no existing account changes and
  nobody who logs in with their address sees a difference. The row says so
  in place, `(from the address)`, the way the server and port rows already
  do.
- **It is sent whatever it holds, empty included.** Passing `-u` only when
  the field has something would mean a login could be set and never cleared:
  empty is how the module is told to go back to the address.
- **An account read back shows it empty when the stored login *is* the
  address.** The module resolves an empty login when it writes the account,
  so one that never had a login reads back with `user == email`; putting
  that in the field would say somebody typed it.

### What this is not

It is not the other half of that report -- several addresses on one account,
so mail to an alias reads as yours and Compose can choose which to send
from. That touches the module, the account file, the identity logic and the
dialog, and is its own release; #157 stays open for it.

### The checks

Four in `tests/mailapp.py`, driving the real dialog through a pty: the row
exists and says an empty login follows the address; a login typed in is what
lands in the account file, rather than the address; re-opening that account
shows it back; and an account made without one still reads as following the
address rather than showing the address as though it had been typed.

They were watched failing against 0.99.144 first -- **seven** checks fail
there, not four, because inserting a field shifts the tab order the older
checks walk, which is the thing to know before adding a row to a dialog a
suite types through.

The module side needed nothing and got nothing: `-u pat` against an account
whose address is `pat@example.com` is already what `tests/mail.py` and
`tests/mailapp.py` log in with everywhere.

## 0.99.144

**`read -n N` took N characters regardless of the delimiter, and `read -N`
did not exist at all** (Gitea #107).

```
$ printf 'ab\ncd\nef\n' > r
$ bash -c 'read -n 10 x < r; echo "[$x]"'
[ab]
$ hibr -c 'read -n 10 x < r; echo "[$x]"'
[ab
cd
ef]
```

bash's `-n` stops at N characters **or the delimiter, whichever comes
first**, which is what makes `read -n 10` on a line of two characters give
two and leave the rest of the file for the next command. hibr read ten and
kept the newlines, so a sequence of `read -n` calls got everything on the
first and nothing after it.

The ticket said the two spellings were "the same command with different
spellings". They were not quite: **`-N` was refused as an unknown option**,
so the form that is *meant* to ignore the delimiter was missing. Both are
here now, and they differ in three ways rather than one:

| | `-n k` | `-N k` |
|---|---|---|
| stops at the delimiter | yes | no |
| keeps a delimiter in the value | n/a | yes |
| splits into fields | yes | **no** |

That third row is bash's and is easy to miss: `read -N 5 a b <<< "x y z"`
puts `x y z` in `a` and leaves `b` empty, blanks and all, and an array gets
one element. `-n` splits as `read` always has.

### What else the sweep found

Running the whole family against bash rather than only the reported form --
this tree's own rule, and thirty seconds -- turned up two more:

- **A count of `0` is not the same as no count.** `read -n 0 x` reads
  nothing, takes **not a byte** from the descriptor and succeeds, so the
  next command sees all of it; hibr read a whole line, because `want = 0`
  was how "no count" was spelled inside.
- **A count that is not a number was silently 0.** `read -n k x` is bash's
  `read: k: invalid number` and status 1; `atoi` made it 0, which after the
  fix above would have meant "read nothing" and succeeded.

### Where it is

`bi_rddelim` takes an `exact` flag beside its count, and both of its
branches -- the block read for a seekable descriptor and the byte-at-a-time
one for a pipe -- ask it rather than the count. The block branch's `lseek`
back to just past the delimiter was already there for the no-count case and
is what makes two `read -n` calls on one file work; the ticket's warning
about it was the right thing to check and needed no change.

### The checks

`tests/206-read-count.t`, **compared against bash**: nineteen lines, of which
eleven failed against 0.99.143. Both counts on a file and through a pipe,
twice on one descriptor, a pipe that ends before the count, `-d` with a
count, `0` taking nothing, more than the file holds, a count that is not a
number, and the five splitting shapes of `-N` -- two names, leading blanks,
a different `IFS`, an array, and no name at all.

## 0.99.143

**`desktop ctl` bounded the answer and not the connect, so a desktop that had
stopped accepting hung its client for as long as anybody waited** (Gitea
#180).

Found by this machine rather than by reading: 0.99.142's gate failed with a
120-second timeout in a check about **removing an app**, which reads as the
application manager being broken. It was not. `apps remove` asks a running
desktop whether any of its windows belong to the app, and that question goes
through `desktop ctl windows`, whose client has had `read -r -t 5` on the
answer since it was written and a plain blocking `connect` above it.

The condition is narrower than "a bad socket", and the narrowness is the
whole bug. A **stale** socket file is harmless: `connect` fails at once with
ECONNREFUSED and the client says there is no desktop. A socket that is
**listening with a full accept queue and nobody accepting** is the one that
hangs -- measured with `strace` on the live case, `connect` blocked until a
signal killed it -- and `ss -xl` named it exactly:

```
u_str LISTEN 0      16   /tmp/hibr-ctl-2000/desktop.ctl
u_str LISTEN 17     16   /tmp/hibr-ctl-2000/desktop.ctl
```

Seventeen queued against a backlog of sixteen, on a session that had stopped
drawing. Every `apps remove`, every suite run and every probe added one.

### HIBR_CONNTIMEO, and why it is opt-in

`net_conn` gives up after `HIBR_CONNTIMEO` milliseconds when it is set, and
is a plain blocking `connect` when it is not -- so **no script that works
today changes behaviour**, which matters because waiting on a connect is
sometimes exactly what a script means to do. It reaches every connect this
shell makes: `/dev/tcp`, `/dev/udp`, `/dev/unix`, `/dev/tls`, the `connect`
builtin and the TLS relay's own socket. `listen`'s in-use probe keeps its own
blocking `connect` on purpose: it wants the real answer.

Three things in it are not decoration, and each is a trap this tree already
records in another place:

- **`SO_ERROR` is asked after `poll` says writable.** A *refused* connect
  becomes writable too, so taking writable for success hands back a dead
  descriptor whose first write fails from nowhere. The test asserts a refused
  TCP connect still says `Connection refused` rather than timing out.
- **The deadline is wall-clock, not a count of rounds.** Every blocking call
  in this shell can be interrupted -- the console installs SIGWINCH -- and
  restarting a 200 ms wait per signal would make the bound meaningless.
- **The descriptor is put back to blocking** on every exit from the function,
  including each failure, or the caller gets a non-blocking socket it never
  asked for.

`desktop ctl` sets it to 2000 ms for itself, and now says which of the two
things went wrong: a socket file that is not there is "no desktop is
listening", and one nothing accepted on is "the desktop there has stopped
answering, or the socket is left over from one that has gone". It cannot tell
those two apart more precisely without the connect's own errno, and the
connection has to land in the client's own shell, so it cannot be captured
from a subshell.

Against the wedged session on this machine: **3 ms** and that message, where
before it waited for as long as it was given.

### What this does not do

`apps remove` no longer **hangs** on a wedged desktop. It does not learn
whether windows are open -- `ap_windows` treats a desktop that cannot be
reached as no windows, which it already did for a desktop that is not running
at all, and which is the right answer for a removal that would otherwise be
impossible on a machine with one wedged session.

### The checks

`tests/205-conntimeo.t`, recorded, and the stand-in is the whole difficulty:
**three earlier versions of it passed against the bug** because the listener
had exited between the fill and the connect, so the connect was refused in a
millisecond and the check proved nothing. The listener now fills its own
accept queue from inside itself -- `settimeout` on its own connects, so it
stops at exactly full -- prints `ready`, and stays alive in a loop that a
signal cannot end. Watched failing first: with the knob set and no fix, the
connect was killed by `timeout` at 5001 ms.

The control case is in the file too: with nothing set, the same connect is
still the blocking one it was, killed by `timeout` with status 124. A check
that only proved the timeout works would not have proved the default is
unchanged.

## 0.99.142

**`set -n` took effect from the next line rather than the next command, so
everything already on the line ran** (Gitea #154).

```
$ bash -c 'set -n; echo X'      # prints nothing
$ dash -c 'set -n; echo X'      # prints nothing
$ hibr -c 'set -n; echo X'
X
```

bash and dash agreed with each other, so hibr was alone. A script runs as it
is read, one complete command line at a time (ADR 0026), and `noexec` was
checked at exactly that granularity -- per input unit, not per command inside
one -- so `set -n; echo X` parsed the whole list, ran `set`, and the `echo`
was already in the list being executed.

### Nothing was added to the hot path, which is why this was fixable

The obvious fix -- ask the flag where each command is about to run -- is in
`ex()`'s prologue or `ex_cmd`, and this file already records a check there
costing **42 instructions per loop iteration**. Loop throughput is the
shell's third priority, so paying that for a flag nobody sets mid-line would
have been the wrong trade, and the ticket said so rather than guessing.

`noexec` turning on **borrows `s->stop`**, which `ex()`'s prologue already
loads along with `quit`, `ret`, `brk` and `cont`. A mode change that means
"run nothing more" is what that flag already does for errexit, so the rest
of the list is abandoned by machinery that was there. Measured, 20,000
iterations of a `while` loop, callgrind:

| | instructions |
|---|---|
| 0.99.141 | 265,715,368 |
| this | 266,395,171 |

**+0.26%, and every hibr function's count is identical** -- the whole
difference is `__strcmp_avx2` taking a different path for the same calls,
which is the address-sensitivity trap this tree already records. Nothing in
`ex()` changed.

### The clear belongs to the outermost text only

The read loop puts `stop` back before the next line, or the parse would end
there and a syntax error further down would go unreported -- which is the
whole point of `noexec` and the reason this could not be done by stopping
the shell. But **only the outermost text does that**: a nested one is an
`eval`, a `source` or a trap body, and

```
$ bash -c 'eval "set -n; echo A"; echo B'   # prints nothing
```

has to stop the caller's list too. `sh_runx` counts its own depth and clears
at depth 1. A subshell is a shell of its own and keeps `noexec` to itself, as
in bash -- `( set -n; echo A ); echo B` prints `B`.

An **interactive** shell never arms it, because bash's own documentation says
interactive shells ignore `noexec`.

### The checks

`tests/204-set-n.t` is **compared against bash**, which dash agrees with, so
both reference shells say the same thing and the test is the strongest kind
this tree has. Fourteen lines, each a shape the fix had to get right: the
same line, a `set +n` after it (which never runs, so it is a one-way door for
the rest of the input rather than a window), what came before, from inside a
function, a compound after it, a subshell, a definition after it, its own
line as before, through an `eval`, an `eval` after it, through a `source`, a
later syntax error still being reported, the piped-stdin read loop, and
`hibr -n -c`, which has always worked. Seven of them failed against 0.99.141.

### And a suite that was waiting on this machine's own desktop

`tests/appmgr.py` failed this release's gate with a 120-second timeout in a
check about **removing an app**, which reads as the application manager being
broken and was not: `apps remove` asks a running desktop whether any of its
windows belong to the app (`ap_windows` -> `desktop ctl windows`), the suite
inherited `HIBR_HOLD` from the shell it was run from, and that named a held
session whose control socket was still listening but no longer accepting. The
installed 0.99.141 blocks on the same connect identically, so nothing here
caused it.

The suite gets a `TMPDIR` of its own now and `HIBR_HOLD` stripped -- the
isolation `tests/screen.py` already does, for the same reason: **a suite must
not depend on, or wait for, whatever desktop the machine happens to be
running.** The control socket is found at `$TMPDIR/hibr-ctl-$UID/<session>.ctl`
and the session defaults to `desktop`, so stripping `HIBR_HOLD` alone would
have reached a *differently* wrong desktop rather than none.

The other half is a product bug and its own ticket (Gitea #180): `desktop ctl`
bounds the answer with `read -t 5` and leaves the **connect** unbounded, so
`apps remove` on a machine with a wedged desktop hangs for as long as anybody
waits, saying nothing.

## 0.99.141

**`json emit`, `keys`, `len` and `type` printed what they answered and filled
nothing, where `json get` has always done both** (Gitea #167).

```
$ hibr -c 'declare -gA M; M["a"]["b"]=1; j := json emit M; echo "bound=[$j]"'
{"a":{"b":"1"}}
bound=[]
```

The `{...}` is `json emit` printing; `$j` is empty, and the status is 0. So a
script that asked for the document in a variable -- to write a file, to send
down a socket, to hand on -- got nothing, and the document went to standard
output instead. In a full-screen program that is the middle of the screen,
underneath the console's own cell drawing.

`json get` had the shape right from the start: honour `s->bind`, so a bound
slot means say nothing, and set `RET` either way. Its four siblings each
`printf`ed unconditionally. All four match it now.

Nothing that worked stops working: `s->bind` is set only by `:=`, so a plain
`json emit M` on a terminal or in a pipe prints exactly as before. Two
details worth stating because they are what a caller will assume:

- **The bound value is the form that was asked for.** `p := json emit M -p`
  binds the indented text, since a script binding it is usually about to
  write a file.
- **`json keys`'s optional out-variable still works, and now sets both.**
  `x := json keys M .a named` fills `named` and the slot, which is what
  `get` does with its own.

`examples/apps.hibr` wrote its state file with `j=$(json emit AP_ST)` and a
comment naming this ticket; it is `j := json emit AP_ST` now, one fork
cheaper per install or removal, and the comment is gone.

### The checks

`tests/203-json-bind.t`, recorded, written and watched failing against
0.99.140 before anything was changed -- all four verbs, each asked
separately, because one binding correctly says nothing about the other three.
Each check reads the slot **and** what was printed, with standard output
redirected to a file: the bug was both halves at once, and a check that only
read the variable would pass a version that still printed. The unbound forms
are in it too, so the thing that must not change is recorded beside the thing
that did.

### And one check this release's gate caught, which was not this release

`tests/760-term.t` failed the sanitizer gate on `0|l 25` against `0|l 18`,
with **0 sanitizer reports** and nothing in this release able to reach it --
the only code change was `src/json.c`, and that file does not mention `json`
once (Gitea #179).

Its scrollback section prints 30 lines into a 3-row terminal with a 4-line
store and pumped a **fixed count** of polls. `term poll` returns as soon as
the pty has something, so a fixed count is a bet on *how few reads the output
arrives in*: idle, thirty `echo`s coalesce into a handful and all thirty are
consumed, leaving lines 25-28 in the store; under a gate running twenty-one
sanitizer suites each `echo` tends to land in its own read, so 22 polls
consumed about 22 lines and the store held 18-21. One line per poll is
exactly the arithmetic that gives 18.

`pump()` waits for `term gen` -- how many times the terminal has been fed --
to stop moving, two quiet windows rather than one, bounded. `show()` and the
three sites that read settled state from a bare count use it. The alternate
screen section keeps its counts **on purpose**, with a comment saying why: it
sleeps a second in the middle, so the two readings are of before and after
the `ESC [3J`, and waiting for quiet would run them together.

Honest about what is proven: the recording is unchanged, which says the
conversion reaches the same settled state the counts reached. The original
failure **did not reproduce on demand** -- 0 of 10 with the sanitizer build
alone, 0 of 6 under one spinner per core, and 0 of 8 concurrent copies under
either build -- so what this removes is the assumption that explains the
number, not a fault anybody can summon.

## 0.99.140

**`source` reads a scheme that says it is local, so shell code can come out
of a mounted archive** (ADR 0043).

**What this release is not:** it is not "an app is a mounted archive", and it
changes nothing in `apps`, the application manager. An installed app is still
a folder of plain files, for the reason [ADR
0041](docs/adr/0041-an-app-is-a-folder-of-files-a-bundle-is-how-it-travels.md)
gives and which has nothing to do with `source`: a person's own files win,
because the config folder is scanned before the data one and `dt_apps` skips
a name it has already seen, and a mounted bundle has no folder for that rule
to act on. This is one measurement out of that ADR being turned around.

The archive module makes a tarball a folder -- `/dev/archive/NAME/path` is a
filename anywhere a filename goes -- and that worked for everything except
the one thing a folder of shell code is for:

```
read -r l < /dev/archive/b/pic.txt      a picture
. /dev/archive/b/app.hibr               source: No such file or directory
```

A scheme is honoured by `rd_do`, the one place a redirection opens anything,
and `source` opens its own file with `fopen`, so it never reached the scheme
layer. The alternative -- `eval "$(archive cat …)"` -- runs the code and
loses `BASH_SOURCE`, which is how a desktop app finds its own header.

So `b_src` goes through the same `net_open` every other reader uses, and
`BASH_SOURCE` is then the scheme path, which names the archive the code came
from rather than a temporary file.

### A module says whether its scheme is local, and the default is no

The first version of this gated on `sc_find` -- any scheme a module had
registered -- reasoning that the danger was `/dev/tcp` and its siblings. One
command refuted that:

```
$ hibr -c 'need http; . /dev/http/127.0.0.1/28883/f.hibr'
RAN from /dev/http/127.0.0.1/28883/f.hibr
```

The **http module registers a scheme**, so `source` had become a one-word way
to fetch and run code off the network -- exactly what excluding `/dev/tcp`
was meant to prevent, through the door left open beside it. And the risk was
misattributed: `/dev/tcp/host/80` is a bare connection and fetches nothing
until something writes a request to it, where `/dev/http/…` fetches by being
opened.

The boundary is therefore **what a scheme does when it is opened**, which
only the module knows. `hibr_schemef(s, nm, fn, HIBR_SCH_LOCAL)` is how it
says so; `hibr_scheme` keeps its signature and means the flag is off, so a
scheme written before this release -- or by somebody who has not thought
about it -- is refused. The archive module says it. The http module does not,
and that is the point:

```
$ hibr -c 'need http; . /dev/http/127.0.0.1/9/x.hibr'
hibr: source: /dev/http/127.0.0.1/9/x.hibr: the http scheme may connect, and source runs what it reads
```

It is said rather than fallen through to `fopen`, which would report a thing
that plainly exists as missing.

`HIBR_ABI` does not move: `struct scheme` is private to `src/net.c` and
`hibr_schemef` is an addition, so an ABI 16 module that never calls it still
loads. The one cross-version effect is the other way about -- the new
`archive.so` needs a shell that exports `hibr_schemef` -- and the package,
the tap and the AUR build ship the shell with its modules.

A dry run refuses every scheme here, because a scheme's own open function is
arbitrary C and a plan that quietly read an archive member and ran it is the
hole ADR 0027 is written against. That check is in `b_src` rather than in
`pl_redir`, which calls every scheme a *connection* -- true of a socket and
wrong about a tarball. It cannot be reached today, since `mod` and `need`
both refuse under a plan so nothing ever registers a scheme there; it costs
one test of a flag and is kept for the day that changes.

### The checks

`tests/202-source-scheme.t`, recorded, and verified failing against 0.99.139
before it passed: a resource read through the scheme, which has always
worked; the same archive's code sourced, with `BASH_SOURCE` reporting the
scheme path; `/dev/http` refused by name, which fails against the wider gate
this release started with; `/dev/tcp` refused; and a dry run opening nothing
and running nothing inside.

## 0.99.139

**Task Manager showed a uid where a name belongs, for anyone whose account
is not in `/etc/passwd`** (Gitea #138).

The Linux scan resolved a uid by reading `/etc/passwd` directly, which was a
deliberate choice and is still the right one for most of the answer: it costs
no fork, matching how the rest of that scan already avoids one. It is simply
not the whole answer. A user who lives in LDAP — or any other NSS source —
has no line in that file at all, so every process they own was labelled with
a bare number.

That is not a corner case on the machine it was reported from: the owner's
own account is one, so **every process they ran** showed as a uid, and
0.99.133 had just made the Owner column size itself neatly around it.

`/etc/passwd` is still asked first, because it is free and answers for most
machines. A uid it does not have goes to `getent` **once** and is remembered
for the life of the desktop — a uid's name does not change, so it is a cache
that never needs invalidating. A uid nothing answers for is cached as
itself, so an unresolvable one costs one fork rather than one a scan.

macOS was never affected: that scan runs `ps -axo user=`, which resolves
through NSS already.

### The checks

Three, against a stub `getent` placed earlier on `PATH` rather than against
this machine's own directory, so they say the same thing everywhere: a uid
`/etc/passwd` does not have resolves to its name, a uid nothing answers for
is itself rather than empty, and **the same uid is asked for exactly once**
however many times it is looked up — because "one fork per uid, ever" is the
design constraint, not merely the happy path, and a check that only tested
the name would pass against a version that forked per process per scan.

The count is per uid rather than in total, since something else on the
machine may ask `getent` for its own reasons; that was not hypothetical, it
showed up while the checks were being verified against the old code.

Verified failing first, as the rule here demands: the same scenario run
against the previous `tasks_ownername` answers `4242` where the check wants
`ldapuser`.

## 0.99.138

**The application manager had no documentation at all** (Gitea #178), asked
for directly: *"point me to the doc for the app management bit?"* — and
there was none to point at.

What existed was `docs/adr/0041`, a decision record rather than a guide, and
a 45-line comment at the top of `examples/apps.hibr` that was the only place
the subcommands were written down. `apps` had no `help` either: a wrong verb
got two lines on standard error and exit 2, with no way to ask.

This file already carries the lesson, from Sheet: **a thing built and then
not handed over is worse than one not built.** The manager shipped in
0.99.130 with a decision record, a suite of 29 checks and nothing a person
could read.

`docs/apps.md` is the page — what an index is, the commands, where a file
lands and why your own copy of a name always wins, the three separate things
it trusts, what it refuses and why, and how to publish an app. `apps help`
is the same list where somebody will actually look for it.

**It says plainly that the default index is not published yet.** It answers
404, so `apps list` finds nothing on a fresh install (#159's second half).
A page describing a catalogue that does not answer, without saying so, would
be worse than no page.

### The trust section is three rows, not one

Because they protect different things and claiming more would be a lie:
**TLS** establishes the identity of the *server* the index came from;
**sha256** the integrity of the *payload*; and **nothing** establishes the
identity of the *author* — there are no signatures, and `apps info` says so
in as many words rather than implying otherwise.

### And the hole underneath it

`tests/531-doc-examples.t` opens by saying every page is held to its
examples, and then named its pages **one by one**. `docs/sheet.md` arrived
in 0.99.120 and was never added, so it had never been checked once. That is
"a glob list is where coverage goes to hide", in the very list that exists
to stop documentation rotting.

It globs `docs/*.md` now, so `sheet.md` is checked for the first time and a
page added later cannot slip past. Verified rather than assumed: a bare
fence put into the new page failed at `docs/apps.md:155`, and the restore
was checked afterwards.

### Left open deliberately

`vw` and `desktop` are the other two things installed as commands and still
have no page; three written at once would be three written badly. The rule
that *would* have caught this — every installed command has a page — should
land with those pages rather than before them, since a rule ships only once
the tree is clean under it.

## 0.99.137

**A session can be older than the desktop running inside it, and the bar
said a restart would fix that** (Gitea #174).

`hold` renders every client from the emulator in its **server** process, and
that server keeps the modules it was started with for the life of the
session. So a fix in `mods/hold`, `mods/term` or `mods/pty` does not reach a
running session at all — and **Restart Desktop re-execs the desktop *inside*
that server**, so it cannot change it either.

The existing mark (`DT_UPNEW`, 0.99.85) is right about the desktop's own
image and wrong by implication about everything else: it says "a newer hibr
is on disk", a restart clears it, and the old emulator is still there. That
is worse than saying nothing, and it cost a full afternoon — a wallpaper
diagnosed six different ways against a session being painted by code from
before the fix, on a server that had been running for hours:

```text
pid     role             exe                        modules
desktop          /usr/bin/hibr              current
hold server      /usr/bin/hibr (deleted)    old
```

`DT_UPSESSION` is the second state. It is asked independently of the first,
because the two really are independent: a restart makes the desktop current
and leaves the session exactly as old as it was, which is the case that
misled. The bar carries `GL[warn]` for it rather than `GL[reload]`, because
it is asking for a different thing — a new session, which a restart is not —
and the notification says so in those words.

`dt_holdsrv` finds the server by walking up the parents until one names
itself as one, which is possible only because 0.99.131 gave those processes
real names (`hold:chicken`). Nothing else knows it: `HIBR_HOLD` carries the
session's *name*, and `hold list` answers a **client's** pid rather than the
server's. Two traps this file already records are in that walk —
`/proc/<pid>/stat`'s command is in brackets and may itself contain brackets,
so the fields are read from the **last** `)`, and the state letter before
the ppid is why a loop that skips fields by reading numbers finds nothing.

### The checks

Three, and both directions are **made** rather than waited for, which is
possible because `title -s` lets a process name itself the way a server
does: a parent running the same binary is a current session, one running a
copy at another path is what an apt upgrade leaves behind (the running image
becomes a deleted inode while `$HIBR` is the new one), and a desktop that is
not held has no session to be old. `dt_updcheck` had no coverage at all
before this.

## 0.99.136

**A picture wallpaper composited away every window's background** (Gitea
#175). Reported from a live desktop as *"the wallpaper works :) It's just
everything has no background, only foreground colours?"* — the picture
perfect, and every window, the menu bar, a sticky note and the desktop icons
reduced to floating glyphs over it.

The kitty graphics protocol has **two** layers below the text, and hibr was
using the wrong one. Its own words:

```text
Negative z-index values mean that the images will be drawn under the text.
Negative z-index values below INT32_MIN/2 (-1,073,741,824) will be drawn
under cells with non-default background colors.
```

`kt_encode` placed an under-text picture at **`z=-1`** — under the glyphs,
*over* each cell's own background. So the desktop painted a window's face in
a real colour and the terminal then composited the wallpaper on top of it,
leaving the ink. `CN_ZUNDER` is one below the threshold now, and the
wallpaper sits where `wm/wallpaper.hibr` has always assumed it does: a cell
with a real background is opaque, a cell blanked to the default shows the
picture through.

That assumption was written down and never true. `dt_wall` blanks exactly
the cells the picture is meant to show through and leaves every pane alone,
with the comment *"a cell with a background colour paints over a picture the
terminal is compositing below the glyphs"* — correct for the layer below the
backgrounds, wrong for the one above it. Nobody could have noticed before
0.99.134: until then no picture wallpaper reached a held client at all, and
every desktop is held.

### How it was finally established

Not by reasoning. Six mechanisms were proposed and refuted in turn — this z,
`hold` dropping backgrounds, the terminal lacking truecolour backgrounds,
stale modules in a long-lived session, the screen size, and a resize
invalidating the pane-ownership map. Each was measured and each measurement
said no, including a "reproduction" of the resize case that turned out to be
the probe sampling a screen model that had simply not repainted yet.

What settled it was reading the **live session**: attaching a second display
to it and asking what the owner's own client is actually sent, with the
Control Panel open.

```text
cells inside the Control Panel, as received by an attached client:
   col 39 bg=#120428  fg=#3d2a6b  glyph='-'
   col 40 bg=#120428  fg=#3d2a6b  glyph='-'
```

`#120428` is that desktop's own `DT_FACE`. The background was in the bytes,
all the way to the client — so nothing was losing it and nothing was
failing to send it. The only thing left that could remove it was the
terminal compositing something over it, and the only thing over it was the
wallpaper, at a z that the protocol documents as being above exactly that.

### The checks

Two new in `tests/kitgfx.py`, and one existing one corrected. They assert
against the protocol's **threshold**, not against a literal, because the
number is a boundary rather than a value: an under-text picture carries a z
below `INT32_MIN/2`, and a picture that owns its cells carries no z at all —
it is drawn over the text by rights, and a z would put it under the very
cells it owns. The existing check pinned `z == "-1"` and failed, which is
what a check pinning the wrong constant is for.

## 0.99.135

**Wallpaper picker previews piled up on each other** (Gitea #172), reported
from a live session: *"the previews work (even though they overlap images as
I go through images selection)"*.

Measured before it was explained, stepping the selection over four PNGs that
alternate 64x16 and 16x64 and counting placements against deletes:

```text
                 before              after
after step 1     1 place, 0 del      1 place, 0 del
after step 2     2 places, 0 del     2 places, 1 del
after step 3     2 places, 0 del     3 places, 2 del
settled          2 live              1 live
```

Zero deletes, ever, and it did not settle. The count stopping at 2 is itself
the explanation of what the owner saw: among four pictures there are only
two distinct rectangles, so each new one replaced by id the last of *its own
shape* — leaving one wide and one tall placement live at once.

### Why the hash could not see it, which is the fourth time

`cn_imgcheck` drops a region that owns its cells when `cn_imgunder(im) !=
im->under` — *"have the cells I was placed over been drawn through"*. That
catches every case where the covering thing is **cells**. It cannot catch a
covering **bitmap**, which writes no cells at all. And it cannot catch cells
rewritten to the value they already held: `wp_draw` blanks its whole preview
box to spaces every frame and centres the picture inside it, so a region
hashed over blank cells finds them blank again next frame and looks
untouched for ever.

So the first hypothesis — that nothing rewrites those cells — was wrong, and
the ticket records the correction: the picker *does* rewrite them. **A hash
of cell contents cannot distinguish "unchanged" from "changed to the same
value."** That is the same blindness as #112 (an under-text picture dropped
on a frame that painted nothing), #118 (a flush with nothing drawn answering
a question about a frame that never happened) and #165 (the emulator
discarding the `z` that said text belongs on top). What makes the rule below
correct is not the hash at all — it is that the console's model holds **one
bitmap per cell**, so two overlapping pictures is a state it cannot
represent.

### The rule

`cn_image` drops any region its new one overlaps, **between two that own
their cells only**, using the *placed* rectangle rather than the requested
one, since a request can be clipped to the screen and what overlaps is what
is on it. `cn_imgdrop` queues the `a=d` as it does for every other drop, so
the delete goes out before the diff and the text underneath is painted in
the same frame.

Three alternatives were measured and rejected, each on its own evidence
rather than on taste:

- **Let img centre the picture in a box**, so the requested rectangle is
  stable and the existing same-rectangle replacement handles it. `img draw`
  cannot: *"img draw's own resample always stretches to exactly the rows/cols
  it is given, with no notion of the source's own shape"* — which is why
  `img size` exists and why the picker computes its own fit.
- **Pass the box and the placement separately.** `dp->image` carries one
  rectangle, so this is a `dp_api` version bump touching every caller —
  which ADR 0037's own history already refused for a larger win.
- **A verb for a caller to retire its own region.** That is a flag each
  caller sets, and this file's standing rule is that the list of things
  nobody can be relied on to maintain is exactly the list not to make.

### What the checks pin, and the regression that was measured first

Three in `tests/kitgfx.py`, which counts placements and deletes unheld.
Before the rule: the overlapping pair **failed** and the other two **passed**
— which is the shape that matters, because those two are what must not
break.

- **Two pictures that do not overlap are both live**, which is Mail: it
  draws an inline image per row of a laid-out message into one pane. Whether
  two of them can share a cell was *measured*, not assumed — `html lines`
  on a document with two images puts a 5-row image at row 2, leaves rows 3
  to 7 as its box and starts the next at row 10, so they never touch. Had
  they touched, this rule would have shown one picture where a message has
  two.
- **A picture over the wallpaper leaves the wallpaper alone.** The guard on
  0.99.134: an under-text picture is below these by construction, so the
  first window to show a picture would otherwise have undone it two hours
  after it shipped. The wallpaper is re-placed on the second frame in that
  check, because that is what keeps an under-text region at all — without it
  the check would have passed for the wrong reason.
- And a region's own delete is `d=I`; the `d=A` every one of these ends with
  is `cn_close` taking them all. Counting `a=d` made one check pass for the
  wrong reason and two fail for a wrong one before that was noticed.

## 0.99.134

**A picture wallpaper never reached a held desktop, so every one of them was
a black rectangle** (Gitea #165). Reported from a live session — *"I have a
png wallpaper set, and I'm all kitty-d up, and I get a black screen as my
background. This is both on my linux and mac boxes"* — and `dt_autohold`
holds every desktop, so this was every desktop with a picture, on every
terminal, since the wallpaper first became pixels.

The report carried the measurement that found it: **the wallpaper picker's
previews render.** A preview is `img draw … -p "w$id"`, a picture that
*owns its cells*; the wallpaper is `-u`, drawn *under* the text. Same
terminal, same kitty, same held session — so the difference was the
under-text path alone, and nothing about the encoding, the protocol or the
platform.

The chain, each link read rather than guessed:

1. `mods/console/kitty.c` emits the wallpaper with the protocol's **`z=-1`**
   — its own comment, *"below the text, for a picture text is drawn over"*.
2. `mods/console/image.c` sends that **once** and keeps the region: finding
   the same picture in the same rectangle it only marks it still wanted,
   *"one drawn under text is simply still wanted, which is what keeps it"*.
   So there is no second transmission to recover from.
3. `mods/term/img.c`'s `tm_imgkeep` stored `r, c, rows, cols, id` and the
   bytes, and **discarded the `z`** — the one thing that says text belongs
   on top of this picture.
4. `tm_imghit` — *"Drop whatever covers this cell, because something has
   just been drawn through it"* — then dropped the region on the first cell
   written inside it. A wallpaper covers the whole screen, so the menu bar
   alone was enough, in the very frame the picture arrived.
5. `hold` re-emits the pictures its emulator **kept**. There were none:
   zero APCs at the client, permanently.

`tm_img` carries an `under` flag now and `tm_imghit` leaves such a region
alone, because for a picture drawn under the text a cell written inside it
is not evidence it has gone. Its lifetime is the one the console already
intends and already arranges: the explicit `a=d` that `cn_imgowe` queues the
first frame the caller stops placing it, and a clear, scroll, resize or
reset, all of which still drop it.

**Why sixel wallpapers were never affected**, which is also why this was
never seen in the one place it could have been: `image.c`'s retire loop
carries, for an under-text region with no id, `if (… cn_imgunder(…) !=
above) sent = 0` — a sixel is *paint*, has no id, and is re-painted whenever
the cells above it change. A kitty placement is an object that stays below
the text, which is right for a real terminal and fatal through an emulator
that throws it away. Sixel keeps exactly today's behaviour: it carries no
`z`, reads 0, and is still dropped when drawn through.

**`z` is read with `tm_imgkeyc`, never `tm_imgkey`**, and that is not a
preference: `tm_imgkey` answers `-1` both for a key that is absent and for
`z=-1`, which are precisely the two cases this has to tell apart. The helper
for a letter-valued key answers the value's first character, and any
negative `z` is below the text.

No ABI move, and no `TM_API_VER` move: `hold` reaches the pictures only
through `tm_api_image`, which copies the fields out by name and never sees
the struct.

### What the checks do

Four in `tests/holdpix.py`, the one suite that drives a held program and
counts what reaches a real client. The failing one **fails against
0.99.133**, and the cell written inside the picture is what makes it do so:
without that `console put`, `tm_imghit` never runs and the picture arrives
even on the broken code. The settled frame is checked separately, because
that is the state a desktop is actually in — `dt_wallkeep` calls `img keep`,
which marks the region wanted and sends nothing, so the client's copy has to
survive another frame of text with no transmission to repair it. And a sixel
under the text is checked to still be paint: a kept one would be re-emitted
*over* the text and the text would stop reading, which is the failure a
later tidy-up would introduce by "finishing" the flag for both protocols.

## 0.99.133

**Task Manager showed every long process name cut to fifteen characters**
(Gitea #171), reported as *"the name field does not expand when we expand
the window, and all the names are cut short, shouldn't that be the only
field that expands?"*

Half of that reads as one bug and is another. **The Name column does
expand** — `namew` is worked out from the window's width on every frame, and
Name already took all the slack, since PID sizes to the widest pid and
Owner, CPU and Mem were fixed at 8, 6 and 7. **What did not expand is the
names.** On Linux the scan reads `/proc/<pid>/stat`, whose `comm` the kernel
keeps in `TASK_COMM_LEN` — 16 bytes, 15 usable — so the data was cut before
Task Manager ever saw it:

```text
pid 50  comm=systemd-journal  cmdline=/lib/systemd/systemd-journald
pid 110 comm=systemd-network  cmdline=/lib/systemd/systemd-networkd
pid 142 comm=blit-update-hel  cmdline=/usr/lib/blit/bin/blit-update-helper
```

14 of 59 processes on this machine sit on that limit. It is the same
constant 0.99.131 was about, read the other way round: that release was
about writing a name that fits fifteen bytes, this one about reading one
that did not. macOS was never affected — `ps -axo comm=` answers the
executable's full path, which that scan already takes a basename of.

`tasks_fullname` asks `argv[0]` for the rest, in the **scan** rather than in
the drawing, so that sorting by Name agrees with what Name shows. One extra
read for the processes at the limit alone, not one per process.

**It answers only when `argv[0]` extends what comm gave**, and that is not
caution for its own sake: a process may rename its own argv — this shell
does, for every part of the desktop — so `argv[0]` can name something else
entirely. A terminal window's comm is `term:Terminal:1`, cut from
`term:Terminal:104`, against an argv of `desktop [Terminal 104]`; taking
that would put a name from one string under a heading built from another.
Measured with the guard removed: `comm=term:Terminal:1
full=desktop [Terminal 104]`. The same test leaves a genuinely
fifteen-character name alone, which is the other thing this cannot tell
apart from a cut one.

### Owner sizes to its content, with a ceiling

It was fixed at 8, so `systemd-network` read as `systemd-` at every window
width with nothing to say it had been cut. It now grows to the widest owner
in the list — the rule PID already used — measured over the whole list
rather than the rows on screen, so the column does not change width under
you as you scroll, and `TK_OWNERMAX` caps it.

**16 is not a round number.** It is the widest account on a standard systemd
machine (`systemd-timesync`; next is `systemd-network` at 15, and nothing
else in `/etc/passwd` here reaches 11), so every real owner fits and the
ellipsis is for the genuinely unusual rather than for every service.

And **Name has a floor**, because sizing Owner to its content took seven
columns off a Name column that only had fifteen — making the default window
worse than before, which is how a layout change pays for itself with the
thing it was meant to fix. `TK_NAMEMIN` is 15: comm's own usable length, so
Name is never squeezed below what the kernel would have handed us anyway.
Below that, Owner gives its content width back, down to the 8 it used to be
fixed at and no further.

### The default window was the one place the fix could not be seen

`dt_app tasks "Task Manager" 16 50` gave `_draw` a width of 48, so `namew`
was `48 - 6 - 8 - 19` = **15** — exactly what comm had already cut the names
to. The one place the names are read had no room for them. It opens at 64
now, which leaves Name 21 columns beside a sized Owner and still fits an
80-column terminal.

### A gate failure that was the clock, and the rule it sharpens

Gating this release failed one check of 585 — *"a note drawn over the
screen leaves nothing of itself when it goes"* — with the other forty
suites green and nothing in this release able to reach it: that session
loads no apps, so `tasks.hibr` is never sourced.

The tell was in the pair. Glyphs differed and the pen check beside it
passed, which is the signature this file already records for `orpath` and
`dragsteps`: the two snapshots are a second or three apart, a minute turns
between them, and `13:58` meets `13:59`. Those two Terms pin the clock with
`DT_BARTIME`; this one never did.

Reproduced on demand rather than called a flake, by starting the run so the
boundary falls between the two snapshots: **4 runs out of 4 differed**,
every one in row 0 at columns 61-64, and **3 of 3 were clean** with the
clock pinned.

The neighbouring bare-window check needs no pin, and that is the whole
tell: it compares **pens alone**, and a clock digit is a glyph with no
colour of its own. So the rule is not "oracles pin the clock" but
**a check comparing glyphs across two snapshots taken seconds apart has to
pin anything that changes on its own** — and one comparing pens is immune,
which is why only one of the two ever failed.

## 0.99.132

**A right-click drew its menu and last frame's menus beside it** (Gitea
#169), reported from a live session: *"when I context click, I always get
the context menu and a windows menu together?"*

`dt_menu` starts an `MB` entry by setting five fields — `title`, `n`, `w`,
`bar`, `own` — and **not `par`**. `dt_sub` is the only thing that ever sets
`par`, and `dt_menus` begins again from `MB_N=0` on every frame, so every
index is reused. An index that held a submenu keeps that submenu's `par`
into the next frame, and `dt_drop` — which walks that chain so a submenu
shows its whole path — walks into last frame's chain and draws those menus
too.

The trigger is the table **shrinking**. Focus moves from an app with more
menus to one with fewer, so the context menu `dt_ctxbuild` appends at `MB_N`
lands where a submenu was. Driven through a pty, a right-click on the bare
desktop drew the whole Window chain over the desktop's own menu:

```text
  Move                        the Window menu, from a stale par
  Resize
  Zoom
  Snap                     ▸
  Move to Workspace        ▸
  Hide
  On Every Workspace
 ──────────────────────────
  Close                    w
   Arrange Icons               the menu actually asked for
   Change Wallpaper…
   Next Wallpaper
  ────────────────────────
   Refresh Desktop
```

The chain was `context → Move to Workspace → Window`, which is why Window
is what appeared rather than any other menu.

**This is not only the context form.** The same shrink reaches the bar: app
A with two menus and a submenu, then focus app B with three plain menus, and
B's third menu lands on A's submenu index — so clicking that bar title walks
a chain of its own. One line in `dt_menu` fixes both, where clearing it in
`dt_ctxbuild` would have fixed one.

**`px` and `py` go stale the same way, and are the other half of it.**
`dt_mdown` gives a submenu the column to sit beside its parent in,
`dt_mclose` does not take it away, and `dt_menu_draw` prefers it over
`MB_CX` — so a context menu inheriting one draws where that submenu was
rather than at the pointer. Measured: with a submenu opened first, a
right-click at row 20, column 60 drew its menu at **row 1, column 39**.

These cannot be cleared in `dt_menu`: `dt_mdown` sets them while input is
read and the rebuild runs afterwards, so clearing them there would make
every open submenu snap back to its bar title's column on the next frame.
`dt_ctxbuild` clears them for its own root instead, which is the one entry
that must always draw at the pointer — beside the `bar=0` it already sets
for exactly this reason.

### Why no suite caught either

Every existing context check asks whether the menu's own items are at the
pointer (`sc.find("Arrange Icons") == (15, 42)`), and not one of them could
see a second menu drawn next to it. And no fixture has a submenu in the bar
at all: their apps declare none, and a test session loads no apps, so the
hibr menu has no Applications submenu either — so no index ever carried a
`par` to go stale. Three checks in `tests/desktop.py` now drive two apps,
the first with submenus and the second with none, and assert that the
context menu is alone and at the pointer. Both failed before the fix.

## 0.99.131

**A process has two names now, because the two places a person reads them
hold different numbers of bytes** (Gitea #168). Asked for twice: *"have we
fixed the process names in a way that makes them ultra legible for the hibr
desktop?"*, and then *"I'm still not happy with the process names, it's so
so clunky."*

Right both times, and the cause was one line: `pt_rename` passed **the same
string** to the argv region and to `prctl(PR_SET_NAME)`. The argv region is
as long as the command line it overwrites; the kernel's own name is
`TASK_COMM_LEN` — 16 bytes, 15 usable — and it is what `top`, htop's default
column, `pgrep` and `killall` read. So every name was designed for `ps` and
cut mid-word everywhere else:

```text
before                          after
comm              argv          comm              argv
hibr [hold: bli   hibr [hold: blit-direct-test-2]
                                hold:blit-dire    hibr [hold: blit-direct-test-2]
desktop [deskto   desktop [desktop]
                                desktop           desktop
desktop [Termin   desktop [Terminal 104]
                                term:Terminal:104 desktop [Terminal 104]
hibr [attached:   hibr [attached: desktop]
                                attached:deskt    hibr [attached: desktop]
```

**Role first, no brackets, and the handle is never dropped.** The role is
what a person scanning `top` wants (which of these is the desktop, which is
watching it, which window is which), so it comes first. No brackets because
`ps` wraps a defunct process's own name in brackets of its own — which is
how four zombies under a live desktop read `[hibr [desktop]] <defunct>`. And
the window id goes last and survives truncation, because it is the handle
`desktop ctl windows` lists the window under:

```text
term:Files:3            a window called Files, id 3
term:A-very-l:12        the title cut, the id kept
```

`dt_shortname role [instance] [handle]` is the one place that knows the
rule, so no caller counts bytes. `title [-s SHORT] NAME...` and
`hibr_title2(short, long)` for a module; `hibr_title` keeps its
one-argument shape, so nothing built against it changes. `HIBR_PROCNAME`
carries the short name to a child beside `HIBR_PROCTITLE`, both copied out
and deleted together for the reason the old comment already gave — a rename
overwrites the region those strings live in.

**And `desktop [desktop]` is gone**, which was the one the owner saw every
day: `wm/session.hibr` wrote `desktop [$sess]` and the session is called
`desktop` by default, so a word appeared twice for no reason. The instance
is left off when it *is* the role.

### What was measured rather than fixed

Three processes under the live desktop had no name at all, two of them
terminal windows' shells — which is exactly what Gitea #111 existed to fix.
Before writing anything: **that desktop is running a deleted binary.**
`ls -l /proc/<pid>/exe` says `/usr/bin/hibr (deleted)` and it started three
days ago. A probe that waits for the pty children rather than sleeping shows
current code naming them correctly, so the bare `hibr` was a stale-binary
artefact and not a bug. Nothing was changed for it. This project's own rule
is to check which binary is live before diagnosing a live session, and it
would have cost a wrong fix here.

The six zombies in the same output are the reaping family (#123, #143) on
that same old binary, and are not this.

`tests/desktop.py` has four checks reading `/proc/<pid>/comm` and
`/cmdline` for the desktop and for two terminal windows, one of them with a
title deliberately too long, asserting that every short name fits the 15
bytes and that the id survives.

## 0.99.130

**`apps` — the desktop's application manager, as a command** (Gitea #159,
the first half). ADR 0041 is its specification, and the whole of that record
rests on one sentence: *an app is a folder of plain files, and a bundle is
only how it travels*. So installing one is placing a file where a scan
already looks.

```text
apps list [TEXT]   installed   info NAME   install NAME...
apps remove [-f] NAME...   update [NAME...]   index [URL]   refresh
```

**The five scan lists gained a data folder**, after the config one, and the
order is the whole point: `~/.config/hibr/apps/calc.hibr` shadows a fetched
`calc`, because the first list to claim a name wins. Patching an installed
app is copying it to your own folder and editing it there, and an update
cannot overwrite your copy.

**Trust is disclosed, not claimed**, in three separate sentences because the
three protect different things: **TLS** gives the identity of the server the
index came from, the index's **`sha256`** the integrity of the bundle, and
**nothing** gives the identity of the author. There are no signatures, and
`apps info` says that in as many words rather than implying otherwise.

**What it refuses, each with a bundle in the suite built to do it:** a
`sha256` that does not match (both hashes named); a member that climbs out of
its own folder with `..`; a bundle carrying a `.so`, refused with that word,
because native code has no sandbox and root loads modules only from the
folder compiled in (ADR 0016); a symbolic link, which can point anywhere; and
an index entry naming a kind the desktop does not scan.

**An app with a window open is not removed.** The command asks the *running*
desktop over its own control socket — which already answers an app name per
window — and names the windows that are open, because someone running `apps
remove` from a terminal inside the desktop is exactly who that protects.
`-f` closes them first.

### Three things measured rather than assumed

- **`dav get` takes a bare HTTPS URL and is binary-safe.** Checked against a
  32,777-byte PNG fetched to a file and compared by `sha256` with the
  original: identical. `dav request` is not usable for a bundle — it
  NUL-terminates its answer, and gzip data has a NUL almost immediately — so
  the fetch had to be `get`, and no C change was needed to find that out.
- **`archive ls` is a folder view, not a flat listing**, and it *implies* a
  folder the tarball never declared from its members' names. Which is also
  how a member called `../escaped.hibr` arrives: as a folder named `..` at
  the top level with the file inside it. So the path a safety check sees has
  to be the one a recursive walk builds, not a line of `ls` output — the
  first version checked the line, which is `kind TAB size TAB mtime TAB
  name`, and let the climb through.
- **An absolute member is neutralised by the archive module, not by this.**
  `/etc/hibr-escaped.hibr` is presented with the leading slash already gone.
  The suite asserts what actually happens and that `/etc` is untouched,
  rather than asserting a refusal that never fires.

### Where a multi-file bundle unpacks, decided by measurement

The app's own file goes to `<kind>/NAME.hibr` and everything else under
`<kind>/NAME/` — not the whole bundle into a folder. `dt_apps` makes a
subfolder a submenu, and running it over a scratch tree with one folder-app
in it gives `dir=[Applications/Demo]`: a one-item submenu of its own, which
is not what anyone wants. This way the app is on the Applications menu where
it belongs, `DT_SRC` is the app's real file so `dt_aboutinfo` reads its own
header comment, and its resources are at `"${DT_SRC%.hibr}/"`.

### Found on the way, and filed rather than worked around quietly

**`json emit` prints and binds nothing** (Gitea #167). `json get` honours
`:=` — it fills the slot, stays quiet when one is bound, and sets `RET` —
and `emit`, `keys`, `len` and `type` do none of that. So `j := json emit M`
wrote the manager's state to *standard output* and left `j` empty, which is
the trap this project already records about `:=` before 0.99.89 wearing a
different hat. `examples/apps.hibr` uses `$(json emit …)` with a comment
naming the ticket; one fork per install is nothing.

### What is the second half

The desktop window, the Applications desktop icon, and the `hibr-apps`
repository itself. The repository is the owner's to create — it is a public
thing with their name on it — and the index URL already points where it will
be, with "the index could not be fetched" as one line naming the address
rather than a stack of whatever the HTTP client thought.

`tests/appmgr.py` is 29 checks against `tests/davserve.py --auth none`
serving a folder: a fifth stand-in server was not needed, and every bundle
the index describes is built in the suite with Python's own `tarfile` and
`hashlib`, so our own writer is never on both sides of a check.

## 0.99.129

**The menu bar is in a pane, and an app can put an item on it** (Gitea #161
and then #160, in that order, which is what the first of them asked for).

**The bar's cells are the bar's now.** A pane is a name and a rectangle the
console tracks damage against, so the wallpaper paints *behind* the bar
rather than over it, and a frame on which nothing the bar shows has changed
does not draw it at all. `dt_bar` composes a key out of everything it reads
and compares it with the last one — the shape `dt_alone` already uses for
the windows' geometry and `dt_clocktext` for the clock, one level up.

Measured, with medians because the run-to-run spread is 300 us: a settled
two-window frame goes **1027 us to 645**, so the bar's own composition is
about **380 us** — more than half the 0.7 ms a 1%-of-a-core desktop has to
spend. The ticket guessed 0.19 ms from an older measurement; it is twice
that.

`dt_barput` is the only thing that knows the bar is paned. Every column
anything stores — `MB[i]["x"]`, `MB_AX`, `DT_CLOCKC`, `DT_BELLC`, `DT_WSC` —
stays **absolute**, because that is what a mouse report carries and what
every hit test already compares against; the pane's origin is subtracted at
the write and nowhere else. So `dt_mhit`, `dt_wshit` and the click handling
are untouched by the move.

**What that exposed, which is the real content of this release.** Seven
places read `console pane list`, and every one of them meant *the windows*:
the frame's own draw loop, a workspace switch, the restart's saved stacking
order, and four separate "the topmost pane becomes the focus" idioms. That
was indistinguishable while every pane *was* a window. With the bar having
one:

- `dt_wsgo` would have **dropped it on the first workspace switch** and
  never put it back, leaving its cells owned by nothing for the wallpaper to
  paint over — the `cn_pdrop` trap, in the one place that could still reach
  it;
- `dt_draw` would have called `dt_win bar`, whose `id` is typed `int`, so a
  declared function refuses it and the body never runs;
- and the restart's own focus fallback **did** set `DT_FOCUS=bar`, which
  `dt_intile` then refused as not an int, once per frame, in the log, with
  the tiling and the menus' dimming wrong behind it. That is what
  `tests/desktop.py`'s "no session printed a shell error" caught.

`dt_wpanes` is the one reader now. A second non-window pane needs no eighth
change.

**Two things sit outside the gate on purpose**, and both are traps this
project already had in other clothes:

- the **shadow**, because it falls on the row *below* the bar, outside its
  pane, so those cells are not the bar's to keep. Any ordinary write clears
  what `console darken -s` marked, so a gated frame stopped re-casting it
  and the bar's shadow vanished during a drag. **The drag oracle caught
  that**, step by step, glyphs and pens — which is the third time this
  shadow family has bitten and the first time a test found it instead of
  the owner;
- the clock's **`dt_want`**, now `dt_barwant`, called every frame. Gating a
  drawing takes with it whatever asks for the next frame from inside it:
  Cursor Blink, the icons' rescan, About's own readings, and now this. The
  fourth time.

And the gate's first version read `[ -z "$DT_FORCEDRAW" ]`. That variable's
default is the **string `0`**, so the test was false for every desktop and
the gate never fired at all — measured as the bar drawing on four of four
identical frames, 11 writes then 22 then 33. Every other reader compares it
against 1.

### An app can put an item on the bar

`dt_baritem name text [cmd...]`, drawn left of the notification icon, and
clicked it runs the words it was given — words, not a string, since a value
with a space in it reached its callback as two arguments the last time this
desktop joined them. An empty text takes the item off, which is one call
rather than two.

The ticket said its own first question had to be answered before anything
was built — *what is a bar item for that a Control Strip module is not?* —
so: **a strip module is a glyph and a click, and the person decides where it
sits**; a **bar item is a state that changes**, laid out by the bar beside
the clock, where the eye already goes for "what is going on". A button is a
strip module. A number or a light is a bar item. The text is a value the app
**sets when it changes**, never a callback the bar asks per frame — which is
what keeps a bar of ten items free on a frame where none of them moved, and
is the `dt_want` trap in a new hat if done the other way.

**The first user is the vault**: a key on the bar for as long as it is
unlocked, and nothing while it is shut. A state rather than an
announcement — a notification that the vault was unlocked would time out in
two seconds and the vault would then be open for half an hour with nothing
saying so, which is the same argument the "a newer hibr is on disk" mark is
on the bar for. It is also the one piece of this desktop's state where *not*
knowing has a cost: an unlocked vault is a vault anything running as you can
read. Clicking it opens the Vault, where locking it again is — not locking
it outright, since a click that silently throws away a session every other
shell is sharing is not a click anybody can take back.

### Three width bugs, one rule

Found while reading the bar, all of them ADR 0021's rule being broken:

- **`dt_layout` measured titles in characters.** The *mirrored* layout
  (`dt_layoutm`) has always used `str width`; the default one counted
  `${#t}`, so a title holding a glyph wider than a cell laid every title
  after it out one column short — and `dt_mhit` reads those very numbers to
  answer a click, so clicks landed on the wrong menu. The wrong one of the
  two was the one almost everyone runs.
- **`dt_bar` measured the same thing a third time** to work out
  `DT_MENUEND`, in characters again. It reads `MB[i]["tw"]`, which already
  holds exactly that number.
- **The application name was `${t:0:11}` into a fourteen-column slot**, so a
  wide glyph drew over the clock. `dt_cut` (`wm/draw.hibr`) measures: the
  longest beginning of the text that fits in n columns, with the cheap check
  first — for text with no wide glyph the two numbers are equal and the
  slice is already the answer.

## 0.99.128

**A still picture's payload is compressed before it is sent, so a reattach
over a link costs a second rather than thirteen** (Gitea #129, the other
half of it). Reported from a live session: *"i just did a desktop -r, it
takes a long time to re-attach ... 10/15 seconds-ish, then the desktop
appears."*

The owner's own wallpaper, measured through the real path at their own
232x71 with an 8x16 cell, as base64 on the wire:

| | payload | at 5 Mbit/s |
|---|---|---|
| full detail, uncompressed — what 0.99.100 sent | 8.43 MB | 13.5 s |
| half detail (0.99.101) | 2.11 MB | 3.4 s |
| **half detail, `o=z` — what ships now** | **0.65 MB** | **1.0 s** |
| half detail, PNG | 0.47 MB | 0.7 s |
| full detail, PNG | 1.24 MB | 2.0 s |

So the reported 10-15 seconds is one second, and a thirteenth of the bytes
it started at. hold re-emits every picture to every client that attaches,
which is why this is what a reattach costs rather than a one-off.

**A deflate encoder, written here** (`mods/deflate.c`), the same reasoning
as the decoder beside it: a few hundred lines against a dependency. Hash-chain
LZ77, greedy, with **dynamic Huffman codes per block** — which is where the
ratio actually is, because what a PNG hands over is filtered residuals
clustered hard around zero and a code built from the data beats a fixed one
by about a fifth on exactly that. A **stored block** whenever a compressed
one would be no smaller, so incompressible input grows by 41 bytes in 100 kB
rather than by a quarter. Lazy matching is the next thing a real encoder does
and is deliberately not here: a tenth of the remaining ratio for a third more
code, all of it the fiddly kind.

Measured against zlib, which is the only honest way to report it:

| | ours | zlib -1 | zlib -3 | zlib -6 |
|---|---|---|---|---|
| CLAUDE.md, 241 kB | 99,802 | 111,009 | 103,152 | 95,920 |
| the wallpaper's pixels, 1.58 MB | 466,820 | 520,482 | 497,154 | 459,220 |

So it lands between zlib's level 3 and its level 6, which is more than was
expected of it. It costs 123 ms to deflate that wallpaper and 244 ms to turn
it into a PNG.

**And a PNG writer** (`mods/png.c`): chunks, CRC-32, and one filter chosen
per scanline from the five the format offers, by the smallest sum of absolute
residuals — which is the heuristic libpng itself uses. Nothing is linked for
either; `mods/inftab.c` is the tables both directions read, in its own file
now so that a module which only compresses does not carry six kilobytes of
decoder to reach half a kilobyte of data.

**`zlib` is the default and the PNG is not, deliberately.** Everything in
this protocol carries `q=2` — answer nothing, because a reply would land in
the stream the key decoder owns — so a terminal that takes `f=24` and not
`f=100` draws a blank rectangle and *nothing anywhere errors*. That is the
0.99.72 shape exactly, when every picture on the owner's own terminal was a
blank rectangle for four releases; and konsole, one of the four terminals the
console sends kitty escapes to, implements only part of the protocol. 0.18 MB
is not worth that failure mode. **`console imgcomp zlib|png|off`**, and
**Control Panel > Pictures > Compress Pictures** (`DT_IMGCOMP`), for anyone
who knows their terminal.

**Only the wallpaper is compressed**, and the first version of this got that
wrong in a way the release gate caught. The gate was "a palette was chosen
from this picture, or it is under the text", reasoning that a still is placed
rarely — and that is false for two of the three callers that set the flag:
**the browser hands its page over on every frame it draws**
(`mods/web/draw.c`) and the Image Viewer re-places on every zoom and pan. So
"a still" there means "a different picture most frames", exactly as a film
does. It surfaced as one `tests/web.py` check failing under the sanitizers,
where everything is four times slower, and the arithmetic says why: a page at
800x544 is 1.3 MB and deflating it is about 100 ms against a 0.7 ms frame
budget.

The wallpaper is the one picture that is both placed rarely — the keep of
0.99.96 makes a repeat placement free — and re-sent to every client that
attaches, which is what the ticket is about. A still photograph in the Image
Viewer now goes out uncompressed, and that is a real thing given up: it would
help a remote session, and it cannot be had until something distinguishes "a
picture that will not change" from "a picture with a palette". `tests/media.py`
checks a film's frame still goes out as `f=24` with no `o=z`, and
`tests/kitgfx.py` that a picture owning its cells does too.

**One thing this does not yet reach, found while gating it and filed as
Gitea #165.** A picture drawn *under* the text — which is what a wallpaper
is — reaches a held client **not at all**: zero APCs and zero images,
measured for a held desktop and for a bare held program alike, and
identically with `console imgcomp off`, so it is pre-existing and nothing to
do with this release. Every desktop is held, so until that is understood the
payload measured above is what a console with a cell size sends rather than
what the owner's own reattach carries. The numbers are real; where they land
is now a ticket.

**How it is tested, which is the part worth reading.** `q=2` guarantees a
terminal never complains, so "it looked fine" is not evidence of anything.
`tests/kitgfx.py` sends one picture three ways and asserts all three describe
**the same pixels**: the uncompressed payload, the `o=z` stream inflated by
Python's zlib, and the PNG decoded by the suite itself — its chunks, its
CRCs and its filters undone by hand rather than by a library, since our own
writer is on the other side and a library would only be checking that libpng
and we agree. The harness keeps picture payloads now (`Screen.imgdata`,
capped) so a check can decode one instead of only measuring its length, which
is the only honest test of a compressed payload: the length of a zlib stream
is not a number a test can predict.

**A pre-existing bug found while adding the row next to it.** Control Panel >
Pictures worked out what the terminal can do into a variable and then
overwrote it two lines later with the Wallpaper Detail value, so the **This
Terminal** row read "Half" or "Full" — what the terminal can actually do had
never once been shown there. It says `kitty pixels, a cell is 8 by 16` now.

## 0.99.127

**One wheel notch is the same distance everywhere, and About has three
headed sections** (Gitea #130). The ticket asked for two things and this is
both of them: *"A scrollable content area ... That is a pattern, not a
widget -- About, Task Manager and anything else that outgrows its window
each have to reimplement it"*, and *"Grouping and headings. It is one
undifferentiated column now."*

**The scrolling behaviour is shared now, not copied.** A list that scrolls
has three questions, and four files had each written out their own answers:
keep the top in range, bring it to the selection when the selection moves,
and turn a key or a wheel direction into a new top. `widgets/scrollbar.hibr`
already held `dt_scrollfit` for the first; it now holds `dt_scrollsel` and
`dt_scrollmove` for the other two, and About, the Control Panel's pane list
and picker, Task Manager and the notification history all go through them.
Each stays a pure function -- the offset itself belongs under the window id
in the app's own map, so two windows of one app cannot fight over it.

**What that changed in use: one wheel notch now moves the same distance in
every window.** It was 2 rows in About and in a Control Panel pane, 2 in
the wallpaper picker, and 3 in Task Manager and the notification history --
not a choice anybody made, just four days' work laid down one file at a
time. `DT_SCROLLWHEEL` is that number, 3, and it has a name so that it can
only be one number. Task Manager's own selection also gained the top clamp
it never had, which came free with sharing the function.

**About reads as three sections.** `This Computer` for the machine --
mods/sysinfo's own block, picture and all; `This Desktop` for what only
this desktop knows; `This Terminal`, which was there already and was the
only heading in the box. Before this, nothing but the order said whether a
line was about the machine or about the desktop drawing it: `Uptime` means
the machine's and `Workspace` means this desktop's.

`This Desktop` is three new facts, all of them free to ask -- no fork, no
`/proc`, nothing that is not already a variable the window manager keeps
current:

```text
This Desktop
Windows: 3 open, 1 hidden
Workspace: 1 of 3
Apps: 26 available
```

The window counts itself, because a count that left itself out would answer
differently than the same question asked from anywhere else; the app count
is what the menus actually list, so the hidden registrations -- About
itself, Mail's compose window, every dialog that is an app -- are left out.

**What did not move, and was asked for directly:** the hostname, the uptime
and who is in stay pinned *above* the box (*"can we move it so hostname,
uptime, users ... then the scroll box sysinfo"*), and the tagline and the
two versions stay pinned *below* it (*"can we move the hibr desktop version
till the end, under the scrollable box"*). Both read better inside a
section and neither went there.

The heading costs the box one row, which is what its scrollbars are for:
the processor is now a page in rather than on the first screen.

**Still open on #130, and said plainly rather than closed over:** a
`dt_scrollbox` widget that *draws* a list -- rows, selection, its own bar
-- does not exist. What is shared is the behaviour, which was the ticket's
actual complaint; the drawing is still each app's own. A picture in the
window still waits on `dp_api` growing a paned put, as the ticket says.

## 0.99.126

**One rule says where everything is listed** (Gitea #158, and the rule the
owner asked for). Reported as *"let's put a clear demarcation without
ambiguity, what's a system app, and plays an integral role in the desktop
itself, is under desktop, what's an app is an app. So About this Computer,
is Desktop, right?"*

The rule, now in `examples/desktop/README.md` as one paragraph:

> **A thing belongs to the desktop if it manages or reports on the desktop,
> the machine or the session — it would make no sense without this desktop.
> Everything else is an application, however small: it opens, shows or
> edits something of yours, and would make sense on any desktop.**

So the hibr menu is the desktop's own things with one door to the rest:

```text
About This Computer…          Applications ▸  Accessories ▸
─────                                         Calendar
Applications            ▸                     Contacts
Clipboard                                     Files
Control Panel                                 Games       ▸
Modules                                       Internet    ▸
Screenshot…                                   Office      ▸
Task Manager                                  Terminal
─────  Screen Saver / Lock                    Vault
```

**And it needed no new mechanism, because the folder an app is listed in
was already data rather than a path.** `da_apps` set
`DT_APPDIR_CUR="Desk Accessories"` — a folder name no folder had — and
`dt_appplan` already turned a folder into a submenu and recursed on
`prefix/child`. So the whole split is a different answer to that one
question: `dt_apps` says `Applications` for a file at the top of its root
and `Applications/Office` for one in a subfolder, and `dt_sysapps` (which
replaces `DA_DIRS`/`da_apps`, scanning `system/`) says **nothing at all**,
which is what puts an app on the hibr menu itself.

Eighteen files moved. `system/`: About This Computer, Control Panel, Task
Manager, Modules, Notifications, Clipboard, Screenshot. `apps/`: Files,
Terminal, Vault, Calendar, Contacts at the top; `Accessories/` for the
calculator, clock, image viewer, puzzle, prayer times and Stickies; Office,
Internet and Games as they were. `desk-accessories/` is gone — and a
person with one of their own is **told**, once, at startup, rather than
finding a file quietly missing from a menu.

**Stickies was the one genuinely ambiguous case**, and the tie-breaker was
*does it have a document?* It saves notes, so it is an application; the
Clipboard keeps history the desktop owns and Notifications are the
desktop's own, so those two are the desktop's. Recorded here so nobody
re-argues it from the other direction — its bare windows do live on the
desktop surface, which is what makes it arguable at all.

**The Control Panel's own groups already had the right three headings** —
Desktop, Apps, Hardware — and were assigned by habit. Five moved: the
`Passwords` pane is **Vault** and in Apps, `PIM` is **Calendar &
Contacts**, Prayer Times joins Apps, and About This Computer and Task
Manager become Desktop, which is the half the owner asked about by name.

**A submenu inside a submenu lost its grandparent, and that had to be
fixed first**, because `Applications ▸ Office ▸ Write` is two deep where
nothing before was. `dt_drop` drew `MB_OPEN` and `MB_PAR` — one level —
and `MB_PAR` is a single global holding the menu `dt_mdown` came *from*, so
at depth two the hibr menu simply stopped being drawn and the chain jumped
left. Each submenu records its own parent now (`MB[i]["par"]`, set by
`dt_sub`, which already knew it) and `dt_drop` walks the chain outermost
first. Measured through a pty before and after; navigation was never
broken, only the drawing.

**And widening a glob found a hole in a check.** `540-examples.t`'s
"no window content draws at absolute screen coordinates" never scanned
`apps/*/` at all — Office, Internet and Games had never been checked — and
once they were, its pattern turned out to read only the word after `put`,
so `console put -r -p "w$id"` read as absolute. The pattern allows flags
before `-p` now, and there was no real absolute drawing hiding in those
three folders.

## 0.99.125

**A module's version is its own** (Gitea #162), and **a note check waits
for the note to be gone** (#136). Reported from reading
`mod list`: *"modules all have their own version. I see modules that carry
the hibr version, this cannot be true, right?"*

Right, and it was nineteen of thirty-six, in two forms:

- **Eight declared `HIBR_VER`** — `auth`, `darwin`, `email`, `html`,
  `http`, `ls`, `prompt`, `sys` — so their version changed on every release
  whether they had changed or not. `mod list` printed `sys 0.99.124`, a
  column saying only which shell you were running, which the shell already
  tells you.
- **Eleven carried the release they were first written in** — `0.21` for
  nine of them, `0.22` for `hold`, `0.24` for `term` — which is the same
  mistake in a slower form. Those numbers have meant nothing since the ABI
  moved sixteen times underneath them.

All nineteen are **`1.0`** now, and the rule is written down in
`mods/README.md`: **1.0 once a module does what its own README says, 0.x
while it does not**, moving when the module's behaviour changes in a way a
caller could notice and never on a release that did not touch it. So the
five that are genuinely unfinished keep their numbers — `dav 0.1`,
`db 0.2`, `media 0.1`, `mermaid 0.1`, `web 0.1` — each with its own list of
what it cannot do yet, and the field now distinguishes them from the
thirty-one that are done.

Nothing compares it, which is what makes it free to mean that: a module
whose **`abi`** does not match the shell's is refused at load, and an
**interface's** own number (`DP_API_VER`, `PY_API_VER`, `TM_API_VER`) is
what `hibr_require` matches exactly. The version is read only to be
printed.

**And the release ritual loses a hand-edit.** `tests/090-module.expected`
records `mod list`'s own line for `sys`, so **every single release** had to
edit that file beside `HIBR_VER` — a recorded test that must change on
every release is recording the wrong thing. It reads `sys 1.0 abi N` now
and stops moving.

**`desktop.py`'s note check waits for the note to be gone** (Gitea #136),
which this release's own gate made unavoidable rather than optional: it
failed on exactly that check, an hour after the ticket had been annotated
as the next member of the family 0.99.124 found. It waited **1.6 seconds
for a 700 ms note** — a little over twice its life, ample on a quiet box
and not ample under a gate — and that is the same shape as the file-dialog
checks, so it gets the same treatment:

```python
ovt.keys([b"x", b"x", 0.5, b"N"])
ovt.until(lambda s: s.find("a note over the screen") is not None)
ovt.until(lambda s: s.find("a note over the screen") is None)
gone = orsettled(ovt)
```

Two waits rather than one, because the condition that matters — the note
being **gone** — is a condition that must not already be true when the wait
starts, and waiting for it to appear first is what guarantees that.
`orsettled` still follows: the check is about the frame *after* it went,
not the moment it went. Verified the way #163 was, under six spinners on a
four-core box: `desktop` **570 passed, 0 failed**.

0.99.124's own new warning fired on this release's gate, which is the first
time it has had a chance to:

```text
asan: 39 suites, most of them driving a pty -- run this alone, not beside all.py
```

## 0.99.124

**A check waits for the thing, not for a pause** (Gitea #163). 0.99.123's
gate failed **twice**, on four different checks in Write's file-dialog
section, and every one of them passed when the suite was run alone —
`apps_reach` 127/0 twice, `935-adopt` 5 of 5 under the sanitizer build. The
cause was not the release.

`tests/asan.py --quick` asks `affected.py` what a changed module reaches,
and a change to console, term, pty or hold pulls in **every suite the
desktop draws through** — which is right, and for 0.99.123 made the quick
set thirty-eight suites, most of them driving a pty, running beside
`all.py`'s own thirty-eight. The gate went from about forty pty sessions to
eighty. What broke under that were the checks that pad with a fixed pause:

```python
OPEN = [b"\x1b[21~", b"\x1b[C", b"o", 0.5]
```

`Term.keys` waits for the idle marker of the bytes it sent, so a key is
never early — but a dialog opening, a folder listed, a preview drawn and a
window retitled all happen *after* that marker, and half a second is not
enough when eighty ptys are competing. The rule this tree already states
twice — *wait for the note, not the effect* — was applied everywhere except
here.

- **`Term.until(cond, tries=40, step=0.1)`** in `tests/screen.py`, beside
  `until_idle`: collect until the screen satisfies a condition, text or a
  function. `tests/apps.py`'s `run()` had that loop written out inside it,
  so this **removes** a copy rather than adding one, and `wrrun` and
  `shrun` gain an `until` of their own.
- **Seven checks converted**, each to the thing it was actually waiting
  for: the `┤ Open ├` border, `readme.txt` in the listing, the retitled
  `┤ Write [fresh.md] ├`, the replace prompt, `page.html` existing, and
  `data.db` in dBASE's dialog. About 3.5 seconds of fixed pauses went with
  them, so the part is faster as well as steadier.
- **`asan.py` says when its quick set has stopped being quick**: past
  `QUICKMANY` (12) suites it prints *"N suites, most of them driving a pty
  -- run this alone, not beside all.py"*. The operator is the only one who
  knows whether `all.py` is running, so the tool says what it knows rather
  than deciding.

**Verified under the load that broke it**, which is the point: six spinners
on a four-core box, `apps_reach` **127 passed, 0 failed**. Under the same
load before the conversion it failed, and the first run of it exposed the
next member of the same family — 0.99.120's own *"Help > Open the Tour"*,
which forks a sandboxed hibr per formula and so arrives well after its
window's title. That one waits for a **worked-out value**, not the title:
a condition already true waits for nothing, which is the trap any sweep of
these pauses has to avoid, and the reason the remaining fixed pauses are
left alone until one of them actually fails.

## 0.99.123

**A flush with nothing drawn since the last one is the same frame, not a
frame that stopped wanting the picture** (Gitea #118), and **the idle clock
restarts when no saver could start**. Two instances of one invariant -- a
cell may be left undrawn only if the thing that owns it is the only thing
that ever writes it -- both named in 0.99.87 as found while reading the diff
for #112 and #114, and neither fixed then.

**1. Every screenshot was taken with the wallpaper deleted from the
terminal.** `wm/shot.hibr` ends with `dt_draw; console flush`, and `dt_draw`
has already flushed. What keeps a picture drawn under the text is the caller
placing it again (ADR 0037), so `cn_imgcheck` running a second time with
nothing in between read "nobody wants it" from a frame that never happened:
the region went, with a kitty delete, and the screenshot caught the screen
without it. The console counts writes now (`cn_wrote`, bumped by `cn_put`,
`cn_fill`, `cn_clear`, `cn_darken1`, `cn_image` and `cn_imgkeep`) and
compares the count with its own last reading, so a flush that follows no
drawing retires nothing at all -- a region that owns its cells cannot have
been drawn through either, since nothing wrote. The rule is worth stating
plainly: **`console flush` is not idempotent for a picture under the text.**

**2. A desktop with no saver installed cleared the screen on every frame.**
`dt_saverstart` reset `DT_SAVERFROM` and not `DT_IDLEFROM`, and when
`sv_begin` fails -- no savers at all -- `DT_SAVING` stays empty, so the next
frame is an ordinary one and `dt_saverwait` finds the idle time up again.
Clear, force a full redraw, repeat, for as long as the desktop runs:
measured at **57 frames and 5.2% of a core in four seconds** against 8 and
0.5% with the line put back. Only that path needs it; a saver that did start
leaves through `dt_saverstop`, which already resets the clock.

Both guards fail against 0.99.122, checked before being trusted.
`tests/kitgfx.py` places a picture under the text, flushes twice and asserts
no `d=I` delete -- not merely no delete, since `console close` always sends
`d=A`. `tests/desktop.py` drives a session whose `dt_saverload` sources the
real saver library and then empties `SV_LIST`, which is the state a machine
with no savers is in, so the real `sv_begin` fails on its own rather than
being stubbed into failing; it asserts the frame count **and** the CPU,
because a frame count alone measured 37 against a threshold of 40 -- a check
that passes the bug on a slow box.

## 0.99.122

**Every option `set` takes, hibr takes on the command line** (Gitea #153),
and `$-` says which are on.

`hibr -x script` -- the first thing anybody reaches for to debug a script --
answered `-x: No such file or directory` and exited 127. So did `-e`, `-u`,
`-o errexit`, `-i`, `-s` and `-f`, and a `#!/usr/bin/hibr -e` shebang:
`main`'s own loop understood ten long options and `-c -d -l -n -v -h`, then
**broke on anything else and treated it as the name of a script to open**.

- **One reader, shared.** `sh_optch` maps a letter to an option *name* and
  goes through `sh_optset`, so the command line and `set` reach the same
  code and cannot drift apart -- which is ADR 0017's argument about `set -o`
  and `shopt`, one level up. `set` gains what it was missing on the way:
  **bundling** (`set -ex` is bash's and was `unknown option` here) and `-n`,
  whose long spelling `set -o noexec` already worked.
- **Bundled at startup too**, because `hibr -lc 'cmd'` is how `su` and
  `login` start a shell; a letter that takes a value (`-c`, `-d`) takes the
  rest of its word or the next one. An unknown letter is `invalid option`
  with **status 2**, which is bash's, rather than 127 and a message about a
  missing file.
- **`-i`** makes a shell interactive whether or not standard input is a
  terminal: it reads `~/.hibrc`, prompts and keeps history, and `hibr -i -c
  '…'` runs the command as a shell somebody is talking to, as bash does.
- **`-s`** reads commands from standard input with whatever follows as the
  positional parameters, so a filename there is `$1` and never a file to
  open.
- **`$-`** is the letters of the options now on, then `i` when interactive,
  then `c` or `s` for how the shell was started. It was the empty string
  before, which is a *wrong* answer to the interactive test
  `/etc/skel/.bashrc` on this machine uses and seven scripts in `/usr/bin`
  name: `case $- in *i*) … ;; *) return ;; esac`.

What `-i` cost: `jc_init` begins `while (tcgetpgrp(s->tty) != getpgrp())
kill(-pgid, SIGTTIN)`, and without a terminal `tcgetpgrp` answers -1 for
ever -- **an infinite loop signalling its own process group**, unreachable
until this release made an interactive shell possible on a pipe. It returns
early with "job control off: standard input is not a terminal" now, which is
the pair of lines bash prints in the same situation.

hibr's `$-` letters are its own options, so there is no `h` or `B` among
them: bash reports those because it has options to turn them off and hibr
has not. That is why the letters are pinned by a **recorded** test
(`tests/201-dash-flags.t`) while everything that agrees with bash --
`-e -ex -o errexit +e -u -x -n -s -i -l --`, an unknown option's status, a
`-c` with nothing to run, and `case $- in *e*)` -- is **compared** against
it in `tests/199-startup-opts.t`, which fails against 0.99.121.

Three things deliberately left: `set -` (bash ends option parsing *and*
clears `-x`/`-v`) still errors; `-f` is still refused, because hibr has no
noglob option to turn on and saying `invalid option` is the honest answer;
and `set -n` inside a single `-c` line does not stop the rest of that line,
since a line is hibr's input unit -- #154, with the cost of fixing it
measured there rather than guessed. The 199 test asserts how many bytes an
unbound variable under `-u` printed rather than the status, because the three
shells disagree with each other there: bash answers 127 for `-c` and 1 for a
script, dash 2, hibr 1.

## 0.99.121

**`${#@}` and `${#*}` are how many parameters there are** (Gitea #152).
They answered the length of the joined parameters:

```text
$ set -- a bb ccc
bash:  ${#@} 3   ${#*} 3   $# 3
hibr:  ${#@} 8   ${#*} 8   $# 3
```

8 is `strlen("a bb ccc")`. `V_LEN` on a part whose name is `@` or `*` fell
through to the ordinary "length of this parameter's value" path, and `$@`'s
value *is* the join -- so the hole was the two special parameters rather
than the operator: `${#a[@]}` has always counted. bash makes all three the
same number, and `${#@}` is the form you reach for inside a function where
you want to be explicit that the count is the function's own.

`tests/198-len-params.t` is compared against bash and fails against
0.99.120. It covers the count at top level and inside a function, with no
parameters, with a parameter that has a blank in it (the old answer was 5
for two parameters), under `IFS=:` and an empty `IFS`, and beside `${#1}`
and `${#a[@]}` so the lengths this did *not* touch are in the same output.

One thing the ticket records as **not** a bug: slicing an associative
array's `[@]` differs from bash, and bash is the odd one -- it iterates its
own hash order, and its offsets disagree with what it does for an indexed
array (`:0:1` and `:1:1` both answer the same element). hibr's maps are
ordered on purpose, so the slice follows from that.

## 0.99.120

**Sheet says what it can do** (Gitea #150). Reported as *"I have no idea
how to use it this way, and there is no toolbar even, so I'm not sure how
to use hibr inside of it? There are no examples either ... I would love to
get some real documenation in place as well."*

The uncomfortable part: **Sheet could already do all of it**. Its formula
language -- `=` and then hibr, every named cell a variable, every range
`A1_B9` an array, the whole thing sandboxed under `--plan` with `#REFUSED`,
`#TIME` and `#CYCLE` -- was written down in thirty-six lines at the top of
`apps/Office/sheet.hibr` and nowhere else. That is not a missing feature.
It is work already paid for and never handed over.

And one correction to the report, which changed the shape of the release:
**Sheet has always had a formula bar.** Row one is the current cell's name
and its *source*, editable in place. What it had never done was look like
one. So:

- **The formula bar says so**: an `ƒx` mark, and on an **empty cell** a dim
  line of what a formula looks like -- `= then hibr: =math "A1 * 1.2"
  =sum "${B1_B9[@]}" =$((A1 * 2))`. The language is taught at the one
  moment it is needed, and it costs no layout change. This is the smallest
  change in the release and the one that answers the question.
- **A toolbar**, which is what was actually missing: the Format menu's own
  actions as buttons -- bold, italic, three alignments, decimals, percent,
  thousands -- and an `ƒx` button that opens the help. Each button runs the
  same call the menu makes, so the two cannot drift apart.
- **Help > How Formulas Work**, a page of it in a window, with the
  formulas written as code and only the prose translated -- a catalogue
  entry holding both would ask a translator to keep a shell expansion
  intact inside a sentence.
- **Help > Open the Tour**: a sheet whose cells *are* the documentation,
  shipped as `examples/tour.csv` (a CSV field beginning `=` arrives as a
  formula, so an example need not be a binary), imported into a **new**
  window so it cannot land on what you were working on. Two of its cells
  are a formula that would write a file and one that loops, so `#REFUSED`
  and `#TIME` can be *seen* -- which is the only way Trust This Sheet means
  anything.
- **`docs/sheet.md`**, with the screenshots taken from the running app.
  The source header now points at it rather than being the only copy.

Adding the toolbar moved every row in the window, and those rows were the
literals 1, 2 and 3 in eight places -- including both of `ss_at`'s, where
a missed one is a click landing on a different cell than the pointer. They
are `SS_TBAR`, `SS_FBAR`, `SS_HROW` and `SS_ROW0` now, and `tests/apps.py`
takes a **sheet** row rather than a screen row for the same reason:
fourteen checks each held one of their own and all fourteen broke at once.

Nine checks added; `GL[fx]` is in all three glyph sets, and the
twenty-seven new strings are in the Arabic catalogue by hand.

## 0.99.119

**An ignored signal is a trap, and `trap` now says so** (Gitea #151).
`trap '' TERM` was kept as a NULL pointer -- exactly like no trap at all --
so the listing could not see it, and `eval "$(trap -p)"`, the one thing
that output exists for, put every ignore back as a *default*: a signal
that starts killing the shell. An ignore is the empty string now, which
`tr_run` already skipped, so nothing else had to learn the difference.

`trap` also took neither **`-p`** nor **`--`**, so the round trip could not
have worked anyway -- every line it prints begins `trap -- '…'`, and
`eval`ing that gave `trap: echo bye: unknown signal`. Both are accepted
now, with `trap -p sig…` listing only those, as in bash.

**A signal the parent left ignored may not be trapped or reset.** POSIX
says a shell cannot undo that decision and bash keeps to it: asked to trap
one it says nothing, lists it as ignored still, and the signal goes on
doing nothing. hibr does the same, and lists it -- so a `trap` listing can
now show signals the script never mentioned, which is the point.

`tr_wasign` answers "did this arrive ignored", asked **lazily and cached**:
sixty-four `sigaction` queries at startup would be 32 us against a 1.16 ms
startup, and a shell that never mentions a signal should pay nothing for
this. The price is that anything in this shell which changes a disposition
has to ask first, or it reads its own work as the parent's -- `jc_init`'s
four job-control ignores and `tr_init`'s own `SIGCHLD` handler, which are
the only two places in the parent; every other `signal()` call in the tree
is after a fork.

This is what failed two release gates for 0.99.117 and passed every run by
hand. `tests/170-trap.t` prints the listing, and bash's had three lines
hibr's did not whenever the parent ignored `SIGTSTP`, `SIGTTIN` or
`SIGTTOU` -- which a shell doing job control does. It read as load and
never was: thirty runs under six spinners failed thirty times, and
`bash -c "trap '' TSTP; hibr -c trap"` shows it in a second. The test was
right and the shell was wrong, twice. It now passes from a shell that
ignores all three.

`tests/197-trap-ignore.t` compares the listing, the reset, `-p`, `--`, the
round trip, the inherited rule in all three of its parts and an empty
`EXIT` trap against bash, and fails against 0.99.118.

## 0.99.118

**A map key named `*` or `@` could not be read back** (Gitea #148).
`${m["*"]}` answered the whole map joined rather than that one entry, and
`${#m["*"]}` answered how many entries there were rather than how long
that one was -- so an entry under either name was unreachable, while the
*assignment* and `unset` worked perfectly, which is what made it look like
anything other than a read.

`xkeys` dropped a trailing subscript as the all-form when its **expanded
text** came out as `@` or `*`. It asks the word instead now, `xallw`: one
unquoted run of text that is exactly `@` or `*`. So `m[*]` and `m[@]` are
every entry as before, `m["*"]` is the entry named `*` -- quoting meaning
what it means everywhere else, which is this project's own rule since ADR
0006 -- and **`m[$i]` with `i=*` is that entry too**, because it was not
written as the all-form. That last one is the half quoting alone would not
have fixed, and it is what bash does.

The rule lived in **one of the two splitters**: `bi_keys`, which `unset`,
`read` and `[[ -v ]]` go through, honoured the quote mask already. A rule
that has to be kept in two places is a rule that is kept in one of them.

Found by using the shell rather than reading it: 0.99.117's
`widgets/putruns.hibr` wanted a pen for "any letter not named", called it
`*`, and the desktop said `console pen: #cbd5e0 #4fd1c5 #f6ad55 ...: not a
colour` -- which reads as the widget building a bad spec. The widget's
default pen is still called `else`, because `else` says what it means.

`tests/196-star-key.t` compares every form of it against bash and fails
against 0.99.117. It prints no multi-key map's `[@]` or `[*]`, on purpose:
hibr's maps are ordered and bash's iterate its own hash order, so a test
that printed one would be comparing map order rather than this. Two
divergences met in the same comparison and *not* changed here -- `${#@}`
and `${#*}` giving the length of the joined parameters rather than how many
there are (Gitea #152), and bash's own inconsistent offsets when slicing an
associative array's `[@]`, where hibr's ordered maps make it consistent and
bash is the odd one.

## 0.99.117

**A Mermaid diagram editor, and a diagram in a document** (Gitea #140).
Asked for as *"we want a mermaid diuagram editor and creator and add it to
the office tools right?"*, and shipped as the three pieces the ticket said
it was made of, with the scope chosen by the owner: flowcharts, sequence
diagrams and pies; the module and the app in one release; and Write with
it.

**`mods/mermaid`** parses the three kinds and draws them into cells.
Flowcharts are the only one with layout to do and it is Sugiyama's, the
three steps dagre takes: a depth-first walk reverses the edges that close a
cycle, each node is ranked one below the lowest thing that points at it,
an edge spanning more than one rank is split into a chain through a bend
point on each rank between, the median heuristic orders each rank four
passes down and up, and then each node is packed and nudged so a parent
sits over the middle of its children. Then **every gutter gets as many
tracks as its edges need**, coloured greedily widest first, because two
edges sharing one track merge into a single line -- a diagram that looks
right and is wrong. `LR` is the same layout read sideways and `BT`/`RL` are
those two with the level measured from the far side, so there is one engine
and not four: everything is in two abstract axes and exactly one function
says which is which.

Everything it does not understand is **named rather than half-drawn**,
because Mermaid errors whole and half a diagram is wronger than none:
`subgraph`, the shapes `[[ ]]` `[( )]` `[/ /]` `[\ \]`, sequence blocks
(`loop`, `alt`, `opt`, `par`) and every other kind of diagram each get a
line number and a sentence.

It answers in the shape **`sysinfo` grew in 0.99.116**: with a result slot
bound, `d["text"][i]` is a row and `d["runs"][i]` the style of every
character in it (`b:3 t:9 b:3` -- a letter and how many characters it
covers). That is the only way a module's colours reach a window, since a
window draws cells through its pane rather than bytes at the screen, and it
means nothing in the desktop parses an escape sequence. `-a` draws the
module's own lines, boxes, arrows and bars out of ASCII for
`DT_GLYPHSET=ascii`; a *label* stays the text the user wrote.

**`widgets/putruns.hibr`** is that drawing, lifted out of About rather than
written a third time -- About, the diagram editor and Write all draw runs
now, and About was converted in the same release. It takes its pens as
`letter=colour` words parsed once a frame, and measures a row in columns
rather than characters only when the row holds a glyph wider than a cell,
which costs one `str width` a row to notice.

**The app** (`apps/Office/diagram.hibr`) is a split window: the text in
`widgets/textarea.hibr` as it comes, the diagram beside it, re-rendered
only when the buffer's own `gen` moves. The divider drags and is
remembered, the diagram scrolls both ways with its own bars when it is
bigger than its half and says so rather than cutting it off, f6 moves
between the halves, and the Diagram menu holds one sample of each kind --
which is how a language nobody can guess gets found by trying it. It offers
a `_dirty`, and says in the file that it has no `dt_want` to re-arm because
nothing in it asks for a frame.

**Write** draws a ```` ```mermaid ```` block as the diagram where it sits,
and shows its source again the moment the cursor is anywhere inside it.
`md lines` letters a fence `m` and says nothing about the info string, so
the fence line is read for the word after the backticks; the block's lines
are scanned once per change rather than per line per frame, and the drawing
is cached against the document's own `gen`. The rule -- the opening fence
takes the diagram's rows, the rest of the block takes none, and the whole
block shows as source when the cursor is in it -- **has to be applied by
every loop that counts rows**, and the one that computes where the cursor
is was missed: the cursor drew two rows below where it was. Both halves are
checked now.

Three things found on the way, each recorded in `CLAUDE.md`: a map key
named `*` cannot be read back in this shell, which is a divergence from
bash and is now Gitea #148 (the widget uses `else`); `TB[id]["ver"]` starts
again at 0 on `tb_set`, so a cache keyed on it never notices a new
document and `gen` is the one that never repeats; and a `dt_tput` whose
column is written inline as `$((sp + 2))` loses its string from the
catalogue, which this release hit for the fourth time.

`tests/mermaid.py` holds the properties a drawing must have whatever it
looks like -- no two boxes overlap, every edge ends on a box it names,
every run list covers its row exactly, every style letter is one the
contract names -- plus the app and Write through a pty, 99 checks;
`tests/999-mermaid.t` records what the corpus draws, and says in the file
to re-record it on a deliberate layout change after reading the diff.

## 0.99.116

**About This Computer shows the machine's own picture, in colour, with its
three facts pinned above the box** (Gitea #139). Asked for as *"show the
actual operating system sysinfo? what do you think, and in color and so
on?"*, and *"can we move it so hostname, uptime, users ... then the scroll
box sysinfo"*.

`about_sysinfo` ran `sysinfo -p -l hibr`, which forces **hibr's own**
picture: every machine showed it beside its own name, which is the one way
the module's README says not to run it. No `-l` at all now, so a Debian box
shows Debian's and a Mac shows the Apple -- and `si_logo` already falls
back to hibr's for a system that says nothing either way, so nothing had to
be added to keep a way back to it.

The colour needed a decision rather than a flag. sysinfo's art carries tone
marks and `si_art` turned them into escapes **only when `isatty(1)`**, and
About captured the block with `$( )`, which is a pipe -- so the block was
grey, and an escape sequence would have been no use to a window anyway,
which draws cells through its pane rather than bytes at the screen. So
sysinfo now fills the **result slot** when one is bound, the way `md lines`
does: `d["text"][i]` is the line and `d["runs"][i]` is the style of every
character in it, as `a:23 .:10 k:2 .:19` -- a letter and how many
characters it covers, the whole line covered. `about_putrun` sets a pen per
run and clips each to the horizontal offset. Nothing parses an escape, and
`:=` on a builtin does not fork, so the block costs one call rather than a
`$( )` child.

Three decisions taken here rather than asked about, each because the
alternative was worse:

- **The three facts are pinned; the versions stay under the box.** The
  request lists hostname, uptime, users, then the versions, then the box --
  but an earlier one, explicitly, was *"can we move the hibr desktop
  version till the end, under the scrollable box"*, which 0.99.113 did. Read
  top to bottom the window is now hostname, uptime, users, the box, the
  versions: the asked-for order, with the box where the earlier message put
  it. Say so, because it is the one place the two requests disagree.
- **The warm tone is `DT_WARN`, not the ticket's `DT_WELL`.** `DT_WELL` is
  a *surface* colour (`#2d3748` on midnight) and would have been all but
  invisible on a window's face. The cool tone is `DT_INFO` as the ticket
  said.
- **The warm tone is tested through `about_putrun` directly**, because only
  the CIX art carries a second tone mark: a Debian or a Mac About box has
  one tone in it, so a test driven through the window alone would have
  shipped that pen untried.

About also gained an **`about_dirty`**, because the window is expensive and
did not know it: everything it shows is on the `AB_SLOWMS` clock, so a
frame inside that period redraws what is already on screen, cell for cell.
At 80 by 24 it was **4.0 ms a frame** with the runs and 3.4 without them,
and it is **0.6 ms** on a frame it is left alone -- and before this, every
frame any *other* window asked for paid it again. Nothing can be stale for
longer than `AB_SLOWMS`, because the reading falling due is itself what
makes it dirty. The dirty check also **re-arms the frame request**, since
the `dt_want` that asks for the next reading lives in `_draw`, which it has
just said to skip: the trap this tree already carries about looking for
what asks for the next frame from inside anything being gated. Guarded on
`DT_WANT` rather than by counting frames -- the harness's own desktop has
other things that wake it, and a frame count cannot tell them apart.

## 0.99.115

**A command substitution came back empty, with status 141, when a signal
interrupted the parent's read of its pipe** (Gitea #144). 141 is 128 plus
SIGPIPE: `xcap` read the child's output with

```c
	while ((n = read(pf[0], buf, HIBR_IOCH)) > 0)
```

and `read` answering -1 with EINTR ends that loop exactly like an end of
file -- so the parent closed the pipe and the child, still writing, was
killed by it. An interrupted read is not an end of input.

The signal is not exotic. A script's own `trap ... WINCH` is installed with
**no `SA_RESTART`**, deliberately, because bash interrupts a blocking
`read` builtin so the trap can run and hibr matches it; so any shell with
a trap on a signal that actually arrives -- the console's SIGWINCH on every
terminal resize, every attach or detach of a held session -- could lose a
substitution. Measured: 60 substitutions against a signal every 15 ms lost
**2, 1, 3, 3 and 2** of them on 0.99.114, five runs out of five, and none
in fifteen runs after.

`io_rdall` retries EINTR. Two reads go through it because the symptom is
theirs -- `xcap`'s pipe, measured above, and the `:=` capture of a
program's output (ADR 0038): both read from a child of this shell that is
still writing, which is what makes an early close fatal rather than merely
short. Two more go through it for consistency and not for a symptom:
`xcapfile` for `$(< file)`, and `in_line`'s seekable branch for a script's
own text. Both of those read a regular file, which a local filesystem does
not interrupt -- and `in_line`'s *other* branch, the one a piped script
takes, has always retried EINTR by hand, which is where the habit should
have come from in the first place. Two kinds of read stay interruptible on
purpose, because a trap has to be able to break a wait that may never end:
the `read` builtin, where bash is interrupted too, and `recv` on a socket,
whose peer is not ours. A truncated socket read is the same class of bug
and is named in the ticket rather than changed here.

**Found by the test shipped with #143 failing about one run in ten**, on
the release it was written for -- whose gate passed by luck. `#143`'s own
note called that status "not reliably reproducible" rather than running it
in a loop and counting; it was reproducible, and it was a different bug.
`tests/999-readsig.t` is the guard and is certain rather than lucky: it
fails 3 runs out of 3 against 0.99.114. `tests/999-waitsig.t` no longer
counts a substitution's status, and says why.

## 0.99.114

**An interrupted wait abandoned its child, so a desktop collected zombies
all day** (Gitea #143). Reported as *"I see many processes that are hibr's
but have zero memory or zero cpu?"* -- which is what a zombie is: a child
that has already exited, whose status nobody collected, so the kernel keeps
the record and nothing else. It costs a pid slot and a line in `ps`.

`jc_waitt`'s no-timeout path was `return waitpid(wp, w, fl)`: one call, no
retry. A signal with a handler makes `waitpid` answer **EINTR**, and that
went straight back to the caller, which took it for the command having
finished -- it read a status that was never written (an uninitialised
`int`, usually a stale 0, so the command read as having succeeded) and
**nothing ever waited for the child again**. `jc_poll`'s drain loop had the
same shape: `while (waitpid(...) > 0)` stops on EINTR and leaves the rest
of a burst. Both retry now.

The signals that do this are not exotic. The console installs
**SIGWINCH**, so every terminal resize -- and every attach, detach or
resize of a **held** session, which is every desktop -- is one. Measured
under a SIGWINCH storm: **2 zombies out of 12 forks before, 0 after**, and
the twelve under the reporter's own 40-hour desktop are this: eight in a
process group of their own (`ex_bg` children whose drain loop stopped) and
four in the desktop's (synchronous forks whose wait was interrupted).
`tests/999-waitsig.t` is the guard and fails against 0.99.113.

**A TLS connection also left one, for the life of the shell** (same
ticket).
Reported as *"I see many processes that are hibr's but have zero memory or
zero cpu?"* -- which is what a zombie is: a child that has already exited,
whose status nobody collected, so the kernel keeps the record and nothing
else. It costs a pid slot and a line in `ps`.

`net_tls` forked the TLS relay, logged its pid and threw it away. Nothing
waited for it, and nothing was in a position to: the descriptor it serves
can be dup'd, inherited, passed to a child or closed anywhere, so no one
place knows the relay has ended. Measured as three `/dev/tls` connections
leaving three relay children, each becoming a permanent zombie as it
exited.

`listen -f`'s per-connection child had the other half: it was reaped with
`waitpid(-1, 0, WNOHANG)` -- one child, any child, once per connection --
which left the last connection's child a zombie until the next arrived, a
burst leaving the rest, and which could collect a child a **module**
started, whose module then could not read its own status. That is the trap
this shell's narrow reaping (`jc_chld && s->jobs.n`, each job on its own
process group) exists to avoid, reintroduced in one line.

Both are a second fork now, the way `hold`'s own server already did it:
the helper belongs to init, and its lifetime is its peer closing rather
than the shell's bookkeeping. `tests/999-relay.t` guards it, and
`CLAUDE.md` now asks the question out loud for any `fork()` added anywhere:
who waits for this?

Found by the new test's own output: a failed handshake printed
`failed: (null)` whenever OpenSSL had no reason string for the code, which
is the least useful thing a diagnostic can say. It says `handshake error`.

**What this does not explain, and the ticket says so.** The twelve zombies
under the reporter's own desktop are not these: nothing in the desktop uses
`/dev/tls` -- `dav` and `email` fork their own relays and wait for them.
Every fork in `src/` and in every module was read against its wait, and
every ordinary shape was measured in a real desktop (jobs, subshells,
pipelines, command and process substitutions, a terminal whose program
exits): all clean. What is known about the twelve is that none of them ever
`exec`'d, eight were in a process group of their own and four in the
desktop's, and they arrive about one per one to three hours of use. The
next step is the shell's own `-d 2` log, which records every fork it makes.

## 0.99.113

**About's versions move under the box, and the mouse scrolls it sideways**
(Gitea #139, the rest of it). Asked for as *"can we move the hibr desktop
version till the end, under the scrollable box?"* and *"the scrollable box
is not scrollable using the mouse, only keys?"*.

The box starts at the top of the window now, so what the machine *is*
reads first and twelve of its lines show at once rather than eight; what
this *desktop* is -- the desktop version, hibr's own and the module ABI,
on one line, with the tagline above it -- is the footer, between the
horizontal bar and the meters.

**A sideways wheel was being read as a vertical one**, which is a console
bug and not only an About one: a terminal reports a tilting wheel or a
trackpad's horizontal swipe as SGR buttons 66 and 67, and `cn_dec` tested
only bit 0 of the button -- so 66 decoded as `wheelup` and 67 as
`wheeldown`, and a sideways scroll moved a list up and down. They decode
as `wheelleft` and `wheelright` now, the window manager hands both to a
window's own `_wheel` as `left` and `right`, and **shift with an ordinary
wheel** does the same, which is the only way in on a mouse that does not
tilt. About scrolls its box sideways with all three. Nothing else is
changed by it: the Control Strip and the workspace wheel act on up and
down and ignore the rest, which they did not before -- a sideways wheel on
the desktop background used to step a workspace forward.

The vertical wheel always worked, and still does -- measured through a pty
either way before changing anything.

## 0.99.112

**A setting for module autoload: `off`, `after` the programs on `PATH`, or
`before` them** (Gitea #142). Until now autoloading was a
`command_not_found` function that `deploy.sh` wrote into a new install's
`~/.hibrc` -- so whether `sysinfo` worked out of the box depended on which
installer you used, and a plain `brew install hibr` or the apt package
never ran `deploy.sh` and never got one. That was reported as *"`sysinfo`
did not autoload"*, and it is not a mechanism if its presence depends on
the installer.

`HIBR_MODULES` is one of three words, and the shell does the loading
(ADR 0040):

| value | the command search |
|---|---|
| `off` | alias, function, builtin, PATH. A module needs `need` or `mod load`. |
| `after` (**the default**) | ...then a module that declares that builtin, if nothing else answered. |
| `before` | alias, function, builtin, **module**, PATH -- hibr's own `ls`, `cat` and `most` in front of the system ones. |

`--modules=off|after|before` sets it for one invocation, and an assignment
prefix works where you would want one: `HIBR_MODULES=before ls` is the
module's `ls` for that one command. A module already loaded is already a
builtin and shadows `PATH` whatever this says -- that is what `mod load`
means, and this setting governs *autoloading*.

`after` is the default because it cannot change what any existing script
does: it fires only where the command does not exist, which was an error
before. `sysinfo`, `most`, `hvi`, `trace` and `img` now work from a fresh
install of any kind with no startup file at all.

**The one real cost, and how it is paid.** Finding which module declares a
builtin means walking the module path and opening each candidate to read
its descriptor -- 4.3 ms for this machine's 35 modules. For `after` that is
paid only by a command that would otherwise be "not found". For `before` it
would be paid on *every* command that is not a builtin or a function --
every `ls`, `git`, `grep` -- so the first command that reaches it builds a
sorted name-to-object index and every later lookup is a `bsearch`. Measured
against 0.99.111 on a 60,000-iteration builtin loop (137-145 ms before,
124-145 ms after) and on 400 forks: `off` and `after` cost nothing, the
added work being one cached integer and one call per command that is
neither a builtin nor a function.

A `--plan` run never autoloads -- a module's init may do anything and a dry
run has promised not to -- root indexes `HIBR_MODDIR` alone, as every other
module load does (ADR 0016), and a module *installed* after this shell
started is not in its index until a new shell; `need` and `mod load` still
find one by name. `deploy.sh` no longer writes the function, and an
existing `~/.hibrc` that still has one is harmless: under `after` the
module has already answered before anything reaches it.

**Caught by this release's own gate, and worth more than the feature.** The
cached mode started life as `int m_md` in `src/mod.c`, and `m_md` is the
**md module's own builtin handler**: the shell is linked `-rdynamic`, so the
shell's variable preempted the module's function and calling it jumped to a
data address. **678 sanitizer reports**, every one a SEGV "in m_md", with
`955-md`, `885-plan` and four of Write's own checks failing beside them --
which reads as the md module being broken. This file has recorded that trap
twice in the other direction (a module naming a function the shell exports);
this is the first time the *shell* grew a name a module already had, and a
module's handler is `m_<builtin>` by convention, so any new short `m_<word>`
in `src/` is a candidate. `tests/999-symbols.t` is now the check rather than
the habit, both ways round, and it fails against exactly that collision.

Also fixed: `tests/760-term.t` polled a fixed fourteen times for an inner
shell's prompt, and under a full gate's load the rows came back one frame
early -- `echo 42 / in> echo 42 / 42` -- which reads as the line editor
putting things in the wrong order. It waits for the state now, as the rest
of the pty tests do.

## 0.99.111

**A login shell: `-l`, `/etc/profile`, and the profile files round it**
(Gitea #141). hibr read **nothing** at login until now -- `~/.hibrc` when
stdin was a terminal, and that was all -- so a hibr set as someone's login
shell, which `deploy.sh` offers to arrange and `/etc/shells` makes
possible, missed the `PATH` a distribution puts in `/etc/profile`, its
`umask`, everything in `/etc/profile.d/*.sh`, and every setting an
administrator has made for every shell on the machine.

`hibr -l` or `hibr --login`, **or** a shell whose `argv[0]` begins with a
dash -- which is the only signal `login`, `getty` and `sshd` give -- now
reads `/etc/profile`, then the first that exists of `~/.hibr_profile`,
`~/.hibr_login` and `~/.profile`, and `~/.hibr_logout` on the way out.
Those are read whether or not the shell is interactive, as bash does with
`--login`, which is what makes the flag usable from a script. hibr's own
two names come first so that login setup meant for this shell has a file
of its own rather than a block guarded inside a `~/.profile` that bash and
dash also read.

**Where this parts company with bash, on purpose** (ADR 0039): an
interactive login shell reads `~/.hibrc` **as well**, after the profile.
bash reads only the profile, which is why an alias written in `~/.bashrc`
works in a terminal window and silently does not after `ssh host` until
somebody learns that `~/.bash_profile` has to source it by hand. zsh reads
both; so does hibr. The reason is the owner's own: *"I prefer this rather
than forcing people to learn stuff that's useless."*

The desktop's own login screen already execs the person's shell as
`-hibr`, so this is the release that makes that mean something.
`HIBR_PROFILE` names a different system profile, the way `HIBR_RC` already
names a different rc file -- for tests, which would otherwise read whatever
`/etc/profile` the machine running them has, and for a packager or a
container that keeps one elsewhere. `-n` and `--explain` read none of it,
having promised to run nothing.

## 0.99.110

**Every process a desktop runs can be told apart in `ps`** (Gitea #111).
Asked directly: everything read `hibr [desktop]` or a bare
`hibr`, so there was no way to tell which process was which terminal
window, and no way to end one by hand without guessing. A live session
looked like this, four of its windows indistinguishable:

```
2296370 3299222  hibr
2815600 3299222  hibr
3299222 3299220  hibr [desktop]
3299220       1  /usr/bin/hibr /usr/share/hibr/desktop/session.hibr
```

Now:

```
1846877  desktop
  1846881  desktop [Terminal 1]
  1846882  desktop [Terminal 2]
```

A shell wears `HIBR_PROCTITLE` from its environment at startup and
**removes the variable as it does**, so a parent can name the shell it
starts without that name reaching anything the shell itself goes on to
run. The desktop gives each terminal window's child
`desktop [<title> <id>]`, the number being the window id
`desktop ctl windows` lists it under, so the process and the window on
screen can be matched up. Only a hibr child is renamed: a window running
`vim` shows `vim`, which is what a person wants anyway, and nothing
renames a third-party program by rewriting its `argv[0]` -- that is what a
login shell, a busybox-style binary and `$0` all read, so it stays alone.

The desktop itself is `desktop [<session>]` and its supervisor
`desktop [<session>: supervisor]`, where before the supervisor showed its
whole command line and read as a second desktop.

One thing worth knowing for anything added here: `pt_rename` overwrites the
whole region `argv` lives in, so the title is worn *after* the script name
and the positional parameters have been copied out of it -- which is why
`sh_proctitle` is called once per branch in `main` rather than next to
`v_env`, where it would read earlier and be wrong.

## 0.99.109

**About This Computer is a box again, with both of its scrollbars doing
something** (Gitea #139, part of it). 0.99.105 made the window as tall as
its own content so that nothing would be below the fold -- which is also
how to guarantee a scrollbar is never seen, since a window the size of
what it holds has nothing to scroll. Reported as *"make the about box
smaller, I want to see the scroll bars both vertical and horizontal"*, and
quite right.

It asks for 18 rows by 52 columns now, whatever it holds. What it holds is
both longer and wider than that -- 27 lines, and sysinfo's logo rows run
to about 60 columns -- so there is a bar down the side and a new one along
the foot of the list. Lines are kept whole rather than cut with an
ellipsis, and the draw takes the slice the window can show, so the end of
a long line is scrolled to: left and right move four columns at a time,
and the pinned versions and the meters stay where they are. `dt_hscrollbar`
is the widget, beside `dt_scrollbar` in `widgets/scrollbar.hibr`: the same
arithmetic turned on its side, so the two stay in step by being the same
code.

Still open on #139: the facts moving above the versions, the machine's own
OS logo rather than hibr's, and colour.

## 0.99.108

**A Modules window: everything the module path can offer, loaded or not,
and a way to change either** (Gitea #133). Each row is a module's name,
version, whether it is loaded, the interface it offers and the builtins it
adds; the selected one's description and path are on the two lines below,
which is the part a list has no room for. Enter loads or unloads, `l` and
`u` do one each, `r` scans again, and the Module menu has the three.

It is built on `mod avail`, which walks the module path and reads each
object's descriptor **without calling its init** -- so a module can be
named, versioned and described before it has ever been loaded. That answer
is now **fields in a pipe**: one line per module of eight tab-separated
values (name, version, ABI, state, path, the interface it offers, its
builtins, its description), no header, the same rule `mod list` follows --
a program reading a tool's output wants fields, not a table. On a terminal
it is unchanged. `-` stands for anything absent, because tab is IFS
whitespace and an empty field would be lost, shifting every field after it.

**What it refuses, and why that is the interesting part.** The console,
pty, term and hold are what the desktop is drawn and held through, so
dropping one takes the screen away from the program doing it -- with the
refusal stubbed out for a moment, unloading the console from inside the
desktop printed a shell error, which is the gentler half of what it does.
And a loaded module that offers an interface may have handed its table to
another module, which `mod drop` would leave holding a pointer into an
unloaded object. The registry knows who *offers* an interface and not who
has taken it, so the refusal covers every loaded provider rather than only
the ones in use: the conservative answer, and the honest one until the
registry can say. Both are explained on the window's own bottom line for
the selected module, before anything is pressed.

A scan is 4.3 ms for this machine's 35 modules, because every one of them
is opened and closed again, so it happens when the window opens, after a
load or unload, and on Rescan -- never per frame, where it would be six
times the whole frame budget.

Also, found by this release's own gate and fixed rather than called a
flake: `tests/apps.py`'s `run()` sent its quitting keys without waiting
for a *program* in a terminal window to have answered, so under a full
gate's load the `qy` was typed into the shell first and the row read
`qyhi` where the check wanted `hi`. It takes an `until` now -- text, or a
function given the screen -- and the two checks that read what a program
printed use it.

## 0.99.107

**Task Manager shows everyone's processes, only yours, or only this
desktop's** (Gitea #131). Task > Show has the three, with a tick on the one
in force, and `w` cycles them. The uid is already read by the scan, so
"only mine" is one comparison per process and costs nothing; "this
desktop" is the desktop process and everything descended from it -- its
terminal windows' shells and whatever they are running -- found from the
kernel's own list of a process's children, so only the tree is read rather
than one file per process on the machine. Where that list is not there (a
kernel without `CONFIG_PROC_CHILDREN`, or macOS, which has no `/proc`) one
`ps` gives every process's parent and each chain is walked up instead. The
default, everyone's, pays a single string comparison per scan.

The window's title says which is in force -- `Task Manager [Mine]` -- so a
list that is short is never mysteriously short, and Control Panel > Task
Manager has the same choice as a Show row. The figures under the graphs
stay whole-machine whichever is chosen: they say what the machine is
doing, not what the list adds up to.

What this is **not**, and the ticket says why: a filter for "hibr desktop
applications". The desktop is one process and its apps are functions
inside it, which is the point of the design; "this desktop" is the honest
version of that question until #111 names each window's own process.

One trap found writing it, and it is this file's own: `read` fails at end
of input **having already assigned**, and `/proc/<pid>/task/<pid>/children`
has no trailing newline -- so `read ... || continue` found every process's
children to be none at all, and the filter showed the desktop alone.

## 0.99.106

**A video YouTube's own player gave up on froze where it stopped, half the
time** (Gitea #135). The app notices the page's error and loads the video
again -- that part always worked -- and then took it up where it was left
as soon as the page's player answered at all, which is before any of the
new stream has arrived. A seek drops our player and asks the tap to queue
each stream's init segment again, and the tap can only queue back an init
it has already seen: asked that early it empties the queue instead, the
page's own player is sent to a time in buffers nobody has filled, and what
comes back is one stream with no init segment and the other not at all. So
nothing ever decodes again -- the picture stays on the last frame it had
and the clock never moves. Taking a video up where it was left now waits
for the first of its own bytes to have been taken, which is the moment the
tap has an init to queue back; a new media source puts that back to
waiting, since its stream has not arrived either. The same race was there
for a video resumed from the history.

Measured rather than guessed: the suite's own check for this failed **5
runs in 10** in complete isolation, always with the page loaded twice and
the clock stopped dead at the second it gave up, and 10 in 10 pass now.
`tests/ytserve.py` can hold a page's first fragments back
(`/control?slow=VID:MS`) and reports a player asked to seek before it has
been fed anything (`/early`), so the new check fails 3 times in 3 against
the code before this and does not depend on winning a race to do it.

## 0.99.105

**About asks for a window one row taller, which is the row the Held line was
hiding in** (Gitea #130). `about_size` is consulted by `dt_launch` *before*
the first draw, so the line list it sizes from was empty and it guessed --
and the guess was one short, leaving `Held:` below the fold on a screen with
room for it. It builds the list and counts it now; `about_lines` needs no
window of its own and the sysinfo block it reads is cached, so asking costs
nothing. On a 40-row screen About opens with every line shown and no
scrollbar at all.

## 0.99.104

**About This Computer again, with sysinfo's logo and everything it knows**
(Gitea #130). 0.99.102's version of this was judged worse than what it
replaced — "why have you trashed our about Machine? ... We want the top to be
static ish the Desktop Version, and so on, and integrate sysinfo stuff like
the logo and more information" — and it was a fair call: it pinned the
*tagline* and let the versions scroll, which is backwards, and it added
nothing to look at.

What is pinned at the top is now the identity: **hibr desktop v…**, **hibr
v…, module ABI …**, and the tagline. Everything else scrolls under it, and
the CPU and memory meters stay anchored to the bottom.

**`mods/sysinfo`'s own block is in it, logo and all.** `sysinfo -p -l hibr`
is twelve lines and sixty-two columns of escape-free text — the hibr logo on
the left, and on the right the OS, the architecture, the processor, the
memory, the **disk** and the **load**, none of which this app knew how to
find. It is captured with `$( )` and refreshed at most every `AB_SLOWMS`; it
costs 2 ms.

**The logo is text, not a picture, which is why this was possible at all.**
#130 said a picture needed `dp_api` to grow a paned put first, and that is
still true of a bitmap — but `si_arts[]` is a table of string arrays with
colour marks, so the logo draws through a window's pane like any other text.
Lumping the two together was wrong, and the ticket said so for a release.

The window asks for its own size now (`about_size`, which is what
`dt_launch` consults): tall enough to show the lot where the screen allows,
clamped to what it does not, 66 columns for sysinfo's 62. On a tall screen
nothing scrolls; at 80 by 24 the foot of it is a wheel or an **end** away.

About's own `Kernel:` line went, because sysinfo says it better, and the
second tagline with it. `Hostname:`, `Uptime:` and `Users:` stay as labelled
lines above the block, because they are what a window pinned small by a
caller should still show.

Five checks: what it opens on, that sysinfo's block and logo are in it, the
terminal section and the foot of the list reached with **end**, and the held
desktop's own Held line, which also needed the end key now that it is at the
bottom.

## 0.99.103

**`mod list` fits the terminal it is printed on, and is still plain in a
pipe** (Gitea #132). Reported as "we need to fix the mod listing and stuff,
it's really nasty over two lines". Measured, with the modules a desktop
loads, on an eighty-column terminal:

     85 |hold         0.22     abi 16  sessions that outlive the terminal they were started on
     83 |pty          0.21     abi 16  pseudo terminals: spawn a program on one and drive it
     78 |term         0.24     abi 16  a terminal emulator: a program's screen as cells
     75 |img          0.21     abi 16  decode an image and draw it as terminal cells
     75 |console      0.21     abi 16  a text display: cells, panes and decoded keys
     74 |lines        1.0      abi 16  show, search and edit files by line, exactly

Six over eighty, so two wrapped and every other row was a continuation. The
columns were fixed widths wide enough for nothing in particular, and the
description was printed whole however narrow the terminal was.

Two forms now, decided on `isatty(1)` — the discipline the cat module
already keeps:

- **In a pipe**, byte for byte what it always printed. `tests/090-module.t`
  records it and anything parsing it reads that, so it could not move.
- **On a terminal**, fitted: sorted by name, since the load order is not
  interesting; the name and version columns only as wide as the widest
  actually present; the ABI said once at the foot rather than repeated in
  every row, because a module whose ABI does not match is refused at load so
  every loaded one has the same; and the description cut to what is left,
  backing off a UTF-8 continuation byte rather than cutting a character in
  half.

      MODULE   VERSION  DESCRIPTION
      console  0.21     a text display: cells, panes and decoded keys
      hold     0.22     sessions that outlive the terminal they were started on
      img      0.21     decode an image and draw it as terminal cells
      pty      0.21     pseudo terminals: spawn a program on one and drive it
      term     0.24     a terminal emulator: a program's screen as cells
      5 loaded, module ABI 16

Widest line: **73** of 80, **49** of 50, **35** of 36. One column is left
unwritten on purpose -- a row exactly as wide as the terminal is one some
terminals wrap on the last cell, which is the wrapping this is here to stop,
and the first version of the arithmetic counted three separator columns
where it prints four and so came out one over at every width.

Checked in `tests/090-module.t` itself, through the pty module at 80, 50 and
36 columns, as a **property** -- "the widest line is under the width" --
rather than recorded row by row, so a module's description changing does not
re-record the file.

## 0.99.102

**About This Computer has headings, a scrolling middle and the meters pinned
to the bottom** (Gitea #130). Asked for as "I do not like the about box ...
can we make it prettier, and maybe have a box that's scrollable in it ... It's
got great elements (the cpu/mem thing), but we can do better, right?" — and
the meters were the part to keep, so they stayed and everything round them
changed.

The window is three headed sections now — **This Computer**, **This
Desktop**, **This Terminal** — with the tagline fixed on the first row, the
facts between scrolling, and the CPU and memory bars anchored to the last two
rows so they stay put while the facts move. At its own 22 rows every line
fits and no scrollbar is drawn; a shorter window scrolls, by the wheel or by
up, down, page up, page down, home and end.

**`dt_scrollfit` is a widget now, beside `dt_scrollbar`.** The Control Panel's
picker has scrolled since 0.94, but as three comparisons written out twice in
its own file — clamp the top, and bring a line into view. About wanted the
same, so they moved into `widgets/scrollbar.hibr` where the bar's own comment
already says a fix or a restyle should be one place and not two. The panel was
lifted onto it in this same change, and its own fifteen scrolling checks are
what say the lift changed no behaviour. The function is pure: the offset stays
under the window id in each app's own map, so two callers cannot fight over
it.

**What is deliberately not in it.** A module list was built and taken out
again: crammed onto one truncated line it was exactly the "really nasty"
the owner then reported, and it belongs in a module manager of its own
(Gitea #132). And a picture — `sysinfo`'s, or any — is still out, because
`mods/sysinfo` writes with `printf` straight to stdout and `img draw` has no
paned form: drawing either inside a window means drawing a window's own
content at absolute screen coordinates, which is the Control Panel preview
trap. That waits on `dp_api` growing a paned put, the same gap the pane
compositor waited on, and #130 stays open for it.

**Two faults found while building it, both caught by driving it rather than
reading it.**

`mod list` **prints; it does not fill the result slot.** `m := mod list` left
`m` empty and sent the listing to the real terminal underneath the console's
own drawing — the `:=` trap this tree records for a program on the `PATH`,
in a builtin. Visible in the probe as `abi 1` leaking outside the window's
border.

The wheel callback is **`(id, dir, row, col)`**, which `tasks_wheel` already
declares. Written as `(id, r, c, dir)` the word `down` is bound to an `int`
parameter, a declared function refuses that, and **the body never runs** —
the 0.44 census trap, and it looks exactly like a wheel report never
arriving.

And one that would have shipped silently: each line is translated **as it is
built**, because the draw prints a whole line with `%s` and `dt_tput`
translates its *format*, so every label would have come out in English in
every language. `tests/993-lang-ar` is what says it did not.

## 0.99.101

**The wallpaper is sent at half the pixels, which takes a reattach over ssh
from thirteen seconds to three** (Gitea #129, the first half). Reported as "I
just did a `desktop -r`, it takes a long time to re-attach, so nothing ...
10/15 seconds-ish, then the desktop appears" — over ssh.

A picture under the text is the wallpaper, `hold` re-emits every picture to
each client that attaches, and the kitty encoder sends `a=T,f=24` — three
bytes a pixel, uncompressed. At 232x71 with an 8x16 cell that is 1856x1136x3
raw, **8.04 MB of base64 on every attach**. Measured on the owner's own
wallpaper:

| | on the wire | at 5 Mbit/s |
|---|---|---|
| before | 8,477,585 bytes | **13.6 s** |
| now | 2,132,263 bytes | **3.4 s** |

**What was not the cause**, each measured rather than reasoned about:
`hold list`, which `desktop -r` calls first, is 9 ms over five sessions; the
client never loads the apps at all, because `dt_autohold` exits before that;
the wallpaper's cold decode is 262 ms as half blocks and 429 ms as pixels;
and a real `desktop --resume` locally, with the owner's own settings, draws
the bar in **0.12 s**. The desktop's work was never the problem — the payload
was.

`cn_image` already halved a picture that is *not* a still, because a film's
cost is bytes rather than encoding. A still was exempt, and the flag that
says so, `DP_IMG_CHOSEN`, means "a palette was chosen from this picture" —
which matters to **sixel** and nothing at all to kitty. So for kitty it was
being read as "send every pixel". A picture **under the text** is now halved
too: it is a background that the terminal scales back up, not something being
looked at. Only for kitty — a sixel paints 1:1, so its rectangle has to be
exactly the cells it covers.

Nothing else changes: the Image Viewer, a film and the browser all draw
pictures that own their cells.

**`console imgdetail full|half`**, and **Control Panel > Pictures >
Wallpaper Detail** (`DT_WALLDETAIL`, half by default) for anyone who would
rather have the pixels. With no argument the verb answers which is in force.

Two checks in `tests/kitgfx.py`, on the payload's own length rather than on
anything about how it looks, since that is the whole point: half is
`48*48*3` base64 where full is `96*96*3`, four times the bytes.

**The other half of #129 is still open.** `o=z` (zlib of the raw pixels) and
`f=100` (PNG) are what the protocol offers beyond this, and measured on the
same wallpaper at 1856x1136 they are 1.88 MB and **1.36 MB** against today's
8.04 — so compression is worth more than the earlier note in that ticket
guessed (it said 1.5-2x for `o=z`; it is 4.3x). Both need a deflate
**encoder**: `mods/inflate.c` only expands. Halving and PNG together would be
about 0.35 MB, a twenty-third of what it was, and 0.6 s on that link.

## 0.99.100

**A resize that arrives while the screen saver or the lock is up is acted on**
(Gitea #126). Reported twice from a live session — "the menubar did not
resize, the wallpaper did not adjust, and the control strip is off the
screen" after moving machines, and then again, with the sequence spelled
out: *detached from a device, re-attached on another of a different size, on
connection we don't resize.* The second telling is what found it, because the
missing ingredient was never the detach or the attach.

`dt_run`'s loop opens with

    if [ -n "$DT_SAVING" ] || [ -n "$DT_LOCKED" ]; then
            ...
            continue

and that `continue` is **before** the `console resizing` check at the foot of
the loop. `dt_saverinput` also throws a `resize` key away on purpose, so
neither the saver nor the lock is dismissed by one. Between them, nothing
recorded that the screen had changed size, and `rsz` stayed 0 — so there was
no pending resize to settle when the lock cleared either. The desktop carried
on drawing for the old screen indefinitely: the bar composed too wide with
the clock off the end, the wallpaper sized for the screen that had gone, the
Control Strip off the bottom.

**Which is the ordinary way of it, not a corner.** You detach from one
machine, it locks while you are away, and you attach from another with a
different screen. Measured, 40x120 then 18x60 with the saver up across the
move:

| | before | now |
|---|---|---|
| menu bar composed for | 120 columns (23 drawn of 60) | **56 of 60** |
| Control Strip | **off the screen** | row 17 of 18 |

The branch checks `console resizing` and re-reads the size itself now. No
debounce there: the saver redraws from scratch on every pass anyway, and
`dt_size` is idempotent.

**Why four probes missed it.** A clean detach and reattach, a client killed
outright, a plain `hold attach` over a live client, and `desktop --resume` at
the owner's own sizes with a copy of their own settings all resized
correctly — on this version and on 0.99.93, the one the live session was
running. 0.99.98 added four checks for exactly that and they passed. **None
of them had a saver or a lock up**, which is the one state in which the loop
takes a different path, and no suite had ever driven a resize in it. The new
check starts the saver from the hibr menu, detaches, reattaches at a smaller
size, and dismisses it; it fails against 0.99.99.

## 0.99.99

**A dropdown is a rectangle again: every dimmed row was a column short**
(Gitea #128). Reported from a live session as "drop downs that are not
rectangles, they have rows shorter than others? like window/edit for
terminal and many many more". Measured on the Window menu with a window
focused, each row's run of menu-background cells:

| row | before | now |
|---|---|---|
| `Tile Workspace` | cols 10..41 | 10..41 |
| `Make Main` (the one dimmed item) | cols 10..**40** | 10..**41** |
| `Cycle` | cols 16..41 | 16..41 |

`dt_menu_draw`'s no-underline branch painted the label to `w - 3` and then
the key field as the literal `" $k "` — **three columns only when `$k` is
exactly one character.** `dt_dim` is `dt_row "" 1 "$label" "" :`, so its key
is **empty** and the field was two columns; a `dt_mark` with no key (the
Stickies Color submenu) is the same; and a key of *two* characters — a
workspace number past 9 on Move to Workspace — went a column the other way,
over the edge. The field is padded into its three columns now rather than
interpolated, so an empty, a one- and a two-character key all come out the
same width. `str pad` counts display columns, which is what the field is
measured in.

**"Many many more" is one function.** Everything shaped like a dropdown goes
through `dt_menu_draw` — every bar menu, every submenu, every context menu
(`dt_ctxbuild` appends `MB[]` entries it draws), and every Control Panel and
Control Strip dropdown, since `dt_droplist` reaches it through
`dt_context_open widget`. The app menu (`dt_drop_apps`) pads every row to the
full width and was already right; so was the mirrored right-to-left branch.

**A second fault found beside it.** `dt_mpick` clamped the hit rectangle's
width to a minimum of **12** where the drawing clamps to **14**, and
recomputed it from `MB[..]["w"]` instead of reading the `dw` the draw
recorded — so a menu narrower than 14 was drawn two columns wider than it
answered a click in. It reads `dw` now, with the drawing's own minimum.

Both are paint and hit-testing only: `dt_mpick` sizes from the box rather
than from what a row painted, so a click in the last column of a dimmed row
always did reach the item.

The check measures the **pens**, not the glyphs, because a dimmed row changes
only the foreground — the glyphs of a row padded a column short look exactly
like one that was not. It asserts every row of an open menu ends at the same
column, and it fails against 0.99.98.

## 0.99.98

**A held desktop reattached on a smaller screen is now tested, which it never
was** (Gitea #126). Reported from a live session after moving machines: the
menu bar had not resized so the clock was off the end, the wallpaper had not
adjusted, and the Control Strip was off the screen. **That could not be
answered from the suites either way**, because every `hold attach` in them
used the same size as the `hold new` before it — and the whole point of hold
is that the desktop outlives the terminal it was started on, so the next
terminal to attach is routinely a different size.

Four checks now start a held desktop at 30x100, detach, reattach at 18x60 and
assert the menu bar is composed for the new screen, the Control Strip is
pulled back onto it, and the wallpaper is repainted out to the new bottom
corner.

**What the investigation found, since the answer matters more than the fix
would have.** Reproduced five ways on 0.99.98 and, where it could be, on
0.99.93 — the version the live session was actually running — all correct:

| what was done | bar | strip row | clock |
|---|---|---|---|
| detach, reattach at 24x80 | 116 -> 76 cols | 32 -> 23 | visible |
| the client **killed** outright, then reattach | 116 -> 76 | 32 -> 23 | visible |
| a plain `hold attach` while the first client was still live | 116 -> 76 | 32 -> 23 | visible |
| `desktop --resume` at 49x175 with the owner's own settings | 228 -> 171 | 65 -> 48 | visible |

So a reattach at a new size has not been broken, and `--resume` takes the
session over rather than joining it. The one arrangement that *does* produce
all three symptoms is a second display **joined** while the first is still
attached (`hold attach -m`, which `desktop --join` runs): the session keeps
the first client's size and the new terminal gets a cropped view, with the
bar and the strip drawn at `DT_PRIMARY_COL`/`DT_PRIMARY_W` — the primary
display's, not the new one's. That is the multi-display path working as
designed.

The ticket stays open: something was seen that none of this explains, and
now there is a test that would catch it.

**One thing the same investigation did settle.** The terminal on the new
machine reports no cell size at all — its pty's `ws_xpixel`/`ws_ypixel` are
zero — so pictures are off entirely there and a wallpaper cannot be pixels on
it, whatever `DT_IMGMODE` says. That is a true thing about that terminal
rather than a fault, and since 0.99.94 About says so in as many words:
*Pictures: blocks only, no cell size given*.

## 0.99.97

**The Control Strip draws on every frame, because its cells were never its
own to keep** (Gitea #125). Reported from a live session as "the control
strip when it's open, it blinks, and does not refresh properly". It was not
blinking out — it was **covered nearly all the time and flashing back about
once a second**. The strip writes at absolute coordinates, *after* every
window, so it is drawn over them; and a window whose app offers no `_dirty`
is redrawn every frame, which wipes the strip's cells. Its gate — its own
shape, the input count, the wallpaper having painted, otherwise at most once
a second — knew nothing about a window having repainted.

Measured at 80x24 with the strip at its default row 19 and one 10x60 window
over it, the window asking for the next frame the way a focused terminal
with a blinking cursor does, sampling the strip's row every 40 ms for 2.8 s:

| the strip's row | before | now |
|---|---|---|
| fully drawn | **5** of 70 samples | **70** of 70 |
| covered by the window | 65 of 70 | none |

`cs_y` is 80% down the screen by default, so a window over the strip is the
normal case rather than an edge one. Its shadow went with it: `console darken
-s` marks each cell it shades and any ordinary write clears that mark.

**Why no suite caught it, and what the new ones do differently.** An idle
desktop barely draws at all, and the few frames it does are the wallpaper's
own once-a-second ones — which the old gate already redrew on. The first
probe written for this, with the window in place but nothing asking for
frames, showed one state across every sample and looked perfectly healthy.
What it needs is *frames happening continuously*, so the two new checks in
`tests/desktop.py` drive a window that asks for the next frame, sample the
row thirty times at 40 ms, and assert the strip's text is in every one of
them and its shadow's pen never changes. Both were run against 0.99.96's
`strip.hibr` first and both fail there.

**What it costs, and the cheaper thing that was refused.** Drawing it every
frame is **186 us against 23 us skipped** — +163 us on a 963 us frame at
232x71, about 17%. Attribution by stubbing: not `nextprayer_draw` (11 us)
and not the shadow; it is the body's own two dozen `console` calls. A flag
for "a window covering this row was drawn", set in `dt_win` the way
`DT_WALLDREW` was, would be one comparison a window a frame — but in the
case that matters it fires on *every* frame anyway, so it saves nothing, and
it is one more rule that has to stay in step with everything that repaints.
`dt_shadow`'s own comment records that going wrong three times, the Control
Strip blinking among them. Making this cheaper means fewer `console` calls,
or cells of its own to composite — a measured next saving, not this fix.

**`DT_WALLDREW` is retired.** The strip's gate was its only reader, and the
gate is gone; the variable, the line that set it and the line that cleared
it each frame have all gone with it.

`tests/uifuzz.py oracle` at `ON=100` is clean, which is the check that a
thing drawn at absolute coordinates over the finished frame has not
desynchronised the forced-redraw comparison — and that a shadow cast over a
window's own face every frame is not darkening twice.

## 0.99.96

**The display recognises a picture four times faster, and hashing the cells
under one got cheaper too** (Gitea #113). `cn_image` hashes the whole picture
it is handed to decide whether it is the one already placed at that
rectangle, and `cn_hash` read a byte at a time. A screenful of pixels at
232x71 with an 8x16 cell is 6.03 MB. Measured end to end — a second
placement of the same picture at that size, which is a pixel-cache hit and
the hash and nothing else, since `cn_image` returns as soon as the sum
matches:

| | 0.99.95 | now |
|---|---|---|
| a repeat placement at 232x71 | **17.5 ms** | **4.7 ms** |
| the hash alone, 6.03 MB | 14.9 ms | 3.45 ms |

Over `CN_HASHWORD` (64) bytes it is the 64-bit form read eight bytes at a
time, from an aligned pointer so nothing is read at an offset UBSan would
object to. Under it, byte for byte the code that was there — **because a
word at a time everywhere would have been a regression on the path that
matters.** A four-byte call costs 13.1 ns that way against 10.2, and
`cn_imgunder` makes one per field per cell, every frame, for every region
that owns its cells: 49,416 of them for a full-screen sixel wallpaper. That
is 0.15 ms a frame, a fifth of the desktop's whole frame budget, to save
11 ms on a placement the keep has already made rare. The measurement that
found it was of the small calls, not the big one.

**And that per-frame path is now 23% cheaper rather than merely unharmed.** A
`cn_cell` has a pointer between `cp` and `fg`, so the three fields are not a
contiguous run to hash in place and were three calls of four bytes; they are
one call of twelve now. For a full screen: **0.500 ms a frame to 0.383.**

**What was not done, and why.** The ticket's second option — `dp->image`
taking an identity the caller already has, so the console compares that
instead of hashing — was measured rather than assumed. After this change a
film's frame at 232x71, which the kitty encoder halves, is 1.58 MB and so
about **0.9 ms of hash against a decode of tens of milliseconds**. That buys
too little for a `dp_api` version bump touching img, media and web, each
needing a key of its own whose failure mode is a *stale picture on screen* —
the same shape as an app's `_dirty` answering "clean" wrongly. The ticket
said "(2) is the better answer"; the number says otherwise, and it is
recorded there.

One check: two 96x96 pictures into 6 by 12 cells of 8x16, scaled one to one,
differing in exactly three bytes of 27,648 — a single changed pixel must
still be a different picture.

## 0.99.95

**A zoom or a center wallpaper is pixels now: it never was, on any terminal**
(Gitea #117). `cn_image` refused a rectangle that was not wholly on the
screen, and `dt_wallfit` produces exactly such a rectangle for two of the
four wallpaper modes **on purpose** — the picture is meant to overflow and be
cropped. So `img draw` fell back to half-block cells for both, and
`DT_IMGMODE` had no effect at all for anyone using either, which is most of
the point of the setting. Measured through a pty with `cellw=8 cellh=16` and
`HIBR_GFX=kitty`, against 0.99.94's own `image.c`:

| `DT_WALLMODE` | 0.99.94 | now |
|---|---|---|
| `stretch` | 1 placement | 1 placement |
| `zoom` | **0 placements**, the screen full of `▀` | 1, cropped to the screen |
| `center` (wider than the screen) | **0 placements** | 1, cropped to the screen |

**The screen's edges crop rather than refuse, and crop rather than squash.**
`cn_imgscalesrc` takes a source rectangle, so only the part of the picture
that shows is scaled into the visible cells — which is what zoom and center
mean. Squashing the whole picture into the visible rectangle was the other
thing that could have been done here and is not what those modes promise.

**A region is named by the rectangle its caller asked for, not by where the
picture ended up.** The cropped rectangle is what the cells are hashed
against; the asked-for one is what the identity check, the kitty-id reuse and
`cn_imgkeep` compare. Getting that wrong would have made a zoom wallpaper a
new region every frame — a fresh kitty id, a delete owed for the old one, and
all 6.3 MB of it hashed and encoded again, which is the cost 0.99.87 had just
removed. The test for it is that a cropped picture placed twice is one
placement and no delete.

**Cropping costs nothing.** A crop lands on a cell boundary for every picture
`img` hands over, because img has already scaled it to exactly `w*cw` by
`h*chh`, so the visible part is a sub-rectangle at the resolution it is
wanted at: a copy per row, not a box filter over every output pixel. The
first version of this went through the filter regardless and paid **81 ms**
for a screenful at 232x71 to resample a picture into itself; a cropped
placement now measures 169 ms against an unclipped one's 172, which is noise.

**A pane's own edge still refuses**, deliberately: that is a different
contract — a window's picture must not spill, and a caller computing a
rectangle larger than its own pane has a bug worth seeing as a fallback to
cells. Only the screen crops.

`dt_wallfit`'s own comment claimed `img draw` already clipped, which is the
belief the bug lived behind; it says what actually happens now. The note the
ticket asked for in the Wallpaper pane — "pixels work on stretch and scale
only" — is not needed and has not been added, because it is no longer true.

Eight checks: four in `tests/kitgfx.py` for the geometry of a picture hanging
off the left edge and off the bottom, and that a cropped one placed again is
the same region; four in `tests/desktop.py` for zoom and center as real
wallpapers, the cells they cover, and the window still being text over them.

## 0.99.94

**About says what the terminal can do, not only what the machine is**
(Gitea #121). Asked for as "can we put in the about box the terminal
capabilities?". About showed the machine — hostname, kernel, uptime, who is
logged in, CPU and memory — and nothing at all about what it was *drawing
on*, which is the other half of what decides what this desktop can do. One
line of the Control Panel's Pictures pane was the only place any of it was
visible. There is a **This Terminal** section below the machine's now:

| line | where it comes from |
|---|---|
| `This Terminal: xterm-256color (kitty)` | `$TERM`, and `$TERM_PROGRAM` where a terminal sets one |
| `Size: 71 x 232 cells, 1856 x 1136 pixels` | the desktop's own `DT_ROWS`/`DT_COLS`, times the cell `console gfx` reports |
| `Pictures: kitty, a cell is 8 by 16` | `console gfx` |
| `Colour: 24-bit sent; COLORTERM=truecolor` | what the console emits, and what the terminal claims |
| `Mouse: clicks and drags` | `console mouse`, new below |
| `Held: yes, 1 display attached` | `$HIBR_HOLD` and `hold clients` |

**Every line is something the desktop can ask rather than guess, and where
there is no answer it says so.** A terminal that has not said what a cell
measures gets `Size: 24 x 80 cells` and `Pictures: blocks only, no cell size
given` — never `0 x 0 pixels`. **Nothing on the Colour line says
"supports":** `cn_sgrcol` emits an RGB pen as `38;2;r;g;b` without ever
asking whether the terminal renders it, and there is no query for that, so
"sent" is the honest word and `COLORTERM` sits beside it as the terminal's
own claim — the two disagreeing being exactly what a person would want to
see. **Synchronized output and the kitty keyboard protocol are not listed at
all**, because both need a query and a reply (Gitea #92 and #93) and a
capabilities list with one invented row in it is worse than a short one.

**`console mouse` with no mode now answers instead of printing a usage
error**: `off`, `click`, `drag` or `motion`. The console already kept
`cn_mousemode`; what it reports is the mode it was *asked* for, which is all
anything can know, since a terminal never says whether it obeyed. Nothing in
the tree called the verb with no argument, and a bad mode still fails with
status 2.

Read when the window draws, not once: `console gfx` deliberately does not
cache a `none` that came from not yet knowing the cell size, so in a held
session — which every desktop is — the answer changes the moment a client
attaches, and the screen saver and standby turn mouse reporting off while
they run. The one exception is `Held`, which is a request to the session
over its own socket and so is taken at most every `AB_SLOWMS`, like the
other readings that cost something.

The window is **22 by 48** rather than 16 by 46 for the seven rows that
needs. At 80 by 24 — the smallest screen the suites run — it lands clear of
both the menu bar and the bottom edge, which was measured rather than
assumed; any taller and `dt_setsize` clamps it. Copy takes the new lines too.

Two helpers were nearly named `about_size` and `about_mouse`, which the
window manager calls by prefix: `dt_launch` asks an app for `<app>_size` to
decide how big a new window should be, so About would have been created with
`"22 x 48 cells, 1856 x 1136"` rows by `"pixels"` columns, and a window with
a `_mouse` never gets `_click`. They are `about_screen` and `about_pointer`,
with the near miss written down beside them. Caught by checking the new names
against the suffixes the window manager calls, before running anything.

Three checks: the section on an ordinary terminal, that no pixels are
invented when the cell size is unknown, and — inside a **real held
desktop**, which is the one fact no ordinary session can answer — that the
Held line counts the attached display. Nineteen new strings, Arabic
hand-translated.

## 0.99.93

**File and Edit are the first two menus in every window** (Gitea #120).
Asked for as "File/Edit as standard in every app… this is a clear pattern
right, I want this 100% consistent across all desktop apps". It was not: of
the twenty-three apps that declare menus, **ten** opened with a menu of
their own and had Edit shuffled to the end, and twenty-four more windows
declared no menus at all and so had **no File menu anywhere**. Every bar now
reads the same -- the hibr menu, **File**, **Edit**, the app's own menus,
**Window**, which is System 7's order.

One rule decided what moved: **File holds the document's lifecycle -- New,
Open, Save, Close -- and nothing else; everything particular to the app
stays in the app's own menu, which keeps its name.**

| app | File | its own menu, now after Edit |
|---|---|---|
| Bricks, Snake | New Game, Close | Game / Snake: Pause |
| Minesweeper, Puzzle | New Game, Close | *none left* -- their bar is File, Edit, Window |
| Stickies | New Note, Close | Note: Color ▸, Delete Note… |
| Calculator | Close | Calc: Evaluate, Clear, Backspace, Use Answer |
| Clipboard | Close | Clip: Use, Pinned, Delete, Clear History |
| Image Viewer | Close | Image: Set as Wallpaper |
| Task Manager | Close | Task: sorting, End Task, View Details |
| Notifications | Close | History: Clear Selected, Clear Low, Clear All |

Two costs, named rather than hidden: five apps have a File menu holding only
Close, which is what a System 7 desk accessory had; and Minesweeper and
Puzzle have no menu of their own left at all, because New Game and Close
were the whole of it, and an empty menu is worse than no menu.

**An app that declares no menus is given File > Close by the window manager**,
in one branch of `dt_menus` rather than a File added to each of twenty-four
files -- so an app written next year cannot forget. That covers four real
windows that had never had one (Clock, Control Panel, About This Computer,
Screenshot) and, deliberately, every dialog: Get Info, Rename, Open With,
Set Date & Time, Time Zone, the Mail account sheet and the rest. Close is a
true action on each of them, and a bar that changes shape between one window
and the next is one nobody can learn.

**The rule is enforced by reading the source, not by a session per app.**
`tests/540-examples.t` now fails any registered app whose first `dt_menu` is
not `"File"`, or which declares a second menu before calling `dt_editmenu`
-- checked against all twenty-three, and checked to *fail*: reverting one
app's file reports `calc_menus opens with Calc, not File`. Two pty checks go
with it, for what the window manager actually draws: that a menuless app
gets the File, Edit bar, and that an app's own menus come after both.

The desktop README, `ARCHITECTURE.md` and `CLAUDE.md` each said Edit came
last "unless the app calls `dt_editmenu`"; that is now simply what every app
does, and they say so.

## 0.99.92

**One About, and it belongs to whatever is in front** (Gitea #119). Reported
as "there are no two About hibr -- only one should be, and that's the about of
the foreground program". With an app focused the hibr menu showed *About
Files…* **and** *About hibr Desktop* beneath it; no menu anywhere works that
way. On a Mac the first item *becomes* the application's, and that is what it
does now:

| what is in front | the one item |
|---|---|
| an app | **About \<App\>…** -- its own, or the card the desktop makes from what it declares |
| nothing | **About This Computer…** |

**And the desktop's own is renamed to About This Computer**, which is what the
window has always been: it shows the machine's CPU, memory, uptime, hostname
and who is logged in. The window's title, the Control Panel pane that sets how
often those figures are read, and the message that pane shows when the app is
not installed all follow the same name. The Arabic catalogue has it as
"حول هذا الكمبيوتر", the wording macOS uses.

With an app in front, About This Computer is reached by clicking the desktop
first -- exactly as it was on the machine this borrows from. Said in
`examples/desktop/README.md` rather than left to be discovered.

**The About window is itself an app, and the one whose About *is* the
computer's.** Focused, it took the first branch like any other and the item
read *About **About This Computer**…* -- a template applied to a title that
already begins with the word. Found by opening the window and the menu bar in
a pty and reading the screen, not by reading the code; it takes the second
branch now, so the item names the machine once.

Ten checks across `tests/desktop.py` and `tests/apps.py` used *About hibr* as
a proxy for "the menu bar is open" -- or shut -- which it can no longer be,
since that item follows the front app; an eleventh read it in a pseudo-locale
to show the bar is translated item by item. All of them ask for *Screen Saver*
instead, which is in that menu whatever has focus. The app's old name is gone
from the code comments and the desktop README too; the one place it remains is
the alt text of `ARCHITECTURE.md`'s menu screenshot, which still shows it,
because the picture has not been retaken.

## 0.99.91

**A background job is reaped when its child exits, not when the next job is
started** (Gitea #123). Noticed by the owner in `ps`: seven zombies, every one
a child of their desktop, about one an hour over seven hours. Reproduced in two
lines -- `( : ) &` and a `sleep`, state `Z` -- and in three for the burst, which
is what said there was no second bug to go looking for:

    ( : ) & ( : ) & ( : ) &      before: Z Z Z        after: gone gone gone

The only thing that reaped was `ex_bg` on its way in, which collects what
finished *before* the next job starts -- so a burst leaves its own children
behind, and a desktop forks for a sync, a transfer or a vault command and
nothing else, hours apart.

The trigger has to be the child exiting. Nothing else is cheap enough: the
desktop runs hundreds of commands a frame, so a `waitpid` per command is the
1920-system-calls-a-frame mistake again, and a clock or a counter is arbitrary
and still per command. So `on_trap` sets a flag when the signal is `SIGCHLD` --
a handler may only set a flag -- `tr_init` installs that handler for **every**
shell rather than only an interactive one, and `ex_cmd`'s tail reaps when the
flag is set *and* this script has jobs.

A loop with neither pays one load. Measured per function rather than by the
total, because this tree has been caught by that before: `ex_cmd` goes
25,760,967 instructions to 26,080,979 over 40,000 commands -- **+8 each** --
and every other function in the build is byte-identical. The 0.72% the totals
differed by was `__strcmp_avx2` taking a different path for the same calls,
because string literals moved.

Three things that look like details and are not. The flag is cleared **before**
the wait, or a child exiting during it is not noticed until the one after.
`SA_RESTART`, so an ordinary `read` or `waitpid` resumes -- `select` and
`pselect` never restart whatever is asked, which costs a module's wait one
early return per child exit, and `cn_wait` already answers EINTR with "no key,
no resize", so a desktop simply goes round again. And **`CHLD` keeps its
handler whatever a script asks of the trap**: `trap - CHLD` setting `SIG_DFL`
would quietly bring the zombies back, and `SIG_IGN` on `SIGCHLD` would stop
`wait` working at all. A script's own `CHLD` trap still runs -- the flag is set
beside the trap's own, never instead of it -- and `tests/997-reap-bg.t` checks
that, along with `wait` still answering 7 for a job reaped before it was asked
about, and 4 for one still running.

## 0.99.90

**One rule for the keyboard: alt and ctrl-alt belong to the desktop, ctrl and a
letter belongs to whatever has focus** (Gitea #124). Asked for as "this is a
clear pattern right, I want this 100% consistent across all desktop apps", and
the defaults were not a pattern:

    snapleft  = ctrl-alt-left     snaptop    = alt-up
    snapright = ctrl-alt-right    snapbottom = alt-down
    wsnext    = alt-right         wsprev     = alt-left

Four tiling directions across two modifiers, the horizontal pair sharing its
modifier with the workspaces. What settles which modifier the desktop may own
is not taste but `dt_keyassigned`, which refuses to yield `ctrl` plus a letter
while `DT_TERMCTRL` is on -- the default: so **`ctrl-w` never closed a window
while a terminal had focus**, and `ctrl-s` never saved, and `ctrl-a` never
selected. Any scheme on ctrl is consistent everywhere except a terminal, which
is where this desktop is used.

| action | key | was |
|---|---|---|
| Quit Application | **alt-q** | nothing: there was no such action |
| Close Window | **alt-w** | `ctrl-w`, which a terminal kept |
| Snap left / right | **alt-left / alt-right** | `ctrl-alt-left / -right` |
| Snap top / bottom | alt-up / alt-down | unchanged |
| Next / Previous Workspace | **ctrl-alt-right / ctrl-alt-left** | `alt-right / alt-left` |
| Copy / Cut / Paste, Undo / Redo | alt-c / alt-x / alt-v, alt-z / alt-y | unchanged |

So `ctrl-c` still interrupts a program, which is what made the all-ctrl scheme
rejected before: a desktop that eats ctrl-c is a desktop nothing can run in.
`ctrl-s` and `ctrl-a` stay where they are, as the focused program's in a
terminal and the GUI convention elsewhere -- named here so it reads as a
decision rather than an oversight.

**This puts the arrows back where they were before 0.99.26**, which had swapped
them the other way. Its reason was that the arrows are the frequent action; its
cost, recorded in its own changelog, was the Browser keeping alt-left and
alt-right for Back and Forward. That cost is what was reported now, and the
fix is not to move the collision from the workspaces to tiling: **the Browser's
Back and Forward are alt-b and alt-f**. Not `alt-[` and `alt-]`, which this was
first going to use and which **cannot be bound at all** -- measured through a
pty, one key at a time: `ESC [` and `ESC ]` are the CSI and OSC introducers, so
the decoder consumes them and the key never arrives. `alt-b` and `alt-f` are
readline's own word-back and word-forward, which is where the letters come
from, and an app's key is its own only while it has focus, so a terminal keeps
them.

**Quit Application means quit the application**: every window of the focused
app, not only the one in front, each through `dt_closereq` -- so an app that
asks before closing still asks, once per window. Window > Quit Application sits
beside Close.

**Settings version 9 moves a saved file**, and only while each key still holds
the default it is leaving. Checked against five shapes: a 0.99.89 file, a
pre-0.99.26 file -- which goes through both swaps and lands where it began,
which is the point rather than a coincidence -- a chosen workspace key, a
chosen close key, and one that ends with two actions on the same chord.

**And `dt_keydups` says when two actions share a chord**, which was silent
before: `dt_keyed` asks about each in a fixed order, so the first fires and the
second simply never does. It goes to `desktop.log` -- "Close Window and Cycle
Windows are both on alt-w; Close Window wins" -- rather than choosing for
anyone, because which of the two should move is not the desktop's call.

Two things deliberately not done, both on the ticket. **Workspace up and down
stay unbound**: workspaces here are a list, not a grid, so stepping is covered,
and the useful thing those keys could do -- send this window to the next
workspace -- is a Window-menu item nobody asked for a key for. And **no test
fails an app for matching a chord the desktop holds**, because it cannot be
made honest: `apps/files.hibr`'s `ctrl-a` is correct, since `dt_event` runs the
focused app's own key before `selectall`, and a deliberate override looks
exactly like an accidental shadow in the source. A report belongs in
`tests/census.py`; a gate would cry wolf.

## 0.99.89

**`:= ` takes a command's result whatever kind of command it is** (Gitea #122,
ADR 0038). Reported from use: `curl` behaved the same under `/bin/sh`,
`/bin/bash` and hibr -- byte-identical bodies, same status -- and hibr's own
`/dev/http/host/port/path` scheme agreed with all three. The whole of the
difference was `:=`:

    v := str upper hello      v=[HELLO]  status=0     a builtin: works
    v := /bin/echo hello      v=[]       status=0     and "hello" went to the terminal
    v := /bin/false           v=[]       status=1     the status did come back
    v=$(/bin/echo hello)      v=[hello]  status=0     what bash would have you write

For a program there is no result slot in a child to fill, so the bind quietly
found nothing **and reported success** -- which is the worst of the three
possible answers, because nothing downstream can tell it from a command that
produced nothing on purpose. It had already cost this project twice: it is what
"the About window is writing outside its own box" turned out to be, and the
same bug made `[ "$AB_OS" = Linux ]` never match on Linux, so two apps ran
their macOS branch there for releases. It was written down as a trap, and the
owner hit it again from a cold start. **When a trap is rediscovered by someone
who has read the file, the behaviour is what wants changing, not the
paragraph.**

So `x := prog` now captures the program's standard output, trailing newlines
removed, exactly as `$( )` does. One rule for all three kinds of command.

What is deliberately unchanged, and is in the test as a guard rather than a
hope -- both written *before* the change and confirmed passing on the old code:

- A **pipeline stage** and a **background job** already run in a child whose
  binding dies with it, so the parent's variable is untouched. `nofork` is how
  the code tells, and `v := cmd | other` still leaves `v` as it was.
- A redirection the command carries of its own still wins: the capture's
  `dup2` happens before `rd_do`, so `x := cmd > file` writes the file and binds
  nothing, as `v=$(cmd > file)` does.
- A **function** still fills the slot only with `ret`. Capturing its output
  would mean forking for an in-process call, which is the cost the slot exists
  to avoid.
- The status still propagates, and a program that writes *and* fails binds what
  it wrote: `v := sh -c 'echo out; exit 3'` gives `out` and status 3.

It costs the loop nothing, because no loop reaches it -- the new code is inside
the program branch, behind `n->f == 2 && !nofork`. Measured anyway, since loop
throughput sits above this feature in the shell's own priorities: a
40,000-iteration `while` loop is 530.2M instructions against 533.1M before,
a difference inside what code layout alone produces. 151 KB of output captured
without deadlocking, because the pipe is read to the end before the wait.

**The lint rule that warned about this is now the wrong advice, so it is
replaced rather than removed.** `bind-program` said `:=` does nothing for a
program; that stopped being true. In its place `bind-in-a-stage` names the case
that is still wrong and can never be right -- a `:=` that is *directly* a
pipeline stage, where a stage is one command and nothing can read the binding.
Said of a stage only: `{ v := f; echo "$v"; } | cat` reads its own binding and
is correct, and the rule stays quiet. `tests/850-lint.t` carries the firing
case and both non-firing ones, and `docs/language.md` and `docs/llm.md` now
demonstrate the rule that exists.

Also: **the settled oracle in `tests/desktop.py` compared the clock**, and
failed this release's own gate on it -- its two snapshots are two or three
seconds apart, so a minute can turn between them and `15:36` meets `15:37`:
one glyph, no pen. It reproduces on demand by starting the run two seconds
before a minute boundary, which is how it was confirmed rather than guessed at,
and the control with the clock pinned is clean at the same boundary. `dragsteps`
was pinned in 0.99.87 for exactly this and the settled one was missed; both are
pinned now. **A one-session comparison is not immune to the clock** -- only to
the two sessions settling differently.

A census while deciding this: all 295 distinct `:=` command words in
`examples/desktop/`, classified against the builtin table with every module
loaded and against the desktop's own 1,715 functions, found **no** `:=` on a
program anywhere -- in the desktop or the rest of the tree. The two mentions
are comments warning about the old behaviour, left at the call sites where it
bit. So nothing in hibr's own code changes behaviour; what changes is that the
convention no longer has to be followed by hand.

## 0.99.88

**The oracle that CLAUDE.md said was needed before any of the frame-skipping
shipped now exists** (Gitea #115). `python3 tests/uifuzz.py oracle`: one
composed desktop, one seed, and after every random event the screen compared
against the same frame with `DT_FORCEDRAW` on -- glyphs **and** pens, because a
shadow is a colour and no glyph comparison can see one. It was owed from
0.99.83, and in the meantime every fault in that work was found by a person
looking at their own screen: a closed window left behind, shadows pulsing,
shadows flickering during a drag, a note's box surviving its own expiry
(#114), the wallpaper deleted by a quiet frame (#112), and a sticky note's
shadow present only on the frames it was skipped (#116).

The arrangement is the whole of it. A window that overlaps another is never
left alone, so an oracle whose windows all overlap exercises the old code path
and reports nothing -- which is exactly why three earlier oracles were silent
through #116. This one holds a bare sticky note and an ordinary window that
overlap nothing and can be skipped, another overlapping a terminal so it never
can, a picture wallpaper as real pixels, and the terminal quiet and unable to
echo so the screen settles and no stray key can reach a shell. Its event mix
deliberately breaks `uifuzz.py`'s own rule about staying inside a window:
menus, context menus, notes and workspace switches are overlays, and an
overlay that stops being drawn is one of the ways the invariant breaks.

**Four ways it could not fail, each of which reported a clean run**, and all
four are now in CLAUDE.md because the shape generalises:

- A note's own life ended *between* the two snapshots, so it was up in one and
  gone from the other -- and a workspace switch raises a note of its own, so
  special-casing the event that asks for one is not enough. `DT_NOTEMS` is
  short enough here that a note's whole life falls inside the settle.
- The force is a key, and a key only reaches the app that has focus: sent while
  the sticky note had it, `F` was typed into the note as text.
- Clicking the oracle window to give it focus *raised* it, a raise reorders the
  pane list, `dt_alone` sees a changed signature and sets `DT_FORCEONCE` -- so
  the frame right after any click is a full one already, and comparing that
  against a forced frame compares a frame with itself.
- **The control condition never held.** `git stash push <path>` stashes
  *uncommitted* changes, so once the fix under test was committed, three
  separate "with the fix reverted" runs were quietly running with it. Reverting
  is `git checkout <previous tag> -- <path>`, and the file wants reading
  afterwards rather than the command being trusted.

So the test carries two guards of its own: a mark written by the force key, so
an event whose comparison was vacuous fails loudly, and a mark written whenever
a window is left alone, so a run that skipped nothing fails too. Against
0.99.86's `frame.hibr` it fails on the **first** event, naming 34 cells and
showing the pen pair -- dim where the frame was left alone, plain where it was
redrawn -- and with the fix it is clean. 30 events is what the gate pays for,
about a minute at 1.9 s an event; `ON=100` pushes it harder and a failure
prints the line that replays it.

Nothing else changed: no C, no desktop code. The release is a test, three
documentation entries, and the open item in CLAUDE.md rewritten from "if it is
ever wanted, it needs an oracle first" to what the oracle is and what it
covers.

## 0.99.87

**A bare window never cast its shadow on a frame that drew it, so a sticky
note's blinked** (Gitea #116). This is the one that was reported, twice:
"stickies shadow on both in workspace one, on one of them in workspace 2 on
the other in workspace 3", then "the stickies shadow blink when I move any
window, only them?". `dt_win` has four shapes and the shadow was cast in the
wrong ones: the path for a window it leaves alone cast it, the path for an
ordinary window cast it, and the two that `return` in between -- a **bare**
window, which is a sticky note, and a `chrome = none` one, which is a mini
player -- did not. So a sticky had a shadow on every frame it was skipped and
none on every frame it was drawn, and it was the only thing anybody saw do it
because it is the only bare window with a `_dirty` of its own: move any window
and every sticky is dirty, so every sticky loses its shadow; let the screen
settle and they are skipped again, so it comes back. Before 0.99.83 nothing
was ever skipped and a bare window simply never had a shadow -- consistently,
which nobody reported. `dt_shadow` moves above the shape branches; it is
idempotent since 0.99.85, so casting it for every shape on every frame cannot
darken anything twice.

It was found by running a desktop with **the owner's own settings file, their
own wallpaper, their window geometry and their screen size**, and comparing
every step of a drag against the same drag with the skipping off: the sticky's
shadow strip came back as 101 differing *pens* and no glyphs, and an
instrumented `dt_shadow` then showed it called nineteen times in one run and
ten in the other, never once for either sticky in the second. Three oracles
written before that reported zero differences, because every one of them used
ordinary windows, which reach `dt_shadow` whichever path they take. An oracle
has to be driven with the **kinds** of window a fault needs, not only the
arrangement -- `tests/desktop.py` opens a bare window of its own now, compares
the frame that drew it against the frame that left it alone, and asserts the
shadow is really there rather than merely agreed about. With this fix reverted
it reports 38 differing pens.

**An overlay that goes left its cells on the screen** (Gitea #114). Reported as
"if something overwrites them like the notification panel, they are not redrawn
instantly unless I click on them". A note, an open menu, a dialog, a drag's
ghost, the standby dim and the screenshot chooser are all drawn at absolute
coordinates on top of the finished frame, none of them owns a pane, and nothing
repaints what they covered once they stop being drawn -- which did not matter
until a window nothing changed began to be left alone and the wallpaper to
paint nothing. A note's whole box survived its own expiry, 74 glyphs and 78
pens, until the next key. The frame after any frame that drew over the screen
is a full one now: rather than have each of the eight declare itself, the
console counts writes made at absolute coordinates (`console drawn`, the
sibling of `console consumed`) and `dt_draw` reads it either side of the one
part of a frame where only such things draw, so an overlay added there later
cannot forget. Two costs of counting rather than declaring, both named in the
code: every frame is full while an overlay is *up*, not only the one after it
goes -- which for a blanked display, whose state lasts, means no cheap frame at
all -- and the count is of this builtin's own calls, so an overlay built out of
a module's drawing would have to say so itself. `dt_saverstop` and
`dt_saverstart` set the same flag directly, since they clear every cell from
outside a frame.

**And a wallpaper of real pixels was taken off the screen by the first frame
that painted nothing** (Gitea #112). A picture drawn under the text owns no
cells, so what keeps it is its caller placing it again: the frame that stops
placing it is the frame the console deletes it from the terminal, and the
gate's own one-second ceiling made it blink rather than simply vanish. A gated
frame says the picture is still wanted now, through `dp_api` version 6's
`imgkeep` and a new `img keep` -- never `img draw`, because draw falls back to
half-block **cells** whenever pixels cannot be placed, and those are cells
written at absolute coordinates outside `console behind on`, which on a frame
that meant to paint nothing would land on top of every window and stay there.
`img keep` draws nothing whatever happens and fails when there was nothing to
keep. The img module recognises its own last placement by path, pixel size,
rectangle and mtime, so it needs neither the scaled copy nor the display's
hash of every pixel -- 6.33 MB and 19.6 ms for a screenful, which was being
paid on every frame that painted a picture wallpaper: a frame at 232x71 with
two overlapping windows and a 3840x2160 photograph went **23.8 ms to 2.2**.

Found while measuring that: **a zoom or center wallpaper has never been pixels
at all** (Gitea #117). Both fit a rectangle that is deliberately larger than
the screen, the display refuses one that is not wholly on it, and `img draw`
quietly draws half blocks instead -- so `DT_IMGMODE` has never done anything
for either mode, and `dt_wallfit`'s own comment says the opposite. Not fixed
here; it needs the console to scale a bitmap to the visible part of a
rectangle, which is what those modes mean.

Also from a reading of the diff rather than a test: Gitea #118, two more
instances of the same invariant -- a second `console flush` in one frame drops
the under-text picture, and `dt_saverstart` repeats its clear for as long as
the desktop is idle with no saver installed.

The per-step drag oracle compared two sessions and the clock in the bar, so it
failed a release gate on `12:38` meeting `12:39`: one glyph, no pen, in every
step at once. Those sessions run eight seconds apart, so one of them crosses a
minute. Its bar format is pinned to a constant now.

## 0.99.86

**A desktop running an old image says so, for as long as it is.** This is the
fix for the worst thing that happened today, which was not a drawing bug.
`dt_updcheck` noticed a new hibr on disk and sent one notification -- which
times out in a couple of seconds -- then set a flag that silenced it for
good. The owner's own desktop therefore ran for eight hours on 0.99.83:
through 0.99.84, which fixed a segfault it had already died of, and 0.99.85,
which fixed what it was drawing. Every bug reported in those hours was
reported against code that had been replaced on disk, and diagnosed against
code that was not running. A notification is an announcement; what was needed
was a *state*. `DT_UPNEW` is set while `/proc/$$/exe` is not `$HIBR` and
`dt_bar` shows `GL[reload]` beside the bell until a restart clears it -- which
a restart does by construction, being a new process. The notification stays,
and the recheck is cheap once the answer is known.

**And the oracle now looks at every frame of a drag, and knows what settled
means.** Two faults in the test rather than the code, both of which let a
real regression through:

A comparison of the *end* state cannot see a flicker. The oracle drove a drag
and compared once it had finished, so a shadow that blinked while the window
moved was invisible to it by construction -- and one shipped. There is a
second oracle now: the same drag driven twice, with the skipping on and off,
compared **step by step**, glyphs and pens.

And a snapshot has to be taken when the desktop has stopped drawing, not
after a pause. `Term.keys` waits for the idle marker of the bytes it sent,
but a frame a *timer* asked for -- the wallpaper's once-a-second ceiling, the
icons' five-second rescan -- lands after that marker, so the two screenshots
were of two different moments in the desktop's life. That check failed about
one run in three inside the suite and **not once in eight runs on its own**,
which is what a load-dependent comparison looks like rather than a
load-dependent bug. Both oracles now wait for the frame count to stop
advancing before they look.

## 0.99.85

**A shadow is idempotent, so nothing has to decide which frame may cast
one.** `console darken` is multiplicative: the same cells darkened twice come
out twice as dark. Since 0.99.82 the answer to that was to work out which
frame was *allowed* to cast a shadow -- a rule that has to stay in step with
every rule about what gets repainted -- and it was wrong three times running:
the shadows under the bar and an open menu pulsing once a second, the Control
Strip blinking, and then, reported from a live session on 0.99.84, a sticky
note's shadow flickering while a window moved, because a window redrawing
near another wipes a shadow that is then not cast again until the wallpaper
next paints.

`console darken -s` marks each cell it shadows with an attribute bit of the
console's own (`CN_SHADOWED`, above every `DP_` flag and never emitted -- the
SGR builder tests each flag it knows by name) and leaves an already-shadowed
cell alone. Any ordinary write clears the mark, because `cn_put` and
`cn_fill` assign the pen's attributes, so the wallpaper painting underneath
or a window moving away gets its shadow cast afresh with nothing tracking it.
All four shadows -- a window's, the bar's, an open menu's, the Control
Strip's -- are now cast on every frame, and the conditions that tried to
schedule them are gone, along with the "do not gate the wallpaper while a
menu is open" workaround that existed only to serve them.

Three checks in `tests/console.py` pin it: cast three times is cast once,
**without** `-s` it is not, and a cell written again gets its shadow back.
The second of those is why the whole family of bugs survived so long in
testing: darkening a *palette* colour lands on another palette entry and is
already idempotent, so the same check written in white-on-blue passes against
the broken code. It only bites in RGB, which is what every theme uses.

**And the loop was measured rather than inferred.** `dt_run`'s own passes
were instrumented -- each section timed into an accumulator, written out as
it went, since a harness kills a session rather than quitting it -- against
three programs each writing every 10 ms:

| | |
|---|---|
| `dt_draw` | 2090 ms over 251 frames, **8296 us each** |
| `dt_drain` | 34 ms over 249 held passes, 138 us each |
| the five periodic calls, the idle marker, the resize check, `dt_event` | about 250 us a pass between them |

So the loop is 6% of the cost and the frame is 96%, which retires two pieces
of planned work -- a `term pollall` to collapse the drain's module calls, and
a hunt through the loop body -- that would between them have optimised the
6%. The frame costing 8.3 ms here against 1.57 ms when called in a tight loop
is the real remaining question, and the same probe says why nothing is being
skipped to pay for it: **1005 windows drawn against 250 left alone**, because
three programs writing constantly make every terminal genuinely dirty on
every frame.

## 0.99.84

**A resize killed a desktop with a signal 11, and three shadows pulsed.**
All four are 0.99.82's, reported from a live session within a day of it.

**The crash.** The map that says which cells a pane covers -- what lets the
wallpaper paint around the windows -- is allocated for the grid as it was
when it was built, and `cn_skip` bounds-checked the *current* grid before
indexing it. `cn_fitq` acts on a SIGWINCH in the middle of a frame, so a
terminal resized larger left the map smaller than the grid and the index ran
past the end of its own allocation: an out-of-bounds read, and the desktop
died. The supervisor restarted it and put the windows back from the
snapshot, but a terminal's program is a child of the process that died and
nothing could adopt it -- so what the owner saw was every window in place
and every shell gone. The map carries its own rows and columns now and
checks against those; a cell it does not cover is one the screen has only
just grown into, which no pane owns, so it is painted rather than skipped.
Found by reading rather than by a test: the window is a signal arriving
between two statements, which no suite can be made to hit on purpose.

**The shadows.** `console darken` is cumulative, and 0.99.82 paired only the
*windows'* shadows to "cast only onto wallpaper this frame painted". The
bar's, an open menu's and the Control Strip's were still cast on every
frame, so each grew a shade darker until the once-a-second wallpaper repaint
put it back: a shadow visibly pulsing, once a second. All four are paired
now.

**The Control Strip.** It is drawn on the screen itself rather than through
a pane, so the wallpaper paints over it -- and its own gate let it skip
exactly that frame, leaving it missing until the next frame it chose to
draw. "It disappears every second or so" was that. It now always draws on a
frame that painted the wallpaper.

And the wallpaper is not gated at all while a menu is open: a drop-down is
drawn on every frame it stays open and casts a shadow on whatever is under
it, window or wallpaper, so the cells beneath it have to be painted that
frame. Painting the wallpaper for as long as a menu is up costs 1.4 ms a
frame during an interactive moment, against a shadow that fades the instant
the window under it redraws.

## 0.99.83

**A program writing does not wake the desktop for a frame it has already
decided to hold.** `DT_TERMMS` holds an output-driven frame a while, and
until now every byte a program wrote still woke the desktop to be told so:
three programs writing a hundred times a second woke it three hundred times
a second, and each wake drained a pty and went back to sleep. The pty's own
buffer is where those bytes belong until there is a frame to put them in.

`cn_wait` leaves the watched descriptors out of its `pselect` when asked
(`console key -q`), so a wait that is only running down the clock ends on the
clock -- or at once on a key, a click or a resize, because the terminal's own
descriptor stays in. `dt_run` uses it for the remainder of a held frame,
having drained first. The desktop's own share of three programs each writing
every 10 ms: **16.6% of a core, now 9.9%**.

**What that did not buy, said plainly.** The point of holding frames was to
be able to *raise* the rate once wakes were cheap -- to pair the desktop to a
display's own refresh rather than to a cost. It is not there yet:

| `DT_TERMMS` | frames a second | CPU |
|---|---|---|
| 16 | 60.5 | 39.5% |
| 33 | 29.8 | 23.9% |
| 70 | 14.2 | 13.2% |
| 140 | 7.1 | 8.1% |

About 6 ms a frame whatever the rate -- of which `dt_draw` is **1.57 ms**.
The rest is `dt_run`'s own loop, twice a frame at about 2.2 ms a pass, and
stubbing every periodic thing it calls (`dt_updcheck`, `dt_remotepoll`,
`dt_snapcheck`, `dt_tickers`, `dt_saverwait`) accounts for only 0.6 ms of
that. So the loop, not the drawing, is the ceiling now, and finding the rest
means timing its sections rather than guessing at them. 60 Hz is affordable
when a pass costs what a frame costs.

Two measurement traps met on the way, both worth more than the numbers.
Emptying `DT_TESTIDLE` to measure a desktop without the harness's idle
marker also stops `dt_supervise` returning early, so a supervisor starts and
the pid the harness forked is no longer the one drawing -- it measured 0.0%
of a core, which is what measuring the wrong process looks like. And once the
whole tree was measured instead, the marker turned out to cost nothing at
all (14.0% against 13.9%): the suspicion was wrong and the measurement said
so.

## 0.99.82

**The desktop draws only what has changed.** A frame on the owner's own
shape -- 232x71, three terminals, two sticky notes, a 3840x2160 wallpaper --
timed by calling `dt_draw` fifty times in a loop:

| | ms a frame |
|---|---|
| 0.99.80 | 7.0 |
| 0.99.81 | 5.8 |
| **0.99.82** | **1.05** |

and what that is worth where it shows: typing eight keys a second costs 3.0%
of a core against 8.5%, the same typing with pictures as real pixels 3.6%
against 93.3%, and an idle desktop 0.2% either way.

**What made it possible, and it is not what it looks like.** A pane is a
name and a rectangle -- it has no cells of its own -- and `cn_flush` copies
each changed cell into the front grid and leaves the back grid alone. So a
cell nobody rewrites keeps last frame's value and flushes as nothing: the
screen already remembers. The one thing stopping a window being left alone
was `dt_wall` painting over it every frame.

So the console learned to **paint behind the panes**. It keeps one byte a
cell saying whether a pane covers it, and `console behind on` makes writes
at absolute coordinates skip those cells. `dt_wall` paints around the
windows now, and because the img module draws through the same `dp->put`, a
picture wallpaper skips them too with no change to img at all. Three things
fall out of that, each of which the alternatives needed machinery for: a note
or a menu that has gone had its cells outside the windows, so the wallpaper
paints them back without anything tracking it; a window that moved leaves
cells that are outside every pane, likewise; and nothing has to be told a
window closed -- though the map does have to be thrown away when a pane is
dropped, which the first version forgot, leaving closed windows on screen.

**A window is left alone when its app says nothing changed.** `<app>_dirty`
is opt-in: an app without one is drawn every frame exactly as before, so a
missed case is impossible rather than unlikely. `term_dirty` reads a count
the emulator keeps of what its program has been fed (`term gen`), with the
size, the focus, the scroll position and whether the program is still there;
`stickies_dirty` a count its own keys and mouse bump. And `dt_alone` decides
which windows may be left alone at all: one whose rectangle *and the strip
its shadow falls on* touch no other window's. Two that overlap are always
both redrawn.

**Shadows, which is where this would have gone quietly wrong.** `console
darken` is cumulative: the same cells darkened twice come out twice as dark.
So a shadow may only be cast onto cells painted this frame -- `dt_wall` says
whether it painted and `dt_win` obeys -- and where two windows overlap, one's
shadow falls on the other and the only arrangement in which every shadow is
sure of fresh cells beneath it is the one where the wallpaper paints as well.
Overlapping windows therefore cost the wallpaper's own saving. That is a real
cost, stated rather than hidden; a layout with nothing overlapping keeps it.

**The wallpaper and the Control Strip draw when something changed, and at
least once a second.** The wallpaper's signature holds everything it paints
and everything that is drawn over it and can go away again -- the menus, a
drag, the notes, the identify box, standby, a confirm, a screenshot, the
saver, the lock, the workspace, the displays. The ceiling is the honest part:
anything the signature does not know about cannot be seen for longer than a
second.

**Three controls that would have gone on claiming to work.** Each of these
has its own `dt_want` or its own timer *inside* the drawing that is now
sometimes skipped, and each would have failed silently:

- Cursor Blink asks for its next frame from inside `term_draw`, so a focused
  terminal stays dirty while the setting is on. With it off -- the default --
  a terminal nobody has typed into is not drawn at all. Its own comment used
  to say blinking was free because a focused terminal redrew every 30 ms,
  which stopped being true when that was fixed and is now plainly false.
- The desktop icons' rescan sat inside `dt_wall`; gating the wallpaper would
  have stopped a mounted disk ever being noticed again. It is outside now.
- The bar's clock asks for its next tick from inside `dt_bar`. Gating the bar
  would leave it telling the right time once and then lying, so the bar still
  draws every frame and keeps its 0.19 ms. That is the trade, said plainly.

**And the oracle that guards all of it** (`DT_FORCEDRAW`, deliberately not a
setting anybody keeps). Inside one session, with the screen settled, turning
the skipping off must not change a single cell -- the pens as well as the
glyphs, since a shadow cast twice differs only in colour. Two overlapping
windows, and a drag that moves one of them first, because a move is what
leaves cells where a pane used to be. It found the double-darkened shadow
above. The first version of it compared two separate sessions and failed one
run in three with nothing wrong: two correct desktops settle at their own
pace, so the last frame before a quitting key is not the same frame. One
session, two frames, is the only honest form.

Also: `cn_fill` and the shadow strip of 0.99.81 are in this too, and
`tests/desktop.py`'s own scratch files are named after the process that
writes them, which the gate caught the hard way.

## 0.99.81

**A frame is 17% cheaper, and the measurement that found it is the news.**
Timing a whole desktop under load has about a millisecond of run-to-run
noise, which is the same size as the savings worth having -- so both of
these changes measured as *nothing* until the frame itself was timed
directly: `dt_draw` called fifty times in a loop with the clock read either
side, on their own session's shape (232x71, three terminals, two stickies,
a 3840x2160 wallpaper). 7.0 ms a frame before, 5.8 after, with the two runs
of each within 150 us. Anything measuring a desktop by sampling CPU wants
that instrument first.

**`cn_fill` decodes its glyph once rather than once a cell.** Every cell of
a fill went through `cn_put`, paying a `strlen`, a `u8dec` and a `u8w` to
learn again what the cell before it had already said. A wallpaper is 16,472
cells and every window's own face is another fill:

| | before | after |
|---|---|---|
| fill 71x232 with a space | 626 us | 199 us |
| fill 71x232 with a wallpaper glyph | 742 us | 204 us |

The fast path takes only a single codepoint one column wide and writes
exactly the cell `cn_put` would have, `cn_split` and the freed combining
characters included; a wide glyph or a string still goes the old way.

**A window's shadow darkens the strip that shows, not the whole window.**
The shadow is the window's own rectangle moved a row down and two columns
right, and the window's own face is drawn over it a few lines later -- so
`h` by `w` cells were darkened to leave an L visible: 4,648 of them for one
terminal against 193. It is now two calls, the row under the window and the
two columns beside it, which is the same cells and measured the shadow down
to nothing at all (6.047 ms a frame against 6.017 with shadows off).

Where the 6.0 ms that remains goes, from stubbing one phase at a time:

| | ms |
|---|---|
| the five windows' own `_draw`s | 1.89 (3 terminals 0.96, 2 stickies 0.93) |
| the window chrome around them | 1.50 |
| the wallpaper | 1.45 |
| the Control Strip | 0.49 |
| the menu gate and the bar | 0.58 |
| shadows | about nothing |

Two things that says, for whoever goes further. A terminal's content costs
0.32 ms because `term draw` blits its cells in one call, where a sticky note
costs 0.47 for a tenth of the area, because `tb_draw` walks it row by row in
shell -- the text widgets, not the terminals, are what is expensive now. And
the rest needs the desktop to stop redrawing what has not changed, which is
possible without touching the console (`cn_flush` leaves the back grid
intact, so a cell nobody rewrites costs nothing) but is a real piece of work:
a window kept from last frame means the wallpaper must not be painted over
it, and painting the wallpaper around the windows means the console has to
know which cells a pane owns.

## 0.99.80

**A desktop with busy terminals in it was spending a core on frames nobody
asked for, and the cap it got in 0.99.79 was set too high.** The owner's own
session sat at 33-48% of a core doing nothing: three terminals, two of them
running an AI assistant whose title carries a spinner. Sampling its run state
3000 times found it *running* in 52% of them and otherwise asleep in
`poll_schedule_timeout` -- a timed wait, so it was not blocked on anything;
it was drawing.

Rebuilt exactly -- their screen (71x232), their wallpaper, their calendar,
three terminals each writing every 10 ms -- and measured:

| | frames/s | CPU |
|---|---|---|
| 0.99.78 | 105 | 76.5% |
| 0.99.79, `DT_TERMMS=33` | 29.9 | 28.7% |
| 0.99.80, `DT_TERMMS=70` | 14.1 | 21.1% |
| `DT_TERMMS=200` | 4.9 | 13.4% |

0.99.78 redrew the whole desktop once per burst of a program's output, with
nothing holding it back: **105 frames a second**. 0.99.79 held an
output-driven frame to 33 ms from the last, which is thirty a second -- more
than any text program needs, and twice the cost of fifteen. The default is
70 ms now (`DT_SETVER` 8 moves a file that still holds 33; a value somebody
chose stays), and Control Panel > Desktop > Frames For Output still goes to
200 for whoever wants it lower.

**Pictures as real pixels cost twenty times what they should** (Gitea #108),
reported as "I switched to pixel and look at the load". The cell path in
`img draw` has had a cache for releases; the pixel path had **none**, so it
decoded and resampled the whole picture on every call -- and the wallpaper
calls it every frame, because a region drawn under the text is kept only
while its caller keeps placing it (ADR 0037). Five consecutive draws of a
3840x2160 photograph onto a 232x71 screen:

| | before | after |
|---|---|---|
| first draw | 415 ms | 454 ms |
| and again | 328-362 ms each | **10-22 ms** |

The console's own side was always right: it transmits the picture once and
sends nothing again (the flush measured 0 ms after the first). All of the
cost was `img draw` redoing work for a picture that had not changed. The
whole desktop, typing eight keys a second:

| | frames/s | CPU |
|---|---|---|
| Pixels, before | 2.7 | **93.3%** |
| Pixels, after | 7.5 | 9.5% |
| Half blocks | 7.5 | 8.7% |

So real pixels now cost what blocks do. It is kept as one entry rather than
`IM_CACHEN` of them because the sizes are not comparable: that screen's
pixels are 1856x1136x3, **6.3 MB**, where its cell grid is 130 kB -- four
slots would cost more resident memory than the whole shell. That 6.3 MB is
the price, and it is paid only while something is drawn as pixels.

**The hibr menu's app list is worked out once** (Gitea #109). `dt_draw`
rebuilds every menu whenever anything is typed, and `dt_appmenu` walked the
registry, lowercased every title, sorted them and worked out an accelerator
letter for each of twenty-six apps -- 2.9 ms of every keystroke, to produce
the identical list again, since the registry does not change while a desktop
runs. `dt_appplan` computes it once into `DT_APPPLAN` and `dt_appmenu`
replays it; `dt_app` clears it. 2.9 ms becomes 1.4, and a keystroke's frame
11.2 ms becomes 10.0.

Where the rest of a keystroke goes, measured by stubbing one phase of
`dt_draw` at a time -- a function defined again replaces the first, so a
session file can do this without touching the tree:

| phase | ms of a keystroke frame |
|---|---|
| `dt_menus` (`dt_ctxbuild` is free) | 4.8 |
| every window's `_draw` | 3.1 |
| `dt_wall` | 1.6 |
| `dt_bar` | 0.6 |
| `dt_idle` | 0.6 |
| `dt_stripdraw` | 0.3 |

and the frame is about 7 ms of *fixed* work whatever the screen measures --
an 80x24 desktop with a window costs the same 7.0 ms as a bare 232x71 one --
so this is per-frame script work, not per-cell drawing. The prize still on
the table is not rebuilding the menus for input that cannot have changed
one, worth about 3.5 ms of every keystroke; it needs the contract already
written down (anything changing a shut menu bar clears `DT_MENUIN`) to be
tightened and every app's `_menus` audited against it, which is #109 and a
job of its own rather than a line.

Two things that fall out of the measurement and are worth writing down.
**The frame is the whole cost**: with the same three programs writing and
every window minimised -- so the output is still drained but nothing is
drawn -- the desktop costs **0.1%** of a core. Draining is free; drawing is
not. And **a frame gets dearer as the cap gets longer** (11 ms at thirty a
second, 27 ms at five), because damage accumulates between frames, which is
why the table flattens rather than falling to nothing. Past 100 ms there is
little left to win; what is left is making a frame cheaper, and the bar is
still 2.9 ms of it.

What this was *not*, each ruled out by measuring rather than by reading --
recorded because every one of them looked plausible:

| candidate | what the measurement said |
|---|---|
| its terminals' output volume | the children wrote 0.003-0.05 MB/s against the 2.4 MB/s it read |
| `hold` writing to its stdin | the hold server wrote 0.005 MB/s |
| the 2.28 MB wallpaper decoded per frame | `img draw` warm is 0 kB and 1.2 ms, on 0.99.78 as well |
| the Hijri calendar, asked per frame | `hcal info` plus `hcal format` is 47 B and 0.1 reads a call |
| `/proc` polling | the desktop has no per-frame `/proc` read at all |
| the control socket | listening cost 9.0% against 9.5% with none |
| an error loop under `keepgoing` | `desktop.log` was 2 kB and not growing |

## 0.99.79

**The wallpaper is a picture** (Gitea #104). It never had been, on any
terminal: a region that owns its cells is dropped the moment they change,
and the wallpaper is placed first and then drawn over by the bar, every
window and the icons -- so it was dropped before the first flush ever sent
it, and every desktop fell back to half blocks however much the terminal
could do. `img draw -u` is the path 0.99.78 prepared: the picture owns no
cells, goes out before the text of each frame, and is kept for as long as
the desktop keeps placing it.

`dt_wall` also blanks the cells it is about to cover, with no colours of
their own. Both halves of that matter: a bitmap writes no cells, so last
frame's text would stay on top of it, and a cell with a background colour
paints over a picture the terminal is compositing below the glyphs -- the
wallpaper-glyph fill that used to stand in for a picture would have hidden
the picture itself.

What it costs is the protocol's rather than a choice, measured on a 70x24
screen with 8x16 cells while dragging a window across the wallpaper:

| | sent | on a drag |
|---|---|---|
| kitty | one placement, 860 kB | nothing |
| sixel | one bitmap, 24 kB | 24 kB again |

kitty composites the picture below the text, so it is transmitted once and
nothing above it disturbs it. A sixel is paint: a cell written over it has
destroyed that much, so the bitmap goes again whenever anything above it
moves. Both work; only one is free.

Checked at three levels: `tests/kitgfx.py` and `tests/sixel.py` for what the
flag does to a region (sent once against painted again, and the frame that
stops placing it taking it away), and `tests/desktop.py` for the thing
itself -- one picture at the top left of a real desktop, the bar and a
window still text over it, and nothing sent as a bitmap when the setting
asks for blocks.

**`read` and `mapfile` read a block at a time** (Gitea #105). A shell `read`
must not consume a byte the next command wants, so it read **one byte a
syscall**: `while read -r l; do :; done` over a 950 kB file made 948,892
read calls and spent 0.30 s of its 0.50 s in the kernel. A descriptor that
can be seeked can be put back, which is how `src/main.c` has always read a
script -- a block, the newline, `lseek` back -- and `bi_rddelim` is now that,
shared by both builtins:

| | before | after |
|---|---|---|
| `while read` over 950 kB | 948,892 reads, 0.502 s | 20,002 reads, 0.066 s |
| `mapfile -t` over the same | 948,892 reads | 234 reads, 0.021 s |
| an idle desktop | 515 reads a second | 22 |

A pipe, a socket and a terminal are still read a byte at a time, because
nothing can be put back on them, and `read -n` still stops exactly where it
should on either. The desktop's own mount scan for disk icons read
`/proc/mounts` that way on every scan, which on a machine with a long mount
table was thousands of syscalls each time.

**`img size` reads the header instead of decoding the picture** (Gitea
#105). It called the full decoder to answer with two integers: 206 ms for
the 3840x2160 photograph on this machine's own desktop, which `dt_wallfit`
asks for on every change of screen size. It is 0.01 ms now -- a PNG says
its size in the IHDR after the signature, a JPEG in whichever SOF marker
opens its frame -- and a JPEG's own EXIF orientation is applied to the
answer, so `img size` and `img draw` agree about a photograph from a phone
rather than disagreeing by a quarter turn. A header this does not
understand still decodes.

And the resampled grids are evicted least recently **used** rather than
round-robin: four other pictures drawn in a frame could between them throw
out a wallpaper redrawn on every one of them, which then paid the whole
decode again. A use stamp costs an integer a slot.

**A frame costs a third less, and the clock is not recomputed on every one
of them.** Measured at 220x62 with three terminal windows, a warm frame was
8.76 ms and is 6.21 ms. Nearly all of the difference is `dt_clocktext`:
the bar asks it for the time on every frame, and a format with the Hijri
codes in it (ADR 0034) answered through three module calls -- `need hcal`,
`hcal info`, `hcal format` -- for 2.38 ms, to say what changes once a day.
It keeps its last answer and what that answer depended on (the second, the
format, the calendar, the adjustment, the language, the digits), so the
most it can be worked out is once a second: 0.02 ms, and `dt_bar` went
from 5.30 ms to 2.79.

What is left of a frame, for whoever looks next: `dt_bar` 2.9 ms, the
wallpaper 1.4, every window 0.7, the flush 0.2. The bar is one row and it
is the most expensive thing in the frame because it is composed and drawn
again every time; the Control Strip, which draws into a pane of its own and
is only rebuilt when it changes, costs 0.01 ms. A bar in a pane is the next
real saving.

**A frame nothing a person did asked for is held to `DT_TERMMS`** (33 ms,
thirty a second; 0 turns it off, Control Panel > Desktop > Frames For
Output). A frame redraws every window, the bar and the wallpaper, and a
program writing in small bursts -- a spinner in a title, a session
streaming its output -- asks for one per burst: a real desktop with three
such programs was drawing tens of frames a second and spending half a core
on it. A key or a mouse report is never held back, so nothing a person does
feels slower, and a held frame still *drains* every window's own program
(`dt_drain`), because a watched descriptor nobody reads stays ready and
skipping the read would turn a held frame into a spin. Honestly: the gain
is bounded by construction and the desktop here could not be made to draw
faster than 12 frames a second to show it off, so this is a limit rather
than a measured saving.

**hold sends a client only the pictures that changed.** It re-placed every
picture it was holding whenever any one of them changed, so a viewer's own
picture moving re-sent an 860 kB wallpaper with it -- which is what made
pixels unusable over a slow link, reported from a remote kitty. Each client
now carries the id and a hash of the bytes last sent for each picture: one
whose bytes have not changed is not sent again, and an id the session no
longer has gets a delete of its own rather than everything being deleted
and placed again. Measured on a wallpaper plus a small picture changed
twice: 3 placements where it used to be 6, and no delete at all, since a
picture replaced in the same rectangle keeps its id and a transmission with
that id replaces it.

**The harness leaked the desktop it was run from, and shared its scratch
files** (Gitea #106). `tests/screen.py` strips `HIBR_HOLD` so a suite run
inside a held desktop does not attach to it, and it strips `DT_SUPERVISED`,
`DT_RESTORE` and `DT_T0` now for the same reason: a suite run from a
terminal window *inside* a running desktop started every test desktop with
`DT_SUPERVISED` already set, so each believed it was already supervised,
none started a supervisor, and the checks that kill a desktop and wait for
it to come back failed -- on the released tag too, hours after that tag's
own gate had passed them, which is what an environment leak looks like:
same code, same box, different shell. Four checks across `desktop`, `vault`
and `calapp` were that one variable.

And every scratch file a suite writes is named after its own process, by
`screen.scratch`. all.py and asan.py run side by side in the gate, so two
copies of a suite are live at once, and `tests/desktop.py` still had
thirteen fixed `/tmp` names and a marker file: this release's own gate
caught one, where a run unlinked `/tmp/hibr-desktop-mshadow.hibr` as the
other was about to start a desktop on it, and the suite died at a check
about a menu's shadow with the other copy's clean-up in the traceback.
`run()` in the same file had used the pid since the beginning; the helpers
written beside it had not.

## 0.99.78

**A picture that goes is taken off the screen, in a held desktop too.**
Reported live on kitty against 0.99.77: moving a window left its picture
behind, so a desktop ended up with pictures of windows that were no longer
there, stacked where each had been. A kitty picture is an object the
terminal keeps until it is deleted, and the console does delete it -- but
hold's clients never saw that escape, because hold draws them cells from
its own emulator and the emulator consumed it. hold now deletes what it is
holding and places what remains whenever the set of pictures changes: one
escape, no per-id bookkeeping, and a transmission with an id already taken
would have replaced it anyway. A sixel needs none of this -- the text drawn
over it is what removes it.

**A picture drawn under text is kept while its caller keeps placing it.**
`DP_IMG_UNDER` used to mean "forgotten once sent", which made a wallpaper
cost its whole bitmap on every frame. Such a region now lives as long as
the caller places it again each frame and goes, with a delete, on the first
frame that does not -- and what it costs between those depends on the
protocol, not on a choice: a kitty picture sits below the text (`z=-1`) and
the terminal composites it, so nothing is re-sent, while a sixel is paint
and goes again whenever a cell above it has been written. `DP_API_VER` is 6
for the changed contract. Nothing passes the flag yet; the wallpaper
(Gitea #104) is what will.

`tests/holdpix.py` has a check for the reported bug -- a picture that moves
leaves nothing where it was -- and its "text through a picture" check now
asserts the invariant that holds whichever way hold's own timing falls:
the text is there and no picture is left behind. Which of the two a client
sees is not ours to decide, since hold renders snapshots of its emulator
rather than the program's byte stream, and under the sanitizers two flushes
a fraction of a second apart arrive in one read.

## 0.99.77

**Pictures reach a held desktop, which is every desktop** (Gitea #103). The
kitty protocol shipped in 0.99.76 and still nothing in the desktop drew a
pixel, because `dt_autohold` holds every desktop and nothing about hold
carried a picture. Measured by driving a held program through a pty, not
reasoned about, and both halves were hold's:

- `hold` starts the program on a pty of its own and nobody had told that
  pty what a cell measures, so `console gfx` answered `none 0 0` inside
  every held program and every picture fell back to half blocks. A client
  now reports its own `ws_xpixel`/`ws_ypixel` alongside its size, hold
  takes the primary client's (a bitmap is 1:1; there is no second size to
  draw it at) and sets it on the program's pty -- `pty resize id rows cols
  xpixel ypixel`, `PY_API_VER` 3. The message goes beside the old two-int
  one rather than replacing it, so a client from this version can still
  resize a session still running the last one.
- hold draws each client cells from its own emulator, and the emulator
  consumed every sixel and every kitty escape and drew nothing for either:
  the OSC 52 trap, a third time. `mods/term/img.c` keeps them as regions --
  a sixel's own raster size turned into cells, a kitty transmission with
  its id, chunk by chunk -- and `"terminal"` version 4 hands them over for
  hold to re-emit at their own corners after each frame's cells. Once per
  change, so a still picture costs a settled frame nothing; skipped rather
  than cut in half for a client whose own rectangle does not hold the whole
  picture, because a bitmap cannot be clipped; and dropped when something
  writes a cell inside it, which is how a window that closed takes its
  picture with it. A terminal *window* still shows a space where a program
  drew a picture -- a window is cells inside somebody else's grid -- and now
  loses nothing on the way through hold.

**A redirection on a `console` command no longer reaches the display.** The
console drew on, and waited on, whatever descriptor number stdout happened
to have, so `console flush > /dev/null` sent a whole frame to the void and
`console key 1000 > /dev/null` returned at once -- a regular file is always
ready to read. Both looked like the console being broken and were a shell
redirection doing exactly what it says; one film measurement in this tree
read 0.05 ms a frame for that reason. The console now dups the terminal to
a descriptor of its own at `console open` (`CN_FDBASE`, 120, close-on-exec,
clear of 0-9, of a `{var}` redirection's 10 upward and of a process
substitution's 60) and closes it at `console close`.

**The YouTube player follows Control Panel > Pictures.** It kept a picture
setting of its own, so the one setting meant to reach every picture the
desktop draws reached the Image Viewer, the wallpaper, Mail and the browser
and not the player. `YT_MODE=follow` is the default now and what its Picture
menu and its pane offer first, with `half`, `ascii`, `mono` and `pixels`
still there for a window somebody wants different; settings version 7 moves
a file that still holds the old default and leaves a choice alone. Its
cycling key walks the list the menu offers rather than a second one written
out beside it, which is how `pixels` was missing from that key for a
release.

`tests/holdpix.py` is eleven checks of the whole path -- the cell size
reaching a held program, both protocols arriving at the client, a display
that joins afterwards getting what it never saw, text through a picture
taking it away, and a client that says nothing about pixels getting blocks
and no bitmap at all. The unheld sixel and kitty suites are its control.

What works and what does not, measured in a real desktop rather than
assumed: a picture in a **window** is pixels -- the Image Viewer places
270 kB of kitty picture at its own corner, and Mail, the browser and the
film player draw the same way -- and the **wallpaper** is not, on any
terminal and never has been (Gitea #104). A picture with text drawn over it
in the same frame is dropped before the first flush ever sends it, which is
exactly what the wallpaper is: placed, then the bar, the windows and the
icons go over it. `DP_IMG_UNDER` is the flag for that case and nothing
passes it yet; doing it needs an invalidation rule of its own, `z=-1` so
the picture is placed once rather than re-sent every frame, and cells over
it left at the default background.

## 0.99.76

**The kitty graphics protocol, because kitty draws no sixel** (Gitea #94).
It never has, and says so in its own documentation -- its own protocol is
the only way to put pixels in it. From 0.99.72 to 0.99.75 the console
nonetheless sent it a sixel on the strength of its name: the region claimed
its cells, the diff refused to paint them, and the terminal dropped the
bytes, so every picture on the terminal most likely to be in front of a
person was a blank rectangle. `mods/console/kitty.c` sends the real thing,
and the list of which terminal speaks which protocol is corrected: kitty,
ghostty, WezTerm and konsole take the kitty protocol, and foot, mlterm,
contour, yaft, iTerm2 and mintty sixel. `HIBR_GFX=kitty|sixel|off` says
outright, and `console gfx` names the one in force.

A kitty picture is an object with an id, not paint: it stays above the text
until deleted, so every region carries one, every path that drops or forgets
a region owes the terminal a delete -- sent before the next diff, so the
text underneath is painted in the same frame -- a replacement in the same
rectangle keeps the id, and closing the display deletes everything it was
holding. Nothing is asked to reply (`q=2`), since the answer would arrive
in the stream the key decoder owns.

Measured per frame of a 640x360 film, two runs agreeing: the kitty protocol
encodes three times faster than sixel (2.7 ms against 9.1 at 60x20) and
sends an order of magnitude more, because the payload is base64 of the raw
pixels (128 kB against 21 kB). So a picture that is not a still -- a film,
which asks for no palette of its own -- is sent at half the pixels in each
direction and the terminal scales it back up: four times fewer bytes, and
at full resolution a 100x34 frame was 1.4 MB and the pty could not keep up.
ADR 0037 has the whole table.

`img -m pixels` is the mode's name now, `sixel` still accepted, since which
protocol carries them is the display's business; the Control Panel's
Pictures setting says Pixels and keeps `pixels`, reading an older file's
`sixel` as the same thing. The YouTube player's Picture menu gained Pixels,
which it never had. The desktop asks `dt_imgpix` whether there are pixels to
be had rather than matching a protocol's name.

Honestly, and it is the reason this is not the end of the story: **a held
desktop still has no pixels.** `console gfx` answers `none 0 0` inside a
program `hold` started, because hold's own pty reports no cell size, and
`dt_autohold` holds every desktop -- so all of this reaches `img draw` in a
terminal and not the desktop the owner runs. That is Gitea #103, and it is
hold's work on both sides: carry the attaching client's cell size to the
program's pty, and give hold's emulator the picture regions so a bitmap
reaches the clients at all.

`tests/kitgfx.py` is 23 checks of its own -- the placement, the id, the
chunking, the deletes, the replacement, a pane's corner -- and
`tests/screen.py` now keeps APC payloads out of its screen model the way it
already did DCS, recording every control string in `Screen.apc`.

## 0.99.75

**A tarball read as a folder** (Gitea #102). A new module, `archive`, opens a
`.tar` or `.tar.gz`, takes its index once, and makes every member a filename:
`/dev/archive/NAME/path/inside` works anywhere a filename goes -- `read`, a
redirection, any builtin that takes a path -- through the scheme mechanism
`http` and `dav` already use, with no kernel mount and no FUSE. Beside that,
`archive ls|stat|cat`, shaped the way `dav ls` is, and `archive list` for
what is open.

Folders the archive never declared are implied from the member names, which
is what most archives need: anything Python's `tarfile` writes, and plenty of
real ones, hold only the files. ustar and GNU headers, the prefix field,
GNU's long name and long link blocks, pax headers read for their `path=`, and
octal or GNU base-256 numbers. An archive of 1024 zero bytes is an empty
archive and opens; a file that is neither a tar nor that is refused rather
than opening with no members, which is what the first version did with a
40-byte text file.

It is called `archive` and not `tar` on purpose: a module's builtins become
commands, and a builtin called `tar` would make `/usr/bin/tar` unreachable
the moment the module loaded. The cat module shadows `cat` because it is
byte-identical in a pipe and nothing can tell; this is no replacement for
tar, so it does not take the name.

A gzip archive is expanded once, at open, into a file nothing else can see,
so every later read is a seek; `ARCHIVE_MAX` bounds how far one may expand
(512 MB by default). The inflate is the prompt module's own, which read git's
objects and packs: it moved to `mods/inflate.c` and is shared rather than
written twice, with a gzip wrapper added beside the zlib one, and still no
zlib linked. Reading only -- a tar is append-only in practice, and rewriting
one to change a member is what archivemount does badly.

`tests/archive.py` builds its own fixtures with Python's tarfile -- plain,
gzip, pax, GNU long names, no folder entries, five hundred members -- and
checks all of it, including that a closed archive's paths stop being files.

## 0.99.74

**Maps are indexed** (Gitea #73). A map is a list, so a lookup walked it and
an append walked it twice: 20,000 appends took 2.7 seconds and building
20,000 rows of three fields took 12.6. Past sixteen entries a chain now
keeps an open-addressed index and its own tail, both in its first entry, so
nothing above `mp_find` and `mp_add` had to change and the order entries come
back in is still the order they went in.

| | before | after |
|---|---|---|
| 20,000 appends | 2746 ms | 74 ms |
| 20,000 rows of three fields | 12,616 ms | 136 ms |
| one field from each of 20,000 | 4016 ms | 109 ms |
| 5000 appends | 157 ms | 18 ms |

A 50,000-row `csv read` is 97 ms, and 2000 lookups into that map cost 9 ms
where each used to walk 25,000 entries -- the csv module asks for an index
once it has linked a large result, since a chain built by hand has none.
Resident memory for a 20,000-entry map goes from 2500 kB to 3092 kB: 16
bytes an entry, and the index at three quarters load rather than half, which
costs 12 ms on those 20,000 appends and saves 272 kB. The ABI is 16, since
`struct ent` grew.

**The sixel encoder is 2.5 times faster**, which is what measuring it was
for. A film frame at 100 by 34 cells (800 by 544 pixels) cost 61 ms to
encode and now costs 23.7 ms; at 60 by 20 it was 21 ms and is now 8.9 ms.
Half blocks cost 0.3 and 0.8 ms, so pixels are still about thirty times the
price, and a film at 100 by 34 is bound to roughly 40 frames a second by the
encoding alone -- measured on a 640 by 360 clip through the player, median of
twenty frames, and written down in ADR 0037 rather than left as a feeling.

The cause was in the first version: it scanned a band once for every palette
colour, 216 times over, where one pass over the band's own pixels can fill
every colour's column pattern at once. The fixed palette's level maths is a
table now as well. What is left is the emission itself, since photographic
content uses most of the palette in every band; a smaller palette for film,
emitting only the bands that changed, and the kitty graphics protocol are
the three ways further, none of them built.

## 0.99.73

**The browser draws the page as a picture** where the terminal can paint
pixels (ADR 0037): the screenshot is taken at the viewport's own size and
drawn as pixels, so a page looks like the page -- its own fonts, its
pictures, its layout -- rather than text over coloured blocks. There is no
text layer in that mode, on purpose: a text cell paints its own background,
so anything drawn over the bitmap boxes itself out of it. Clicks and typing
still reach the page, which go by coordinate; what is lost is reading the
page as cells, so no copying text out of it and no link cells. The window's
own chrome -- tabs, address, the status line -- stays text either way, and
**Control Panel > Pictures** chooses, as it does for every other picture.
`web mode T cells|pixels` is the same thing for a script.

## 0.99.72

**Pictures as pixels** (sixel). Where the terminal can paint them -- kitty,
foot, WezTerm, mlterm, iTerm2, mintty, contour -- a picture is drawn as
pixels instead of coloured half blocks: the Image Viewer, the wallpaper,
Mail's pictures and the film player. **Control Panel > Pictures** chooses:
Automatic, Half Blocks, Grey Blocks, Characters or Pixels, and says what
this terminal can do beside it. `img draw -m MODE` and `media mode ID sixel`
are the same thing for a script, and `console gfx` answers "sixel 8 16" or
"none 0 0" so nothing has to guess from `$TERM`.

A bitmap is not cells, so the console owns it (ADR 0037, `dp_api` version
5): a picture is placed as a *region*, encoded once, and the ordinary cell
diff keeps it honest -- the cells it covers are left alone, so a window
clearing its own face each frame does not paint over it, and the region is
dropped the moment those cells stop matching what they were, so a window
that moves or closes paints its text again with nothing having to say so. A
still picture therefore costs nothing on every flush after the first. The
palette is chosen per picture for a still one (median cut, 256 colours) and
fixed 6x6x6 for a film, where the same palette every frame is what lets a
terminal keep its colour registers.

Whether a terminal can paint pixels is decided from two facts rather than a
probe: it must report the pixel size of a cell, since sixel paints 1:1 and a
wrong cell size spills the bitmap into its neighbours, and it must be one
known to understand the format. `HIBR_GFX=sixel` or `off` overrides. A
Primary Device Attributes probe was rejected on purpose: its reply arrives
in the stream the key decoder owns, racing a keystroke.

`tests/sixel.py` drives all of it through a pty -- the picture lands where
it was put, the text around it survives, the same picture again sends
nothing, text drawn through it brings the text back, a pane clips it, and a
terminal that says nothing about cells gets blocks. The harness now keeps
DCS payloads out of its screen model and records where each landed, and a
test pty can say what a cell measures.

The browser is not converted: it draws text over its screenshot, and a text
cell paints its own background, so "page as a picture" and "text over
colours" are two different renderings -- a decision, not a detail.

## 0.99.71

**Who orders right-to-left text is asked of the terminal** (Gitea #68, ADR
0031). 0.99.70's Arabic was reported unreadable within the hour, and the
reason was a setting rather than the catalogue: hibr put the text in display
order, kitty shaped it with HarfBuzz -- which orders an RTL run along with
the shaping -- and the two reversals cancelled, so Arabic read left to
right. **Control Panel > Language > Order Right-to-Left Text** now offers
Automatic, hibr or The Terminal, and Automatic is the default: hibr orders
unless the terminal says it is kitty, VTE 0.60 or later, or iTerm2 3.5 or
later, each of which does its own. Automatic shows what it settled on in
brackets. A settings file that still held the old frozen "on" is moved to
Automatic once (`DT_SETVER` 6); someone who had chosen The Terminal keeps
it.

A prayer-note check in `tests/desktop.py` could only pass between Fajr and
Isha -- it marked "last looked" a second before today's Fajr and expected a
note for each prayer since, which is none at all before dawn. It gives the
ticker six moments just passed instead; what the sun does is
`tests/salat_adhan.py`'s, all year, to the minute.

The pty harness stops passing the terminal's own identity through to a test
session, so a suite run from kitty draws Arabic the same way as one run
anywhere else, and `tests/desktop.py` checks both ways round: shaped forms
in the cells by default, the letters as written under kitty, and hibr
ordering again when it is asked for outright.

## 0.99.70

**Arabic** (Gitea #68, ADR 0032). `examples/desktop/lang/ar.json` is the
first real translation: all 806 strings the desktop draws, right to left,
with Arabic's own six plural categories where English has two -- so the
Trash's own message counts correctly at none, one, two, a few and many.
Choose it in **Control Panel > Language** and the desktop both translates
and turns round, since the catalogue says it is right to left; English
stays the default and costs nothing. Eight strings stay in Latin on purpose
-- product and protocol names, an example address, the project's own
tagline -- and `tests/993-lang-ar.t` names them, so any *other* string added
without a translation fails there.

**Write's toolbar is mirrored** (Gitea #68, ADR 0033), the last part of the
desktop that was not: its buttons run from the right edge in a
right-to-left desktop, and each is registered where it is drawn, so a click
lands on the button a person sees rather than on its unmirrored twin.
"Plain text" moves to the right for a file that is not markdown.

## 0.99.69

**The vault on the desktop** (Gitea #71). A **Vault** desk accessory reads
the Bitwarden or Vaultwarden vault 0.99.68 brought to the shell: type to
find an item, then Password, Username or Code puts it on the clipboard.
A copied password is never written to the clipboard history, and the
clipboard clears itself afterwards -- 30 seconds by default, a setting.
**Control Panel > Passwords** holds the rest: the server and account, Log
In, Lock, how long it stays unlocked, the PIN, how often it syncs while it
is, and that clipboard timeout.

The desktop is unlocked once for all of it, by master password or PIN, and
exports the session key, so a Terminal window opened afterwards can use
`vw` too; Lock, or the timeout, ends it everywhere. Nothing decrypted is
kept in the desktop: every read runs the `vw` command as a child and the
keys stay in its own module. Syncs run as jobs, so the desktop never waits
on the network, and the vault is kept here encrypted, so everything but a
sync works with none.

`tests/vault.py` drives all of it through a pty against the stand-in
server, including that no password reaches the clipboard history or any
file. A desk accessory missing from the desktop README's own table since
0.99.67 (Prayer Times) is listed now, next to the new one.

## 0.99.68

**Bitwarden and Vaultwarden from the shell** (Gitea #71). A new command,
`vw`, logs in to a Bitwarden-compatible server -- Vaultwarden,
self-hosted, first -- syncs the vault and reads it: `vw list`, `vw get
password|username|totp|notes|uri|item NAME`. The vault is kept here
encrypted, exactly as the server sends it, so `vw unlock` and everything
after it work offline; `vw sync` fetches it again with the stored login,
no password. Unlocking prints `export VW_SESSION=...`, as Bitwarden's own
CLI does with BW_SESSION, and the vault locks after `vw timeout` minutes
unused (15 unless set). A **PIN** can unlock it too (`vw pin set`), until
the next lock, or for good with `--keep`.

The keys never leave a new module, `vw`, whose builtin `vwk` does the
crypto -- PBKDF2 or Argon2id, Bitwarden's EncStrings (AES-256-CBC with an
HMAC checked first), the session and PIN wrapping, TOTP -- with libcrypto
loaded at run time (ADR 0036). Nothing decrypted is ever written to disk,
and the suite checks that it is not. The crypto is tested against an
account built independently with Python's cryptography package, and the
command against a stand-in server. It reads; changing items,
organisations, two-step login and a desktop app are still to come.

`dav request` is a plain HTTP request to any address, for an API that is
not WebDAV -- the vw command's requests go through it.

**tests/apps.py runs as four parts** (Gitea #101): `apps_panel`,
`apps_reach`, `apps_core` and `apps_more`, side by side, each cut from
apps.py at run time by `tests/appslice.py`, so apps.py is still the one
place a check is written and still runs whole. The same 548 checks;
the suite's twelve minutes become the slowest part's eight and a half.
`tests/affected.py` now also picks the dav, web, media, YouTube and
markdown suites when their own module changes -- it never did.

## 0.99.67

**Prayer times** (Gitea #70). A new module, `salat`, works out the day's
prayer times for a place: the sun's position computed as Meeus gives it,
the method -- angles, Isha by angle or minutes after Maghrib, offsets,
rounding -- read from a JSON file in a folder, like calendars (ADR 0035).
Twelve methods are bundled (Muslim World League, ISNA, Egypt, Umm
al-Qura, Karachi, Dubai, Moonsighting Committee, Kuwait, Qatar,
Singapore, Tehran, Diyanet) and each is checked against adhan-js to the
minute, at sixteen places from the equator to inside the Arctic circle,
through the year, with both Asr schools. A method of one's own is a file.

On the desktop, each a switch in **Control Panel > Prayer Times**: a
**Prayer Times** desk accessory (today's six times, the next lit with how
long until it); the next prayer beside the bar's clock; a note at each
prayer, and minutes before if asked; a Control Strip module (shown when switched on); and the
**athan**, a sound file you choose (another for Fajr if you like), played
through the media module -- hibr ships no recording. The place comes
from the time zone, as Date & Time finds it, or from a latitude and
longitude set by hand; the Asr school, the high-latitude rule and a
minute's adjustment to each prayer are settings too.

`hcal list` and `salat list` are sorted by title, and both now refuse a
date with a month or day out of range, or anything after it.

**The Hijri date is part of the date format** (Gitea #69). Rather than a
switch that put a fixed Hijri date beside the clock, a date format now
has six Hijri codes beside strftime's own -- `%id %ie %im %iY %iB %ib` --
so the menu bar's clock, a custom format and every date the desktop
draws can carry the Hijri date wherever the format puts it:
`%a %ie %ib %H:%M` reads "Mon 24 Rab II 14:40". `hcal format` does it for
scripts too. **Date & Time** now holds the date settings: **Hijri** (None,
Umm al-Qura, or a tabular calendar -- None leaves the codes empty and
Calendar's days plain), **Adjust** and **Week**; Hijri bar formats are
offered when a calendar is chosen. **Digits** stays in Language. Month and
weekday names in any format follow the language. Settings saved with
0.99.66's Show Hijri Dates on keep a Hijri date in the bar.

## 0.99.66

**Hijri dates, and dates as a region writes them** (Gitea #69, #68).
A new module, `hcal`, gives the Hijri date of any day and the day of any
Hijri date in whichever reckoning a person follows. Calendar systems are
data -- JSON files in a folder, like themes (ADR 0034) -- so a region's
own can be added with no code: hibr ships Umm al-Qura (from ICU's table,
1300-1600 AH) and the civil and astronomical tabular calendars, and each
is checked against ICU on every day from 1870 to 2200. A whole-day
adjustment covers local sighting of the moon.

In Control Panel > Language, under Dates: **Show Hijri Dates** puts the
Hijri date beside the bar's clock and the Gregorian date in Date & Time,
and each of Calendar's days carries its Hijri day, its title naming the
Hijri months the month runs across; **Hijri Calendar** and **Hijri
Adjustment** choose the reckoning; **Digits** writes numbers in
Arabic-Indic digits; and **First Day of Week** starts Calendar's week on
Saturday, Sunday or Monday, or, on Auto, where the locale's region does
(CLDR's table: Saturday in Egypt, Sunday in Saudi Arabia and the United
States, Monday elsewhere). Month and weekday names now go through the
catalogue too. Gregorian dates are always shown; Hijri ones only when
asked for.

## 0.99.65

**The rest of the lists turn round** (Gitea #68, phase 4c). Mirrored,
the Clipboard reads each entry from the right -- its time, its mark,
then the text -- with its scrollbar at the left; Task Manager's columns
run the other way, Mem first and PID last, the names right-aligned and
each heading still sorting where it is drawn; YouTube's results put the
title at the right and the channel and length at the left; and
Calendar's agenda is right-aligned. dBASE keeps dBASE III Plus's own
command line, as a terminal keeps its program's screen, and games, the
calculator's keypad and the clock keep their shapes.

## 0.99.64

**Calendar and Contacts turn round** (Gitea #68, phase 4c). Mirrored,
Calendar's month runs its week from the right, each day's number and
events right-aligned, the month's name at the right of the bar between
a previous arrow pointing right and a next one pointing left, and the
left arrow goes to the next day, as it looks; the chosen day's events
are right-aligned below. Contacts puts its list at the right and the
person chosen at the left, each field's label at the right of its
value. The agenda view is not mirrored yet.

## 0.99.63

**Sheet and the file dialog turn round** (Gitea #68, phase 4c). Mirrored,
Sheet puts column A at the right with the row numbers outside it and the
columns running leftward, the cell's name at the right of the formula
bar, its scrollbar at the left; a column is widened from its left edge,
and the left arrow goes to the next column, as it looks. The file dialog
used for Open, Save As and Export everywhere puts its list and the name
at the right, the preview and the type at the left, and its divider
drags the other way.

## 0.99.62

**Mail turns round** (Gitea #68, phase 4c). Mirrored, the sidebar is at
the right with each view's count at its left, the search field at the
right and the account at the left, and a conversation's row reads date,
subject and snippet, senders, star from left to right. An open
conversation puts its subject, sender, notes and attachments at the right
and the date at the left, Back at the right. A message's own lines keep
the layout the HTML module gives them -- the console orders right-to-left
text within each, but a right-to-left message is not yet right-aligned.

## 0.99.61

**Restart Desktop works on a Mac.** `json parse NAME < file` read its
input through the C library's stream, and on macOS a stream that has once
reached the end of a file stays there: the second `json parse < file` in
a process read nothing. The desktop reads its clipboard history that way
just before putting a restart's windows back, so on a Mac every restart
found its saved state "malformed", set it aside as `state.json.bad`, and
came back with nothing -- a terminal's shell included -- and Control
Panel > Appearance listed no themes and no colour schemes, every file
after the first read as empty and refused. Each read now
starts and ends with the stream's end-of-file cleared; `uni levels`
likewise.

**Task Manager logs nothing for a process with no memory figure.** A
zombie or a kernel thread has no `VmRSS`, and the Linux scan stored that
empty, so every redraw logged `tasks_human: kb expects int, got ''` for
each one; it counts as 0 now, as the macOS scan already did.

**The Terminal's menus begin with File and Edit**, then Shell, as every
other app's do: File has New Window and Close Window.

**The log says how long the desktop took to start**: one line in
`desktop.log` after the first frame -- `started in 94 ms: window manager
21, apps 37, console 0, open 2, first frame 32` -- timed from the first
process, across the hold re-exec and the supervisor, and again after a
restart. A slow start says where it went.

## 0.99.60

**Control Panel's panes and Files turn round** (Gitea #68, phase 4c).
Mirrored, every row of a Control Panel pane puts its value, checkbox,
slider or dropdown at the left and its label at the right, a pane's
scrollbar on its left edge. Files puts its path at the right and the
search box at the left, its names at the right of the list, the details
columns in the other order with the name last, and its icon grid from
the right; a click on a tile or a column heading reaches what is drawn
there. Mail, Sheet and the rest of the apps follow.

## 0.99.59

**A script reaps its background jobs.** A non-interactive hibr collected
a finished `( ... ) &` only at `wait` or `jobs`, never on its own, so a
long-running script that started jobs and read their results from files
-- the desktop's remote transfers and syncs -- kept every one as a zombie
for as long as it ran: a day-old desktop had a hundred. Each finished job
is now reaped before the next one starts, so at most the newest lingers;
a script keeps the status of the last 256 finished jobs for `wait`, and
`wait PID` on one already reaped returns its status rather than 0.

## 0.99.58

**The rest of the desktop's own layout turns round** (Gitea #68, phase
4b). Mirrored, the desktop icons start from the left edge, the Control
Strip docks at the right, notifications stack from the top-left corner, a
tiled workspace keeps its main window on the right and the stack on the
left, and Control Panel puts its list of panes on the right of the
divider, which drags the other way. Your own strip side and notification
corner are still yours: they are read the other way round, never rewritten
(ADR 0033). The inside of a Control Panel pane, scrollbars and each app's
own layout come next.

## 0.99.57

**The desktop turns round for a right-to-left language** (Gitea #68,
phase 4a). With Arabic or Hebrew -- or Control Panel > Language > Mirror
Layout set to on -- the menu bar runs from the right edge, the
application menu, the clock, notifications and the workspaces from the
left; menus open under their title's right edge, submenus to the left,
each row's shortcut on the left and its label right-aligned; a window's
buttons sit on its left and its title on its right; a row of dialog
buttons runs the other way. It is a layer over the settings, never a
change to them, so your own button side and title alignment still mean
what they did (ADR 0033). A right-to-left pseudo-language, `xy`, drives
the suites through it. Control Panel, the desktop icons, the Control
Strip and tiling follow next, then each app's own layout.

## 0.99.56

**The rest of the desktop's text is translatable** (Gitea #68). 677
distinct strings now go through the catalogue, from 448: notes built from
values are templates (`dt_note "Saved %s" "$f"`, 104 of them), confirms
and menu items with a value are filled through `dt_tr` first, counts that
need a plural use `dt_trn`, text an app draws itself goes through
`dt_tput` (93 places), and a window title translates its name and keeps
what is in brackets ("Files [~/notes]"). What is left in English is data
-- file names, a message's subject -- and a few messages that splice in a
word from elsewhere ("Moved", "to the trash"); `xx` shows them.
`printf -v NAME -- FORMAT` takes `--` as the end of its options, as bash
does; hibr used to print `--`.

## 0.99.55

**The menu bar follows the terminal's size.** On a resize -- and a
reattach is one, the new terminal's size arriving as a resize -- the
screen was drawn again at the new size but the menu bar was not laid out
again, so its right end (the workspaces, the clock, the app's menu) stayed
where the old width had put it: past the edge of a narrower terminal, or
short of a wider one. The bar's layout is kept between frames and rebuilt
only on input or a change of focus; a resize now asks for it too. And a
menu item in a right-to-left language is drawn whole, its shortcut beside
it, where the underline split it into three pieces, each ordered on its
own. A note's text may be a template now, `dt_note "Saved %s" "$f"`,
translated and filled in one call.

## 0.99.54

**The desktop can speak another language** (Gitea #68, phase 3). A
language is a JSON catalogue, English as the key, in `lang/` or the
person's own `~/.config/hibr/lang`; Control Panel > Language chooses it,
beside the switch for terminals that reorder right-to-left text
themselves. The `lang` module holds the catalogue in a hash, with CLDR
plural rules (Arabic's six forms among them) and arguments a translation
can reorder (`%2$s`). The desktop translates at its widgets -- menus,
items, notes, confirms, buttons, checkboxes, icons, window titles and
Control Panel -- so the 450 distinct strings that reach one need no edit
where they are written; templates and text an app draws itself follow
app by app. `tools/strings.py` finds every string drawn, keeps
`lang/strings.txt`, and writes a pseudo-language, `xx`, that marks each
one, so a desktop running in it shows what is not yet translated; a test
fails when the list and the code differ. English pays one variable read
a widget, about 1.3% of building a menu and nothing on an idle frame
(ADR 0032). The Arabic catalogue ships once reviewed.

## 0.99.53

**hvi edits Arabic and Hebrew** (Gitea #68, phase 2 complete). A line
holding right-to-left text is drawn in display order, Arabic joined, each
character keeping its own colour, selection or search highlight; motions,
deletions and the cursor stay logical, the cursor standing on the column
its position landed in. `uni`'s interface is version 3 for it (`line`, a
line with each drawn character's source and column). With this, every
place hibr draws text a person reads or edits -- the desktop, the prompt,
Write, Stickies and hvi -- puts right-to-left text in display order.

## 0.99.52

**Write edits Arabic and Hebrew** (Gitea #68, phase 2c). Each row of a
line holding right-to-left text is drawn in display order, Arabic joined,
each cell in its own style -- bold, italic, a link -- while the cursor,
typing, clicks and the selection stay logical: typed letters go where
they read, a click lands on the letter under it, and the file saves as
written. Markdown and plain text both, the plain path through the
textarea widget, so Stickies has it too. `widgets/bidi.hibr` is the
shared piece, over a new `uni map` (a row of a line with where each
character went), and `console put -r` draws text already in display
order. `hvi` is next.

## 0.99.51

**The prompt reads Arabic and Hebrew too** (Gitea #68, phase 2b). A line
holding right-to-left text is drawn in display order, Arabic joined, one
screen row at a time, the whole line one paragraph -- levels and shaping
over all of it, each row reordered on its own as UAX #9 says, so a word
cut by the wrap still joins across it. The buffer, the keys and the
cursor stay logical: left and right step through the text as typed, and
the cursor stands on the column its position landed in. `uni`'s interface
is version 2 for it (`vismap`, a line of a paragraph with where each
character went). `HIBR_BIDI=off` turns it off for a terminal that reorders
itself, and is the console's default too. `^U` was documented as killing
the whole line; it kills back to the start, as in bash, and says so now.

## 0.99.50

**The desktop draws Arabic and Hebrew readably** (Gitea #68, phase 2a).
Text a script draws with `console put` that holds right-to-left characters
now goes through the `uni` module first: display order, Arabic in its
joined forms, marks after their base, brackets mirrored. A pane's text is
clipped first and ordered after. Plain text never reaches `uni`, and a
program's own cells -- a terminal window, `most`, `hvi` -- are drawn as the
program laid them out. `console bidi off` is for a terminal that reorders
right-to-left text itself (ADR 0031). The core's width table, written by
hand and incomplete, is generated from Unicode 15.1.0 now, like `uni`'s,
and is 6% cheaper; the test harness takes widths from Unicode as well.

## 0.99.49

**The `uni` module: bidirectional text and Arabic shaping for a cell grid**
(Gitea #68, phase 1 of 5). A terminal draws a character per cell, left to
right, and shapes nothing, so Arabic needs its joining forms chosen per
letter and any right-to-left script needs the Unicode Bidirectional
Algorithm to be read at all. `uni vis` gives the display order -- UAX #9
in full, every one of the 770,241 cases of Unicode's BidiTest and 91,707
of BidiCharacterTest passing -- with Arabic in its presentation forms,
lam-alef as one cell, marks kept after their base and brackets mirrored,
checked case by case against GNU FriBidi. Every table comes from one
Unicode version, 15.1.0, generated by `tools/unigen.py`, with the
conformance suites from the same download kept in `tests/uni` (ADR 0030).
Nothing draws through it yet: the console and the editors are phase 2.

## 0.99.48

**The `lines` module: show, search and edit files by line, exactly**
(Gitea #67). Work on this tree reached for `sed -n`, `grep -rn` and a
python replace that refuses an ambiguous anchor; a hibr script doing the
same was 60 to 270 times slower, so it is C. `lines show` prints a range
byte for byte; `lines grep` searches a tree with grep's output and
context, as fast as GNU grep on a plain pattern; `lines count` counts a
literal; `lines edit` takes `<<<<` old `====` new `>>>>` blocks on
standard input, applies them in order, and writes nothing unless each
old text occurs exactly once at its turn -- then through a new file
renamed over the old, its mode kept. CLAUDE.md now says to use it.

## 0.99.47

**An About card keeps the end of a long path.** The card cut the app's
file path to its width from the right, so a path longer than the card
lost the file's own name -- which is how the About check failed in every
full sanitizer run, made from a worktree deep under the job folder, and
in none of the ordinary ones. A long path now shows its end, after an
ellipsis. And the app menu's builder reads into a declared local rather
than `_`, which strict vars took for a new global; the change had been
sitting in the working tree, tested by every gate and shipped by none.

## 0.99.46

**`desktop ctl resize` is measured against the window's own display.**
Under Blit the desktop's screen is the primary's surface alone, 128
columns, while a joined display sits beside it at column 128; a resize
went through the same clamp as a drag, against that screen, so a window
moved to the joined display was clamped to no width and every resize
there was refused (Gitea #100). A ctl resize now finds the display the
window is on from the display list and checks the size against that
rectangle, and a size that does not fit is refused whole, the window
left as it was, where the clamp used to shrink it quietly. Tested on a
joined pair, and with given layouts for one display below another and
two panes side by side.

## 0.99.45

**About an app is found in one pass.** 0.99.44's grep-free search for the
file that declares an app read every app file through on a miss, 371 ms
each time and several times that under the sanitizers. The desktop now
reads each app file once, only as far as its first function (where every
`dt_app` line is), and keeps the answer: 36 ms, once a session. The
sanitizer wrapper names `setarch` by its path, so the test that runs the
desktop with only hibr on `PATH` can start under ASan too.

## 0.99.44

**`mv` is a builtin, and the desktop starts with no coreutils at all.**
Under Blit's busybox-only guest the desktop's crash snapshot could not be
written -- it is written beside and renamed into place with `mv` -- and
About Me was read with `head` at every start. `mv` joins `mkdir` and `rm`
(ADR 0029): renames and moves into a directory, `-f`, `-n`, `-v`, `-t`,
`-T`, answering as GNU's does, checked against it by `tests/987-mv.t`. A
move across filesystems is a copy, and goes to the program. About Me is
parsed straight from its file, the About box finds its app without `grep`,
dBASE's `DATE()` and `TIME()` use
`printf '%(...)T'`, and Mail makes its empty POP map with `: >>`. The
Blit-launch test now fails on any command not found. What the desktop
still forks (`sort` with keys, `cp -R`, `touch -r`, `ps`, `readlink`,
`uname`) is a system tool's job; a guest that wants those can link
busybox's applets.

## 0.99.43

**The control socket under Blit, for real this time; `mkdir` and `rm` are
builtins.** 0.99.41 logged why a socket was missing and retried, and it
was still missing in Blit's guest. The reason: that guest has busybox and
no applet links, so there is no `mkdir`, and the socket's private folder
was made with `mkdir -p -m 700`. The log's folder was made the same way,
so the log that would have said so never opened either. Hold makes its
folder in C, which is why its socket was there. `mkdir` and `rm` are
builtins now (ADR 0029), answering as GNU's do and handing any option
they do not implement to the program on `PATH`. The desktop makes its
folders with no fork, and with no coreutils at all. Under Blit, whose
display cannot watch a descriptor, the socket is polled twice a second
instead, or a request would wait for the next key. `tests/desktop.py`
launches the Blit way with `PATH` holding only hibr. The desktop still
forks other programs such a system lacks -- `mv` for its crash snapshot,
`date`, `sort` -- which is Gitea #99.

## 0.99.42

**A sanitizer gate in minutes, and the full run after.** The whole
sanitizer pass -- every suite under ASan and UBSan -- takes over twenty
minutes, which made every release that touched C wait for it.
`tests/asan.py --quick [--since REF]` is the gate now: run.sh, self.hibr
and 300 rounds of the parser fuzzer, the suites of every C module outside
the desktop, and the pty suites a module changed since the last tag
reaches (from `tests/affected.py`). It took 133 seconds here, the
sanitizer build included; the build now runs in parallel. A change to a
module the desktop uses widely -- csv, say -- brings the desktop and apps
suites in, and takes as long as they do. The full `tests/asan.py` runs
after each release, and any report it makes becomes a ticket.

## 0.99.41

**The control socket says why it is missing, and keeps trying.** A desktop
that cannot make its socket -- the folder not its own, the path in use,
a `TMPDIR` that is not there -- used to say nothing, and `desktop ctl`
could only report that nothing was listening. Every outcome now goes to
the desktop's log as one `desktop: control socket: ...` line (listening
and where, off and why, or what failed), a failure is retried every five
seconds, and a socket file removed from under a running desktop is made
again within half a minute. `desktop ctl` against a session that is held
and running but has no socket says so and points at those log lines.
Tested the way Blit launches it: held as `--session blit` with a display
name, the supervisor on, a second display joined, `ctl` through
`session.hibr`.

The pty harness no longer misses a shell error that is the first line of a session's `desktop.log`: it was joined to the terminal's output with no newline between, so the check for errors read past it. The one it had been hiding was the slip test's own, expected but declared too late. What it found next was real: the Wallpaper picker sized its first preview before loading the img module, so that preview filled the box instead of keeping the picture's shape, and the log said `img: command not found`. The theme checks know the bundled Retro Car theme.

## 0.99.40

**Calendar and Contacts point to Control Panel > PIM.** Their notes and
empty-window hints still named the pane by its old name, Calendars &
Contacts; the rename in 0.99.38 had left them out.

## 0.99.39

**The desktop has a control socket.** A program can ask a held desktop
which displays are attached and which windows are open, and move, resize
and focus them -- through the same operations the menus use, never by
typing keys at it. It is what Blit, a terminal server that draws hibr
straight to the screens with no X11 or Wayland, needs to drive a desktop
across several displays. `desktop ctl displays`, `desktop ctl windows`,
`desktop ctl move 3 right` print the desktop's JSON answer; errors are
named (`no-window`, `no-display`, `detached`, `bad-geometry`,
`unauthorized`...). Only the desktop's own user can use it.

**`listen` serves Unix sockets, and `accept` can wait a while or not at
all.** `listen -b /path` binds an owner-only Unix socket, `$REMOTE` is the
connecting process's `uid:N`, and `accept -t secs` gives up after that
long (`-t 0` only looks).

**`exec {fd}<>/dev/unix/...` and `/dev/tcp/...` work.** The socket went to
standard input and `fd` stayed empty.

## 0.99.38

**Appearance, regrouped.** Headings for Look (Theme, Colours, Glyphs),
Wallpaper (Pattern, Image, Mode), Menu Bar, Shadows and Controls. The Theme
row carries its own buttons: **Save** appears once the look no longer
matches the theme, and a theme of your own can be **renamed** and
**deleted** there (or with `s`, `r`, `d` on that row).

**Calendar and Contacts are Desk Accessories**, on the hibr menu with
Calculator and Clock. **Control Panel > Calendars & Contacts is now PIM**,
and it asks each Network Server what it keeps: servers with calendars or
contacts get a switch saying which, a plain file server is shown as files
only, and Add Server… opens Network Servers.

**A Nerd Font glyph set**, Appearance > Glyphs > nerd, for a terminal
whose font is a Nerd Font: folders, files, home, disks, the trash and the
marks drawn as its icons. **Checkboxes can be a tick**, `[✓]`.

## 0.99.37

**A csv module.** CSV as RFC 4180 says -- quoted fields, doubled quotes,
line breaks inside quotes, CRLF or LF, a byte-order mark skipped, any
separator. `csv read FILE` binds every record as `r[i][j]`, or with `-H`
by the header's names; `csv open`/`row`/`close` stream a record at a time;
`csv line` writes one with only the quoting a field needs; `csv split`
takes one record apart. 50,000 rows bind in a quarter of a second. Sheet's
Import and Export CSV go through it, so a quoted cell with a line break in
it now imports whole.

**`printf` and `echo -e` read escapes as bash does.** A format's `\NNN`
and `\xHH`, `%b`'s `\NNN` and `\xHH`, `echo -e`'s `\xHH`, and `\c`
ending `%b` and `echo -e` -- nothing printed after it, not even the newline
-- while a format prints it as it is. hibr had none of these.

## 0.99.36

**YouTube's mini player.** `i`, Play > Mini Player or a double click on
the picture shrinks the player to the picture alone -- no border, title or
bar -- in a small window at the bottom right, on every workspace. Drag it
anywhere; right-click it for pause, seek, next, volume, Full Player and
Close; a double click or escape goes back to the full player where it was.

**Windows with no chrome.** `DT[$id]["chrome"]=none` gives an app every
cell of its window; any press moves it, a double click and a right click
are the app's. **Double clicks reach apps**: `dt_isdbl` says whether a press
is the second of one -- Calendar's double click on a day, which never
worked, now makes an event there.

## 0.99.35

**Windows on every workspace.** Window > On Every Workspace, and the same
on a title bar's right-click menu, makes a window sticky: it stays in view
whichever workspace is current, and floats over a tiled one. Taking it off
leaves it on the workspace you see it on; sending it to one workspace takes
it off too. Restart Desktop keeps it. Control Panel > Stickies > Notes on
Every Workspace does the same for every note, open now or opened later.

## 0.99.34

**The desktop is the Finder.** With no window focused the menu bar reads
File, Edit, View and Special, as System 7's Finder did: File > New Window,
Open (the icons chosen) and Find…; View > Desktop Icons and Refresh;
Special > Clean Up Desktop (moved from the hibr menu) and a new Empty
Trash…, which asks and then deletes for good.

**About follows the front app.** The hibr menu's first item is About
Files…, About Calendar… and so on for whatever is in front -- a helper
window answering for the app it belongs to -- with About hibr Desktop
below it. An app may define `<app>_about`; otherwise the desktop shows a
card of its icon, name, description, file and the shell's version.

## 0.99.33

**Themes are the whole look; colours are their own choice.** What used to
be a theme -- seven colours and a shadow -- is now a colour scheme,
Appearance > Colours. A theme, Appearance > Theme, is the whole look at
once: a colour scheme, the wallpaper (a glyph or a picture), the window
frame, the title bar, the buttons and where they sit, checkboxes, dialog
buttons, shadows and the glyph set. Every setting it carries can still be
changed on its own afterwards, and Save Current Look as Theme… keeps the
look as it stands under a name of your own. Themes and colour schemes are
both JSON files (`examples/desktop/themes/`, `examples/desktop/colours/`,
and your own in `~/.config/hibr/`), each checked whole; a colour file you
kept in `~/.config/hibr/themes/` is still read as colours.

**Two new looks.** Under Construction: black-and-yellow hazard stripes,
yellow windows, double frames and solid black title bars. Meadow: a sky
over a green hill, beige windows with rounded frames and blue title bars,
after the desktops of around 2001. Classic is the desktop as it has
always looked.

**Solid title bars.** Control Panel > Windows > Title Bar: `line`, the
title sitting in the frame's top edge as before, or `solid`, the whole top
row a bar of the frame's colour with the title and buttons on it.

## 0.99.32

**Calendars and contacts sync on their own.** The servers chosen in
Control Panel > Calendars & Contacts are synced every so often whether or
not Contacts or Calendar is open -- before, nothing synced until one of
them was, so a reminder could be read from an old copy and Mail finished
addresses from one. What the three apps share about syncing now lives in
`examples/desktop/lib/pim.hibr` instead of inside Contacts.

**Copy on Select in Terminal.** Control Panel > Terminal > Copy on Select
copies a mouse selection the moment the button comes up, as xterm does. The
text stays lit, and the next key only puts the highlight out -- it is not
sent to the program, so the enter pressed out of habit after a copy does
not run a half-typed line. Off by default.

## 0.99.31

**Calendar.** A new app keeps your calendars on this machine from any
CalDAV server, through the same Calendars & Contacts settings as Contacts:
a month at a time, each day's events in their calendar's colour and the
chosen day's listed with their times, or an agenda of the next sixty days.
Events are made, edited and removed here -- title, date and times or all
day, a repeat, a reminder, the calendar, a place, notes, who is invited --
written in this machine's own zone and sent at the next sync. Reminders go
off as notes whether a Calendar window is open or not.

**Invitations, both ways.** Inviting people to an event mails each of them
an invitation from your Mail account, the event in it as a calendar
request any mail program understands. An invitation that reaches you is a
card in Mail -- what, when, where, from whom -- with Yes, Maybe and No: the
answer goes to the organiser and the event into your calendar. An answer
to your own invitation can be recorded in your copy, and a cancellation
removes the event.

**The pieces underneath.** `email build -C file -M METHOD` adds a calendar
part beside a message's text; `pim ics store` makes the copy a calendar
keeps of an invitation, with an attendee's answer set; `pim when` turns a
local date and time in any zone into seconds and back.

**A sync is no longer held up by a mail being sent.** A change made while
an invitation was going out waited for the next timed sync, because the
sending job was taken for a sync already running.

## 0.99.30

**Contacts.** A new app keeps the people in your address books on this
machine, from any CardDAV server -- Nextcloud, Fastmail, iCloud, Radicale:
a searchable list, each person's emails, phones, organisation and note, and
new, edit and remove, sent to the server at the next sync and only if its
copy has not changed meanwhile. Control Panel > Calendars & Contacts
chooses which Network Servers to use and how often to sync; a background
job finds the address books itself and keeps them up to date with sync
tokens.

**Mail finishes addresses.** Typing in To, Cc or Bcc offers the people who
match, from Contacts and from everyone Mail has had a message from; tab or
enter takes one. Message > Add Sender to Contacts adds whoever wrote the
conversation, and a click on an email in Contacts writes to them.

**Mail's New Message is no longer an app on the hibr menu.** It was meant
to be hidden and was not, from 0.99.24, because of one argument too many.

**Clicking in a new message's fields works.** A click on To, Cc, Subject or
a button in Mail's compose window did nothing: the window's mouse handler,
there for selecting text, took every press and dropped the ones outside the
body. Now it passes them on.

## 0.99.29

**A calendars-and-contacts module.** `pim` reads and writes iCalendar and
vCard: every event and task with its times, people and reminders; every
contact with its addresses, phones and emails, from vCard 2.1 to 4.0. It
expands a recurring event into its occurrences over any window -- every
part of RRULE, with RDATE, EXDATE and moved occurrences -- and is checked
against RFC 5545's own examples, occurrence by occurrence, with
python-dateutil as the reference: all 46 agree. Times in a named zone go
through the system's zoneinfo; a zone only the calendar describes is read
from its own VTIMEZONE. It builds events (with a VTIMEZONE made from
zoneinfo), contacts, and the reply to an invitation.

This, with 0.99.28's additions to `dav`, is the foundation for calendar
and contacts on the desktop, which come next: contacts and Mail's address
completion, then the calendar and invitations.

## 0.99.28

**A mail account with a space in its name no longer ends the desktop.** An
account named "Home Email" was read, in two places, as arithmetic -- an
unquoted subscript -- and the error ended the desktop. Every name a person
chose is now quoted, and the suite's accounts have spaces in their names.

**One app's error no longer ends the desktop.** The shell has a new option,
`set -o keepgoing`: an arithmetic error, or nesting too deep, fails the
command it happened in instead of ending the script, as it already did
inside `try`. The desktop turns it on, so a slip in any app is written to
`desktop.log` and everything else carries on. Without it, a script behaves
as before, and as in bash.

**Also in this release, not yet used by an app:** the `dav` module learns
`propfind`, `report` and `sync` (WebDAV properties, reports such as CalDAV's
and CardDAV's, and RFC 6578 sync tokens), `put -t` for a content type (and
guesses `.ics` and `.vcf`), `rm -m` for If-Match, and lets a server's login
follow it to another host of the same domain over TLS -- iCloud sends its
calendars to a numbered host. These are the first part of calendar and
contacts.

## 0.99.27

**Restart Desktop and the new supervisor get on.** Restarting a desktop
that has terminals open made the restarted process a supervisor, which left
the terminals' programs as its children rather than the new desktop's, and
the desktop that should have come back did not draw. A restart that carries
running programs now keeps them with the desktop and runs without a
supervisor until the next fresh start; `tests/desktop.py` restarts into
that case and checks the program is still the desktop's own child.

## 0.99.26

**alt-left and alt-right go between workspaces.** They were Snap Left and
Snap Right, which move to ctrl-alt-left and ctrl-alt-right -- the keys the
workspaces had -- so the two swap. A window that uses alt-left and alt-right
itself keeps them while it has focus: the Browser's back and forward. A
saved settings file still holding the old defaults is moved over once
(settings version 3); a key someone chose is kept.

**Runaway recursion is an error, not a crash.** A function calling itself
for ever, a file sourcing itself, an `eval` evaluating itself: each is now
stopped before the stack runs out, with a message naming it, and the script
ends as it does for an arithmetic error. bash's `FUNCNEST` is honoured too.
This is what took the desktop down in 0.99.24; it would now have been an
error in the log.

**The desktop starts again by itself when it dies.** A held desktop has a
small supervisor: if the desktop dies on a signal or an error, it is started
again and its windows are put back where they were, from a snapshot kept
while you work, with a note saying so and the reason in `desktop.log`.
Terminals come back with a fresh shell. One that dies within ten seconds, or
three times in five minutes, is left stopped. SIGTERM and SIGHUP -- a
shutdown -- write the windows down and exit cleanly, and the next start
reopens them once.

**`exit` in a trap keeps its status.** `trap 'exit 143' TERM` exited 1, and
a bare `exit` in a trap did too: the earlier status was put back after the
trap. Both now behave as in bash.

## 0.99.25

**Adding a mail account no longer takes the desktop down.** Choosing Add
Account (or an account, or Signature) in Control Panel > Mail ended the
whole desktop at once, with nothing in its log. The dialog's opener was
named `mlad_open`, which is also the name the window manager calls when a
`mlad` window opens -- so it opened a window, which called it again, until
the shell ran out of stack. It is `mlad_show` now, and
`tests/540-examples.t` fails on any window whose `_open` opens another of
its own kind. `tests/mailapp.py` opens the dialog, is refused a server it
cannot work out, and saves a Gmail account.

The dialog is also a row taller, so its Gmail hint and an error from
saving are shown whole rather than cut off at the edge.

## 0.99.24

**Mail.** A mail app for the desktop, in the Internet folder, laid out the
way Gmail is: the views and labels down the left with their unread counts,
conversations on the right with a message and its replies together, and a
conversation opened in place -- HTML laid out with its headings, lists,
quotes, tables and links, attached pictures drawn, attachments saved with a
click. Gmail's keys work (`j` `k` `o` `u` `e` `#` `!` `s` `l` `v` `c` `r`
`a` `f` `/`, and `g` then a view), and the File, Message and Go menus have
them all. It is offline: a sync job keeps each account in a `db` file, what
you do happens at once and is queued for the server, and a dropped
connection loses nothing. New mail is pushed through IMAP IDLE. IMAP with
Gmail's labels and threads, POP3 (downloaded and kept here, flags this
machine's own, Trash deleting from the server) and SMTP. Control Panel >
Mail adds accounts -- for Gmail or Outlook an address and a password are
enough -- and sets how much is kept, how often to check, pictures and a
signature.

What it does not do yet: Google sign-in -- Gmail wants an app password,
made in the Google account's security settings; fetch pictures from the
web, which would tell a sender the message was opened; save a message being
written as a draft (closing one with text in it asks first); or keep more
than about 2000 messages quickly, because the shell's maps are lists. It
keeps 1000 by default -- the main folder that many, every other folder a
fifth -- and with 1000 opening takes 0.2 s and changing view 0.35 s.

**An HTML module.** `html` parses HTML as the WHATWG standard says a browser
must -- our own tokenizer and tree builder, passing all 1792 of html5lib's
tree-construction tests -- and answers CSS selector queries, text,
attributes and the tree, and lays a page out as lines of cells with a style
per character, link columns and boxes for pictures. An 813 KB page parses
in 159 ms. It is what Mail reads messages with, and the start of a browser
of our own; JavaScript, through QuickJS, is for a release of its own.

**An email module.** `email` speaks IMAP (IDLE, special-use folders,
Gmail's labels, threads and ids, MOVE or COPY and expunge), POP3 and SMTP
(STARTTLS or TLS, PLAIN or LOGIN, Bcc taken out), over the shell's TLS
relay, with accounts in a file only you can read that never shows its
passwords, and does the MIME: RFC 2047 and 2231 headers, quoted-printable,
base64, charsets into UTF-8 (iconv found at run time), parts out to files,
and messages built. `tests/mailserve.py` is a stand-in IMAP, POP3 and SMTP
server, and `tests/mail.py` and `tests/mailapp.py` drive the module and the
app against it.

**`:=` binds a map into a map.** `m["rows"] := db query "$h"` kept only the
scalar and dropped the rows; the result's map is now copied under the
target, at any depth, with its JSON types, and binding over a map frees the
old one.

**Smaller things.** `dt_keep` no longer lists a variable twice when two
files keep it.

## 0.99.23

**YouTube no longer freezes a minute in.** On many videos YouTube's own
player stopped about a minute in with "Something went wrong", and ours
played out what it already had and froze on the last picture. The cause
was the browser announcing itself: the web module's Chromium said
"HeadlessChrome" and set the automation flag, and YouTube treats such a
browser as a robot. It is now started without the automation flag and its
tabs give the browser's ordinary Chrome user agent (`HIBR_WEB_UA` to
choose another). Measured on the video that always froze: five runs in
five before, none of five after.

**And it recovers when YouTube's player stops anyway.** Every few seconds
the app checks YouTube's own player; if it has shown its error or fallen
back to unstarted, the video is loaded again and taken up where it was,
with a note saying so. A player that does not start within 25 seconds is
tried again too, where before it gave up at once; after three tries the
app says what went wrong. And it no longer waits for YouTube's page to
finish loading before starting its player -- that could take twenty
seconds, during which the video played under "starting" and then jumped
back to the beginning.

**The list's highlight is readable.** The chosen row in YouTube's list was
drawn in a colour no theme defines -- dark text on black. It is the
theme's selection now, like every other list, and so is the text on the
progress bar; a test fails if any pen names a colour no theme has.

## 0.99.22

**A login screen.** `login/login.hibr` logs people in on a text terminal,
in place of getty: a screen saver always behind it, and on a key a box
asking for a user name, then that person's picture, name and password, and
whether to start the Desktop or a Shell -- Tab changes it, and the choice
is remembered. When the session ends the screen comes back. Linux only;
root cannot log in there. The Debian package ships `hibr-login@.service`
and `/etc/pam.d/hibr-login` **switched off**: `systemctl enable --now
hibr-login@tty2` puts it on tty2, and `disable` gives getty back. Its own
settings (saver, title, a message, the PAM service, glyphs) are root's, in
`/etc/hibr/login.json`.

It runs as root, and gives that up only by forking children that drop for
good: one reads the person's About Me and decodes their picture, handing
back JSON and a small PPM, so root never reads a person's files or hands
their picture to an image library; another runs their session. The auth
module gains `auth open` (password, account, credentials, a PAM session on
the terminal), `auth run` (the command as the user, with PAM's
environment, recorded in utmp and wtmp) and `auth close`.

**About Me** -- Control Panel > About Me: your name, a line under it, a
picture and the session the login screen starts, kept in
`~/.config/hibr/me.json`. It is data, never run.

**`exec -a name`, `-l` and `-c`**, as in bash: the program's `$0`, a login
shell's leading dash, and no environment. A shell started from the login
screen is a login shell.

**`img -o ppm`** writes a picture as a binary PPM at the size it would be
drawn, and `img` reads PPM back with no library.

## 0.99.21

**Screen lock.** Lock Screen on the hibr menu, or ctrl-alt-l (changed in
Shortcuts like any other), puts the screen saver on and keeps the desktop
behind it until your password is given. A key or a click shows a box
over the saver with your name and a password field -- a letter typed at
the saver is already the password's first -- enter checks it, escape puts
the box away, and a wrong password holds the box for two seconds. Locked,
no key reaches the desktop: not Detach, not Quit, not a shortcut; a held
desktop attached to while locked is still locked. Control Panel > Screen
Saver > Lock After locks on its own some minutes after the saver starts
("at once", or never, the default); Preview never locks.

The password is checked through PAM by a new module, `auth` (`auth check
[-s service] [-c dir] user password`), with libpam opened on first use
and no privilege: checking your own password is what PAM's own helper is
for, and hibr is never setuid. The Debian package installs
`/etc/pam.d/hibr` (the system's usual rules); without it the lock asks
`login`, or `screensaver` on macOS. With no PAM, Lock Screen says so and
does not lock. It locks this desktop, not the machine: another terminal,
console or login on the same machine is still open to whoever reaches it.

**The screen saver no longer takes a whole core over a busy terminal.**
While a saver ran nothing read the terminals' ptys, so a program writing
in a terminal behind it woke every wait at once and the saver redrew as
fast as it could: 100% of a core, measured; now 1%. Each window's idle
callback reads its pty while the saver runs, as it does for a hidden
window.

## 0.99.20

**Screen savers.** After ten idle minutes the screen is given to a screen
saver until the next key or click, which ends it and goes nowhere else.
Six to start: Matrix rain, Flying Toasters, a Classic Mac drifting about
with its little screen going from the happy face to a desktop to a window,
Pipes, Mystify and a big Clock that moves each minute. Control Panel >
Screen Saver chooses one or none at random, the idle minutes (or never),
and previews; the hibr menu's Screen Saver starts one now. Each saver is
a file in `savers/` -- a `_start` and a `_frame` -- and one of your own goes
in `~/.config/hibr/savers/`, listed beside them; `savers/saver.hibr` runs
any of them on its own in a terminal, by name or by path. Waiting costs nothing: the
desktop sleeps until the idle time is up. This is the first part of the
login and lock screens, which will run a saver behind them.

## 0.99.19

**Switches, rounded frames, diamonds and dashes.** An on/off choice can be
drawn as a switch: Control Panel > Appearance > Checkboxes is `box` (as
before), `knob` -- a knob that slides right when on, `(  ●)` and `(●  )`
-- or `block`, a block that does the same between brackets, `[  █]` and
`[█  ]`, each in the theme's colours: on in the accent, off dimmed. It is
used everywhere there is one: the Control Panel's rows and every dialog's
boxes. Left at `theme`, the theme decides -- a theme's file may suggest a
style with `"checks"`, and neon suggests the knob. Windows > Frame gains
`rounded`, with arcs at the corners, and the title-bar buttons two styles,
`diamonds` and `dashes`, coloured like circles and squares. Get Info's
permission boxes sit six columns apart now, to fit any style.

## 0.99.18

**A terminal's shell closes its window however it ends.** ctrl-d (or
`exit`) in a terminal closed the window only when the shell's status was
0 -- and a shell's status is its last command's, so after a command that
failed, ctrl-d left "exited 127 -- close this window" on a window closed
on purpose. A shell's window -- a plain terminal running a shell, or
Terminal Here -- now closes whatever it ended with. A program run in a
terminal is unchanged: it closes on success and stays to show why it
failed. A plain terminal counts as a shell when `TW_CMD` is one on its
own (hibr, bash, zsh, sh, dash, ksh, fish, tcsh, csh).

Control Panel > Terminal > **Close When a Program Ends** chooses what a
program's window does: `success` (the default, as before), `always`, or
`never`, which keeps even a successful program's window open to say so.

## 0.99.17

**JPEG pictures.** The image module decodes JPEG as well as PNG, through
libturbojpeg, loaded at run time the way libpng already is -- so the Image
Viewer opens `.jpg` and `.jpeg`, the Wallpaper picker offers them, and a
JPEG can be the desktop's wallpaper in every mode. A file is decoded by
what its first bytes say it is, not by its name. A photo's EXIF
orientation is honoured, so a picture a phone stored on its side shows
the right way up. `img` also writes its pictures in order with anything
printed before them now, where in a pipe a line printed first could come
out after. On macOS this needs Homebrew's `jpeg-turbo`, which the formula
now depends on; on Debian and Ubuntu it is `libturbojpeg0`.

## 0.99.16

**YouTube: playlists, channels, history and favourites.** A search lists
playlists and channels as well as videos, read in every form YouTube draws
them today; enter opens one -- its videos under its own title -- and
escape goes back to the list before. A playlist's, a channel's or a
video's address typed or pasted in opens it. `n` and `p` play the next
video in the list and the one before, and at the end of one the next plays
by itself (Play > Play Next Automatically, or Control Panel > YouTube).
What was watched is kept in a history (`h`) with where it was left, and
choosing it again takes it up there; `f` keeps a video, a playlist or a
channel as a favourite, starred wherever it is listed, and `v` lists them.
Both are files of their own under `~/.local/share/hibr/youtube/`; Control
Panel > YouTube has Clear History. This finishes #27.

**Fixed: a video could start with no picture.** When a page starts a new
media source -- every video, and every ad -- the app restarted its player
after taking the new stream's first bytes, and the restart could throw
away the init segment those began with, leaving the player waiting for
one that never came. `web take` now stops at a reset, so the player is
started afresh before any of the new stream arrives. A video's end is also
found from YouTube's own length, not only the stream's.

## 0.99.15

**YouTube, in a window.** The YouTube app, in the Internet folder,
searches YouTube and plays what it finds: type, enter, choose, enter.
Space pauses, the arrows seek and change the volume, `m` changes the
picture between half blocks, ASCII and plain, a click on the bar goes
there. Nothing here is a YouTube client of its own: YouTube's own page
runs in the web module's headless Chromium, its player asked for the
lowest quality and muted, and every chunk it plays is copied into a media
player here and decoded with FFmpeg's libraries -- the page fetches, hibr
watches. Search reads the results page's own data the same way. Control
Panel > YouTube keeps the picture, the quality and the frame rate. Ads
play as YouTube plays them.

Two module pieces make it, each usable on its own: `web tap`, `web take`
and `web tapseek` copy what any page hands its media source, video and
audio apart; `media feed` and `media pipe` make a player whose streams
arrive on pipes. The Browser no longer stops Chromium while a YouTube
window still uses it. This is #27's second part; playlists, channels,
history and favourites come next.

## 0.99.14

**Video and sound.** The `media` module plays video and sound from a file
or an address -- MP4, WebM, MKV, MP3, HLS, http -- with FFmpeg's own
libraries loaded at run time, the way hibr already loads libssl: nothing
linked, no headers to build, and no ffmpeg or mpv process. A decoding
thread runs a few seconds ahead, a sound thread feeds the device (ALSA on
Linux, AudioQueue on macOS) and keeps the clock, and the picture follows
it, drawn as coloured half blocks, or as ASCII in colour or plain. Seek,
pause, volume, a frame-rate cap and a colour detail setting for slow
terminals; `media info` says where it is. FFmpeg 5.1 to 8 are known, the
few structure fields read located per release from that release's own
headers (`tools/mvoffsets.sh`); anything else is refused by name.

`examples/play.hibr` is a terminal player built on it. This is the first
part of the YouTube player (#27); the desktop app comes next.

## 0.99.13

**Folders on servers, in Files.** Files reaches any WebDAV server as
though it were a folder here: **Servers > Connect to Server…** (or Control
Panel > Network Servers > Add Server…) takes a name, an address, a user, a
password and whether to check the certificate, and **Test** tries them
before anything is saved. Each server is then on the Servers menu and
shows as `NAME:/` in the title. Enter, backspace, rename, New Folder, Get
Info (size, time, type, ETag), drag, Copy and Paste all work there, to and
from local folders and between servers, never over something already
there; transfers run in the background with a note at each end, so a
large file never stops the desktop. Delete asks first, since a server has
no trash.

**Opening a file on a server** fetches a copy into `~/.cache/hibr/dav` and
opens that. With **Upload Edits on Save** on, saving the copy puts it back
over the version it was fetched as -- and if the server's copy changed in
the meantime, the edit goes up beside it as `name (conflict DATE).ext`, so
nobody's work is overwritten. Control Panel > Network Servers lists the
servers, each opening the dialog that changes it, and keeps the upload
switch, a timeout and Clear Cache. Passwords are never shown. Files also
gains **New Folder** for local folders.

Two shell fixes came out of it. `-nt` and `-ot` compare times to the
nanosecond, as bash does, so two changes in one second are told apart.
And `&` inside a function under `strict vars` no longer fails: it sets
`$!`, a special parameter, which the check had taken for a global the
function created.

## 0.99.12

**A WebDAV client.** The `dav` module lists, fetches and changes files on
any WebDAV server -- Nextcloud, ownCloud, a NAS, Apache or nginx DAV,
rclone -- with no curl and no libraries: its own HTTP/1.1 keeps a
connection per server and reuses it, redialling one the server dropped
while idle; replies may be chunked; redirects are followed; a login is
Basic or Digest, whichever the server asks for; TLS goes through the
shell's own relay, with a per-server switch for a NAS that signs its own
certificate. `dav ls`, `stat`, `get` and `put` (each `-r` for a whole
folder), `mkdir`, `rm`, `mv` and `cp`; `ls` and `stat` give maps with
`:=`, and `$DAV_CODE` the last status. `put -m ETAG` uploads only over the
version it was given and `put -n` only where nothing is, checked before
sending as well as asked of the server, since some servers ignore the
headers. A name in a listing that could step outside its folder is never
listed or written. Servers are set up with `dav server set`, kept in
`~/.config/hibr/dav` with mode 600 -- a list anyone else can read is
refused -- and `dav servers` never shows a password.

This is the first half of WebDAV in Files; the window comes next.

## 0.99.11

**A web browser.** The `web` module runs Chromium (or Chrome) headless and
drives it over its own DevTools protocol on a pipe -- no port, no
WebSocket -- turning each page into cells: the page's text placed where the
browser laid it out, in its colours and weights, over a half-block picture
of its backgrounds and images, taken with the text made transparent so the
two never fight. Links carry their addresses; wide characters take two
cells. Scripts get it too: `web open`, `web text`, `web links` (a map with
`:=`), `web eval`, `web click`, `web type`, `web key`, history and tabs.

**Browser,** in a new Internet folder on the hibr menu, is built on it:
tabs (click to switch, x to close, + for another), back, forward, reload
and an address bar that goes to an address or searches for anything else;
links followed with a click, fields typed into, the wheel scrolling, alt-
left and alt-right through history; bookmarks kept in a file of their own;
every tab back after a restart. Without Chromium installed it says what is
missing. This is ticket #38.

The image module's PNG decoder also reads from memory now (`im_pngmem`).

## 0.99.10

**Sheet formats its cells.** A Format menu works on the selection: **bold**
and *italic*; a text colour and a fill from the theme's own roles, so a
sheet follows the theme; alignment left, centre or right (numbers right
and text left unless told); a number's decimals, thousands separators,
percent and currency (`SS_CURRENCY`, `$` unless set); a bottom or right
border; a rule that colours a number by its value -- Negatives Bad,
Positives Good, or Rule... for anything like `> 100 good`; and frozen rows
and columns that stay while the rest scrolls. A format is kept with its
cell in the file, and Undo takes it back like any change.

## 0.99.9

**Standby displays.** `desktop --standby --name NAME` is a terminal that
waits to be a screen of the desktop: a quiet screen with its name until the
desktop runs, then it joins on its own; let go from the desktop (Detach),
it waits again -- marked *let go*, so it is not pulled straight back -- and
when the desktop quits it waits for the next. Control Panel > Displays has
a **Standby** line listing the ones waiting (a click asks one to join) and
**Standby displays join on their own**, on unless switched off, for
whether they come by themselves or only when asked. `q` stops waiting.

**Blank a display.** Right-click it in the Displays pane: it stays joined
and keeps its place in the arrangement, but goes dark and out of use --
windows on it move to the primary, none are placed there -- until it is
unblanked. Kept across restarts. The primary cannot be blanked.

## 0.99.8

**Sheet, a spreadsheet whose formulas are hibr,** in Office. A cell holds
text, a number, or `=` and hibr -- what a formula prints is its value
(`=math "A1 * 1.2"`, `=sum "${B1_B9[@]}"`), and one that is an expansion
is that (`=$((A1 * 2))`, `="${A1} each"`). Every cell a formula names is a
variable holding its value and a range `A1_B9` an array of them; formulas
are worked out in the order they need each other, and one that needs
itself says `#CYCLE`.

Formulas run in a hibr of their own under `--plan`, with `math` loaded,
two seconds of CPU and ten of the clock: they compute and read what you
can read, and a formula that would write, connect or start a program says
`#REFUSED` and what it would have done; one that loops says `#TIME`.
**Sheet > Trust This Sheet** asks, then lets one sheet's formulas run for
real -- remembered on this machine, never inside the sheet, so a sheet
cannot arrive trusted.

A sheet is a `db` file (`.hsheet`), one row per cell, written as it
changes: there is nothing to save, and Save As copies it. Typing replaces
a cell, enter or f2 edits it, shift and the arrows select, delete clears;
a column's edge in the header drags its width; the Sheet menu inserts and
deletes rows and columns -- formulas follow the cells they name -- and
adds more of either. Copy, cut and paste are tab-separated; CSV comes in
and goes out through File. Undo and Redo cover the session.

**Assignments in one command are made left to right,** as in bash:
`x=1 y=$x` sets `y` to 1, and `p=/a/b d=${p%/*}` sees `p`. hibr expanded
every assignment before making any, so the later ones saw the old values.
The command's own words are still expanded first, so `x=1 echo $x` prints
nothing, as in bash.

## 0.99.7

**Floating point: the `math` module.** `$(( ))` has only integers, as in
bash; `math` has the rest -- `+ - * / % ^`, comparisons, `c ? a : b`,
`round(x, n)`, `sqrt`, `abs`, `pow`, `ln`, `log`, `sin` and the like, `pi`
and `e`, shell variables by name, and `sum avg min max count` over arrays.
It prints a number the way a spreadsheet shows one: a whole number whole,
anything else to at most fifteen significant digits, so `math 0.1+0.2` is
0.3; `-s n` gives exactly n decimals, rounding half away from zero.

**A plan may load a module that only computes.** `--plan` refused every
`mod load` and `need`, since a module's builtins can do anything; `math`
and `md` are now let through by name -- never by path -- and found on the
module path, never in the current folder. Finding a module means opening
it, which runs its code, so the folder a planned script sits in must not
be able to supply one; the same now holds for any plan's search, as it
always did for root's. The spreadsheet's formulas run this way.

## 0.99.6

**The db module changes and deletes rows.** `db set h N col val...` changes
a record by the number `query -n` gives it; `db update h where ... set col
val...` changes every row that matches; `db delete h N...` or `db delete h
where ...` deletes (a `where` is required -- nothing deletes every row by
accident); `db compact h` writes the file again without its deleted rows,
the one thing that renumbers. Values are written where they are, zone
maps widened when a value moves outside them, and a deleted row is marked
in its group and passed over by every query, count and aggregate.

A database made before this (`HIBRDB1`, dBASE's included) reads and updates
as it was, and its first delete writes it again beside itself with the
marks, keeping every record's number. This is the ground the spreadsheet
stands on.

## 0.99.5

**Files has a search box,** on the right of the path. `/`, ctrl-f, View >
Search… or a click puts the keyboard in it, and the list narrows to the
names that hold what is typed, whatever its case. Enter keeps the filter
and goes back to the list; escape clears it; another folder starts clear.
A paste while it is open goes into it.

## 0.99.4

**The Control Panel has a search.** It sits at the top of the list: type
while the list has the keyboard, and only the panes that match are shown
-- by title, or by any row inside them, so "wall" finds Appearance through
its Wallpaper rows. The arrows move among what was found, escape clears
it, and a key the desktop holds still goes to the desktop.

## 0.99.3

**Notifications say who they are from.** The window manager marks the
window whose app it is handling -- a key, a click, a menu item, a frame --
and a note made meanwhile is from that app; the desktop's own are from
Desktop, and a terminal program's are from its title. The sender is on the
note's top border and in its own column in Notifications.

**Priorities mean what they say.** Feedback on what you just did -- Record
4 added, Wallpaper set, Not a valid name -- is low: it pops up and is not
counted. A failure -- could not rename, cannot write -- is high.

**Open Terminal Here closes on exit,** as any terminal does, instead of
staying up to say it exited with 0. And a window that closes itself is
taken off the screen at once, not at the next key.

**The Control Panel's list sits inside its window,** its highlight a
column in from the border and the divider, reaching the bottom border, and
the window opens larger (26 by 70). A setting's label too long for the pane
ends in an ellipsis instead of running into its control.

## 0.99.2

**Markdown, complete: the `md` module.** CommonMark with GitHub's
extensions -- tables, strikethrough, task lists, extended autolinks, the tag
filter -- parsed in C the way cmark does it. Every example in both specs
passes, compared byte for byte: 652 of 652 CommonMark 0.31.2, 24 of 24 GFM
extensions (`tests/md_spec.py`, in `tests/all.py`). `md html` writes HTML as
cmark-gfm does; `md lines` gives each line a style letter per character, for
a program that draws markdown itself. Nesting is bounded, every
pathological input runs in linear time, and it is clean under ASan and
UBSan after fuzzing.

**Write is built on it.** What was a line-by-line guess is now the real
document: setext headings, emphasis by the spec's rules, nested lists that
keep their indent, quotes in quotes, tables with their rules, reference
links, autolinks, multi-backtick code, entities, inline HTML. The selection
is drawn exactly over what is selected, and Export to HTML is `md html`.

**A split window's divider drags** -- a new widget, `widgets/split.hibr`.
The Control Panel's list and the file dialog's list and preview use it, and
where the divider is left is kept.

**The terminal's scrollbar** runs from its first row to its last; it began
in the title bar and stopped a row short.

**The desktop's menu:** Change Wallpaper… opens the Control Panel at
Appearance, Next Wallpaper steps through them as before, and Refresh
Desktop draws the whole screen again and looks again at what is on disk.

**The prompt** no longer takes an empty `.git` directory for a repository;
git does not either.

## 0.99.1

**One Open, Save As and Export dialog for everything.** A folder's
contents, folders first, filtered to the app's kinds of file (and All
files), with a preview of what is selected; Save As adds the extension and
asks before replacing. Write's Open…, Save As… and a new Export… (HTML, or
plain text as it reads), and dBASE's New Database… and Open Database…, all
use it; `dt_filepick` gives it to any app.

**Stickies have no frame.** A new window style, `bare`: the window's own
colour, a strip across the top a shade darker with a close box -- drag it
to move -- and a grow mark, and no shadow. Stickies use it.

**Every glyph is named again.** Write, Stickies, the Clipboard and the file
dialog had written •, ☐, ☑, ★, ⏎ and … straight into the code; they are in
`GL` now, with ASCII stand-ins, and `tests/540-examples.t` fails on the
next one. A one-line field takes ctrl-u and ctrl-k.

## 0.99

**Write, a word processor for markdown,** in Office. Markdown is shown as it
reads -- headings, bold, italic, strikethrough, code, links, bulleted,
numbered and task lists, quotes, rules, fenced code -- and only the cursor's
line shows its marks, dimmed, to edit exactly, as Typora does. A toolbar and
a Format menu add the marks to the selection or the line; a click on a
task's box ticks it. File > New, Open…, Save (ctrl-s), Save As…; Find… and
Find Next; undo, redo, cut, copy, paste and select all. Plain text files
are edited plain. Files opens `.md` and `.txt` in it.

**An app can keep a window open:** `_canclose` is asked by the close
button, Close and the Close Window key -- Write uses it to ask before
losing unsaved changes. **Save** (ctrl-s) is a desktop key that goes only
to a window with `_savefile`, so a terminal still gets ctrl-s. A file's
double-click uses its type's first Open With app.

## 0.98

**Stickies replace Note Pad.** Notes stuck on the desktop, as many as you
like, each yellow, blue, green, pink, purple or grey, saved as you type with
their place and size. Launching Stickies opens every note; the Note menu
makes a new one, changes its colour, or deletes it after asking. Text wraps
at the note's width, and its first line is its title. Control Panel >
Stickies sets new notes' colour and whether the notes open with the
desktop. Note Pad's note becomes the first sticky.

**Undo, Redo and Select All** are on the Edit menu, undo and redo on alt-z
and alt-y -- settable, like Copy -- through each window's `_undo`, `_redo`
and `_selall`.

**`widgets/textarea.hibr`**, a multi-line editor any app can use: cursor,
selection with shift and the mouse, word moves with ctrl, cut, copy,
paste, undo and redo, and soft wrap.

**`$(< file)`** gives the file's contents, as in bash, without a fork; it
gave nothing. And `dt_atstart` lets an app run something as the desktop
starts.

## 0.97

**The Clipboard.** Everything copied -- in any window, pasted from the
machine, or set by a program in a terminal -- is kept, and the Clipboard
desk accessory lists it newest first: Enter puts one back on the
clipboard, p pins it so it is never dropped, delete removes it, and a
paste into the Clipboard keeps that too. The last 50 are kept between
desktops in a file only you can read; Control Panel > Clipboard sets how
many, or none on disk, and clears it. Every clipboard change goes through
`dt_clipset`.

**A restart that cannot read its saved desktop says why** -- in
desktop.log, with the parser's own message -- and keeps the file as
`state.json.bad`, saying separately when it was saved by another version.

## 0.96

**Open With, as on a Mac.** Right-click a file in Files: Open With lists
every app that can open it, the default first, then Other… to type a
command for it; a choice opens it there once and is not remembered.
Control Panel > File Types gives every type an Open With row -- tick what
it lists, choose its default, which is what a double-click does. Apps
declare what they open with `dt_opener`.

**Open Terminal Here** on Files' right-click starts a terminal in the folder.
Control Panel > Files can take either item off the menu and chooses the
shell it runs.

**About hibr Desktop** (renamed) shows the module ABI, the uptime and who is
logged in, each user with their number of sessions. **Screenshot…** moves to
Desk Accessories, beside the Image Viewer that shows what it took.

**`cd` takes `-L`, `-P` and `--`**, as in bash; it refused all three.

## 0.95

**Screenshots show up and open.** A screenshot appears at once in a Files
window open on its folder -- it waited for the folder to be left and
entered again. An `.ans` screenshot opens in the Image Viewer, double-clicked
in Files or from the note that announces it: the viewer plays it into a
terminal emulator of its own and draws those cells, colours and wide
characters exactly, in a window sized to the shot.

## 0.94

**Copy and paste reach everything.** Every dialog's text field -- Rename,
Get Info, File Type, Clock Format, Set Date & Time, Time Zone, Screenshot
Folder -- takes copy (the whole field), cut (which empties it) and paste
(at the cursor, on one line), with the shortcuts, the Edit menu and a
paste from the real terminal alike. dBASE copies and cuts the line being
typed. Task Manager copies the selected process, Process Details and About
what they show, Notifications the selected note (cut clears it), and Clock
the time. `dt_textpaste` is the one-line paste every field uses.

**The Control Panel scrolls on both sides, each on its own.** The list of
panes has a scrollbar and its own scrolling, so it no longer stops at the
window's height; the pane beside it has its own, which shows when a pane is
longer than the window -- Shortcuts, where Screenshot sat out of sight.
The wheel scrolls whichever side the pointer is over.

## 0.93

**Screenshots.** ctrl-alt-g (settable, as Screenshot) or the hibr menu's
Screenshot… asks Screen, Window or Area -- an area dragged out with the
mouse, or drawn with the arrows and enter -- the last one taken already
chosen, so the shortcut and enter repeat it. The picture is the screen's
cells: ANSI text by default, which `cat` shows as it was, or HTML or plain
text, into `~/Pictures/hibr`, set in Control Panel > Desktop. The chooser,
the band and any notes are kept out of it.

**`console shot`** writes what is on screen, or a rectangle of it, as ANSI,
HTML or text.

## 0.92

**Notifications have a priority, and the count means something again.**
Low, normal, high, urgent. The desktop's own chatter -- switching or moving
to a workspace, tiling, Copied and Cut, keyboard move and resize, shortcut
capture, the Control Strip -- is low; a program's notification and anything
an app reports is normal; a failed restart is high. Every note still pops
up (high in the warning colour, urgent in the error colour, low dimmed) and
goes to the history, but only notes at Control Panel > Notifications >
Count From (normal by default) or above raise the bar's count.

The history shows each note's time and priority. Up and down select, delete
clears the selected one, and a History menu clears the low ones or all of
them. `dt_notep` and `dt_notifyp` post at a priority.

## 0.91

**Cycle through every workspace.** Control Panel > Desktop > Cycle Windows
On: `workspace`, the default, goes through this workspace's windows as
before; `all` goes through every workspace's -- from this one on and round,
each one's windows in the order they were opened, minimised ones passed
over -- switching to whichever workspace holds the next window. Both alt-tab
and Window > Cycle follow it.

## 0.90

**A minimised terminal no longer holds a core.** A terminal window's
output was read only while it was drawn, so one minimised or on another
workspace whose program kept writing -- `screen -r`, a build, a clock --
left its pty readable: the desktop woke at once, drew nothing that read
it, and went round again at 100% of a core. Windows that are not drawn now
have their app's `_idle` called each frame, and the terminal reads its
program there; a minimised one costs nothing again, and its screen is
current when it comes back. A Restart Desktop made it likely, by bringing
back a busy terminal minimised or on another workspace, but any of these
did it.

**The wheel flips workspaces.** Over the bare desktop or the menu bar, the
wheel goes to the next workspace (down) or the previous (up), wrapping
round; a burst of reports from one notch, or a trackpad, is one step.

**A title bar's menu moves the window to another workspace.** Right-click a
title: Move to Workspace, beside Hide. Move to Display is there only when
another display is attached -- it used to appear with just the one.

## 0.89

**Restart Desktop keeps your sessions.** On the hibr menu: the desktop
replaces itself with the hibr installed now, in the same process, and
carries on -- every window in its place, workspace, stacking and focus, and
tiling as they were. A terminal's program never stops: the same shell, with
its variables and jobs, and its screen and scrollback come back. Note Pad,
Calculator, Puzzle, Mines, Snake, Bricks, Files, Image Viewer and dBASE keep
their state; a game in play comes back paused. When apt or brew installs a
newer hibr under a running desktop, a note says so, and clicking it
restarts (Linux; it reads `/proc`). No screen or tmux needed around the
terminals any more. The update check rides the clock's once-a-minute wake,
so an idle desktop draws no more frames than before.

An app keeps state with `dt_keepstate app MAP...`, and `<app>_stash` /
`<app>_resume` for what a map cannot hold.

**`pty adopt` and `pty release`; `term adopt`, `term save` and `term pid`.**
A pty survives `exec` -- its master is not close-on-exec and the program
stays the process's child -- and the new image takes it over with its
screen, scrollback, cursor and modes.

**`json parse` into a subscript** -- `json parse m[3] "$doc"` replaces just
that entry.

**`export -n`** takes a name out of the environment, as in bash. hibr took
`-n` for a variable name and exported it.

## 0.88

**dBASE's menus are System 7's.** File (New Database, Open Database, Close
Database, Directory, Quit dBASE), Edit, Records (Append, Browse, Display
Structure), Query (List, Count, Sum, Average), Help -- in place of dBASE
III's own Assistant names, which nobody reaches for any more. dBASE moves
to a new Office folder, a submenu of the hibr menu.

**An app's File menu comes before Edit.** An app that calls `dt_editmenu`
after declaring its File menu gets the desktop's Edit there, as on a Mac;
one that does not still has it after its own menus. dBASE and Files do.

## 0.87

**dBASE, a desktop app.** A little dBASE III on the `db` module: the dot
prompt (`CREATE`, `USE`, `APPEND`, `BROWSE`, `LIST FOR load > 2.5 .AND.
host = 'web1'`, `COUNT`, `SUM`, `AVERAGE`, `DISPLAY STRUCTURE`, `?`,
`DIR`), an `APPEND` form, a scrolling `BROWSE`, and the Assistant's menus
typing the same commands. Records are only appended, as `db` has no update.

**`"$*"` and `"${a[*]}"` join with IFS's first character**, as in bash, and
`"${!a[*]}"` and the `[*]` slices with them. Every one of them joined with
a space whatever IFS said, so `local IFS=:; echo "${a[*]}"` printed spaces,
and a quoted `"${a[*]:0:2}"` came out as separate words.

**Bracket patterns know the POSIX classes.** `[[:space:]]`, `[[:alpha:]]`
and the other ten matched nothing in `case`, `[[ == ]]`, `${x#...}` or a
glob; they work now, alongside escaped members in a bracket.

**`db query -n` and `from n`.** `-n` gives each row's record number, from
1, first (`r[i]["#"]` in a map); `from n` starts at record n, skipping the
groups before it unread -- what a browser needs to page through a file.

**The bar gives way to long menus.** An app with many menus no longer has
the workspace numbers, the bell or the clock drawn over its titles; they
step aside until there is room.

## 0.86

**`db`, a small column store module.** `db create stats.db ts:int
host:str:16 load:float` makes a database of typed columns (int, float,
fixed-width str) in one file; `db insert` and `db import` (tab-separated)
append rows; `db query h where load gt 2.5 and host eq web1 limit 10`
prints the matching rows, or with `r := db query ...` gives them as a map
with numbers kept as numbers; `db count|sum|min|max|avg` aggregate. Rows
live in groups of 1024 with a zone map of each column's least and greatest
value, so a filter skips any group that cannot match without reading it.
A million rows import in under 0.4 s and a full-scan count takes about
30 ms. No SQL, no update or delete, one writer, no crash recovery -- see
`mods/db/README.md`.

**A map copied with `:=` keeps its JSON types.** `r := f`, where `f` left
a JSON document in `$RET`, gave back every number and boolean as a string.

## 0.85

**Every colour follows the theme.** 87 colours in the desktop were written
as midnight's values -- Files' selection, Note Pad's cursor, the
calculator's keys and answers, dim labels, the traffic-light buttons, exit
statuses, Mines' cells -- so on paper they were dark-theme greys on a light
face. A theme may now set seven colour roles beside its seven colours: dim,
selink (text on a selection), good, warn, bad, info and well (the surface
keys and cells sit in), each optional and defaulting to what the dark
themes share, so a theme file with only the seven colours still works.
Paper, phosphor and amber set their own; the others look as they did. A
game's own art -- the bricks, Snake's board -- stays as drawn.

## 0.84

**Tiling.** Window > Tile Workspace has the desktop lay out the current
workspace's windows itself: the main one on the left (Control Panel >
Windows > Tiled Main, 30 to 70 per cent), the rest stacked down the right,
laid out again as windows open, close, hide or move between workspaces,
and when the screen changes size. Make Main puts the focused window on
the left; a tiled window dragged onto another swaps places with it, and
anywhere else goes back. Each workspace is tiled or not on its own; a
fixed window floats over the layout; resizing a tiled window by hand is
refused with a note. Tile Workspace and Make Main are shortcuts with no
key until given one. With 0.81 to 0.83 this completes placement, snapping,
workspaces and tiling.

## 0.83

**Workspaces.** Three by default, for the whole desktop -- Control Panel >
Desktop > Workspaces sets 1 to 9. The bar shows the numbers with the
current one lit: click one to switch, or drag a window by its title onto
one to send it there. alt-1 to alt-3 switch, ctrl-alt-right and
ctrl-alt-left step, Window > Move to Workspace sends the focused window --
every key a shortcut you can change. A hidden workspace's windows have no
pane, so they cost nothing; each comes back stacked as it was left.
Opening an app that is open on another workspace goes there rather than
opening a second; new windows are placed as if the other workspaces' were
not there; the application menu lists every window with the number of the
workspace it is on. Fewer workspaces moves the windows of the ones that go
onto the last that stays.

## 0.82

**Snapping.** alt with an arrow puts the focused window on the left,
right, top or bottom half of the display, below the menu bar; the same
again puts it back where it was. Window > Snap has the four and Center.
Each is a shortcut in Control Panel > Shortcuts -- Center has no key until
given one. A fixed window can only be centred. With Shortcuts in Terminals
on, alt with an arrow snaps from inside a terminal too, as any chord the
desktop holds does; clear the shortcut, or Pass Every Key on that window,
to give it to the program.

## 0.81

**A new window goes somewhere sensible.** Every launched window used to
step down and across from the top left whatever was open, so the third
covered the first two. Now it goes in the first spot on the display that
overlaps nothing -- below the menu bar, clear of the Control Strip, with
room for each window's shadow -- and where there is none, where it covers
least; a window bigger than the display is shrunk to fit. Control Panel >
Windows > Placement: smart (the default), cascade (as before) or center.
An app can say its size as it opens with `<app>_size`.

**A terminal opens at 24 rows by 80 columns** -- the classic size, where
it was 12 by 50 -- shrunk to fit a smaller screen. Control Panel >
Terminal > Rows and Columns change it.

## 0.80

**The arrows belong to your apps again.** Since 0.77 a shortcut could be
set to any key, and the Shortcuts pane takes the very next key after a row
is activated -- so the arrow pressed to move to the next row became that
row's shortcut. A Menu Bar on the right arrow opened the menu bar from
every window, and the open menu then took the other arrows: no arrow
reached any app. Now:

- An arrow, enter, tab, space, backspace, delete, home, end or a page key
  pressed during a capture ends it unchanged ("Not changed") and moves on
  as it would have. With a modifier -- alt-left, ctrl-enter -- each is
  still a shortcut you can set.
- Settings saved by 0.77 to 0.79 with a shortcut on one of those keys have
  it put back to its default when the desktop starts, and entries for
  actions that no longer exist are dropped.
- Moving or resizing a window from the keyboard, a file drag, and the
  Control Strip's arrow mode each end on a click or on any key they have
  no use for, which then goes where it would have. Each used to swallow
  every key until its one way out, with a note that had already gone.

## 0.79

**The desktop's glyphs have names, and an ASCII set.** Every character the
desktop draws that is not plain text is named in `wm/glyphs.hibr` --
`${GL[vline]}` in the code, not a literal `│` -- written as its code point
with its Unicode name beside it. Appearance > Glyphs switches the whole
desktop to plain ASCII for a terminal or a font without box drawing. An
app's or pane's own icon stays as written on its registration line.

**`$'\u2502'` and `$'\U0001F514'` work**, as in bash: hibr dropped the
backslash and kept the digits. Found because the glyph table wanted them.

**Date & Time is under Hardware** -- it sets the machine's clock and time
zone. **The scrollbar follows the theme**: its thumb in the theme's accent,
its track in the idle colour, where it was always midnight's blue.

**`tests/750-pty.t` stops flaking**: two of its checks counted matching
lines, and how many arrive in one read is timing; each asks whether the
thing appeared, reading until it has.

## 0.78

**A held session rings, notifies, names its tab and keeps its links.**
hold redraws each attached terminal from an emulator of its own, and
anything that is not drawn text died there: the bell, notifications (OSC 9
and 777), the window title (OSC 0 and 2) and clickable links (OSC 8) --
checked against the same program run unheld, where every one arrives.
Each reaches every attached terminal now: the bell and notifications as
the program sent them, the title whenever it changes and to a terminal as
it attaches, and a link per character, so it stays clickable.

**The desktop does the same for a program in a Terminal window.** Its
notifications become desktop notifications -- clicking one brings the
window up -- and are sent on to your real terminal; a bell rings the real
terminal and marks the window's title with a dot until you click it; a link
it prints is a link on the real terminal. `console link`, `console bell` and
`console notify` do the same for a script, and the display interface
(version 4) carries links, so any tool drawing through it can make one.

## 0.77

**Every key the desktop acts on is a setting.** The menu bar's F10 and
escape, Copy, Cut and Paste (alt-c, alt-x, alt-v), Select All Icons
(ctrl-a) and the terminal's page back and on (shift-pageup and
shift-pagedown) were fixed in code; each is now an action in Control
Panel > Shortcuts like Close Window or Cycle Windows -- changed or cleared
there, and the change applies at once in every window and on the Edit
menu, which shows each one's current key. No key is reserved any more:
taking one another action has asks first and moves it. What stays fixed
is how an open menu or a dialog is driven -- the arrows, enter, escape,
y and n.

**The Keyboard pane is two settings and nothing to decipher.** Shortcuts
in Terminals (was Desktop Shortcuts Win): the desktop's shortcuts work
while a terminal has focus. Terminals Keep Ctrl+A-Z: ctrl with a letter
always reaches the program in a terminal -- ctrl-c, a shell's ctrl-w --
even where it is a shortcut; it was a fixed rule, and is now a setting,
on by default. The lines of text that explained the old fixed keys are
gone, since there are no fixed keys left to explain.

## 0.76

**The desktop costs half as much sitting idle.** Measured over the same
four windows (a terminal, the clock, Files and Note Pad): 0.57% of a core
idle before, 0.31% now, and a 30-report window drag 32 ms of CPU against
39. Two things were most of an idle frame, and neither was a window:

- **Every console write asked the kernel for the terminal's size**, so
  filling the wallpaper was 1920 system calls a frame. Writes now use the
  size found when the frame was last flushed, unless a resize has arrived;
  the fill went from 0.80 ms to 0.11.
- **The menus were rebuilt on every frame**, including the clock's once a
  second, though only input or a change of focus can alter a shut menu
  bar. A frame a timer asked for reuses the last build.

Redrawing only the windows that changed was weighed and not done: what is
left to save is a few tenths of a percent, against a change to the console
and a rule every app would have to keep. The numbers are in CLAUDE.md.

## 0.75

**One clipboard, everywhere.**

- **A copy in the desktop reaches your machine's clipboard again.** The
  desktop runs held, and `hold` draws from an emulator that kept nothing
  it did not draw -- so the OSC 52 a copy sends was dropped, and no copy
  ever reached any machine's clipboard. The emulator keeps it now and hold
  passes it to every attached terminal, so with a second machine joined, a
  copy lands on both.
- **Note Pad copies, cuts and pastes**, with a selection: shift and the
  arrows, home and end, or a drag. Text copied in a terminal pastes into it
  and back, which it could not before -- Note Pad had no copy or paste.
- **A program in a Terminal window can set the clipboard** (OSC 52: vim's
  `"+y` through a provider that uses it, tmux's `set-clipboard`), reaching
  the desktop and every attached machine. On by default; Control Panel >
  Terminal > Programs Set Clipboard turns it off. A request to read the
  clipboard is never answered.
- **A paste from your machine becomes the desktop's clipboard**, so alt-v
  pastes it again in any window, on either display.
- **Files and pictures copy as files.** Image Viewer copies its picture as
  its file; Files pastes copied files as copies (after Cut, moves them),
  and text that names no file as a new `Pasted text.txt`.

What does not cross: a terminal carries text only, so a picture or a file
on a machine's own clipboard cannot reach the desktop. That is recorded in
the backlog, with the cross-machine pointer.

The `"terminal"` interface is version 2, adding `clip`; `term clip t`
gives a script the text a program set.

## 0.74

**Escape works straight away in a held desktop, and a click after it is
no longer lost.** `hold` -- which the desktop runs under, so it can be
detached -- kept a lone escape back until the next byte arrived, to see
whether it began a mouse report. So escape did nothing until another key
or click came, and then the two reached the desktop stuck together: a
click straight after escape read as alt-escape and stray characters, and
did nothing. That was the mouse "not working sometimes". A read that ends
on an escape is now passed on at once.

**Inside GNU screen too.** Screen 4.09 has the same habit of its own,
whenever the program in it has the mouse on, and that cannot be fixed from
inside: escape still waits there for the next key (F10 does not). But the
desktop now reads an escape that arrives glued to a following sequence as
an escape and then that sequence, so the click or key after it still
lands. tmux sends escape on after its `escape-time`. Both are described in
the desktop README, under Inside GNU screen or tmux.

**An Arch package.** `packaging/aur/` holds a PKGBUILD built with gcc,
checked by running its build, check and package steps against the release
tarball; `packaging/aur/update.sh` points it at a release and publishes it
to the AUR once the account to push from exists.

## 0.73

**Themes are files.** Each of the ten themes is now a small JSON file --
seven colours and a shadow strength -- in `examples/desktop/themes/`, and
`~/.config/hibr/themes/` is read first: a file there replaces the bundled
theme of its name, and any other name adds a theme to Appearance and the
Control Strip. A file is checked whole, every colour `#rrggbb` and the
shadow 1 to 100, and one that fails is left out rather than half applied.
JSON rather than a script because a theme is only data and is the thing
people share: one you were given can only ever be colours
([decision 0028](docs/adr/0028-a-theme-is-data.md)). The list is now
sorted by name, so the order the Theme row cycles through has changed;
midnight is still the default.

**New defaults**, chosen to match what most desktops do:

- **alt-tab** cycles windows (was tab) -- and works from inside a terminal.
- **ctrl-w** closes the focused window (was alt-f4). A terminal keeps it
  for its program, where it deletes a word or drives vi's windows: a
  terminal now keeps every ctrl with a single letter, not only ctrl-c, d
  and z, since those are the control characters programs read.
- **Quit has no key.** It is on the hibr menu; one stray `q` no longer
  ends the desktop. Shortcuts can give it one.
- **Holding alt while dragging** anywhere in a window moves it.
- **A terminal window shows its scrollbar.**

Settings saved by an earlier version are brought up to these once, on
load: a key still at its old default moves to the new one, and a key you
had changed stays as you set it. Neither super nor ctrl-escape can open
the menus: most terminals never report super on its own, and most
terminals send ctrl-escape as a plain escape -- which already opens the
menu bar, as F10 does.

## 0.72

**The Control Panel is grouped by what a pane is about.** Three headings
now: **Hardware** -- Displays, Keyboard, and a new Mouse pane; **Desktop**
-- Appearance, Control Strip, Date & Time, Desktop, File Types,
Notifications, Shortcuts, Windows; and **Apps**, one pane per app. Every
shortcut binding moved from Keyboard to its own **Shortcuts** pane, and
**Keyboard** is now about the keyboard: Desktop Shortcuts Win (which was in
Terminal) and the keys the desktop always keeps. **Mouse** holds what was
the mouse half of Windows -- edge resizing, modifier drag and its key, and
what a double click on a title bar does. The window opens two rows taller,
22, so the picker fits; it used to draw its last row over the border.

**Desktop Shortcuts Win is on by default.** A chord shortcut such as
alt-tab now works while a terminal has focus, without finding the setting
first; plain keys and ctrl-c/d/z still reach the program, and Pass Every
Key in the Window menu still gives one window everything. Settings saved by
an earlier version have it switched on once, on load (`DT_SETVER`), since
a saved file holds every default as it was when written; switch it off
again in Keyboard and that stays.

**A Mac's disks are on the desktop.** The disk icons were read from
`/proc/mounts`, which macOS does not have, so only Home showed. With no
mount table the desktop lists `/Volumes` instead, the startup disk as `/`.

**`make CC=gcc` builds the modules.** They were linked without `-fPIC`, which
tcc does not need and gcc does; the shell built and every module failed to
link.

**The README says how to install it**: Homebrew, the apt repository, Arch
(from source for now) and from source, and how to take it out again.

## 0.71

**A terminal window can give the desktop its shortcuts back.** Every key
used to reach the program inside a focused terminal, so a shortcut bound to
a chord -- alt-tab for Cycle Windows -- never worked while one had focus.
Control Panel > Terminal > **Desktop Shortcuts Win** (off by default) makes
a terminal decline any shortcut the desktop or an app launcher has been
given, so the desktop runs it instead -- but only a chord (ctrl or alt with
a key) or a function key. A plain key such as `q` or `tab` always reaches
the program, and so do ctrl-c, ctrl-d and ctrl-z, whatever is bound to them.
One window can still be given everything: **Pass Every Key** in the Window
menu, ticked, for a program that needs the chord or a desktop running inside
that terminal. Shift-right-click opens the terminal's own menu even when the
program has taken the mouse, which was already so and is now tested.
`tests/apps.py` checks each case through a real pty: the chord raising the
next window, reaching the program with the setting off, reaching it with
the window's override, the menu item, and the right-click.

## 0.70

**The six bash differences found while checking the documentation, fixed.**

- **`mapfile -O n` keeps the array**, as bash does: the lines go to `n`,
  `n+1`, ... and everything else stays. It padded the keys below `n` with
  empty strings and replaced the rest; `mapfile` without `-O` still clears.
- **`exec 3< <(cmd)` reads what `cmd` writes.** The substitution's own pipe
  took the lowest free descriptor -- 3 -- so `/dev/fd/3` became the
  redirection, and cleaning up after the substitution closed it. Its
  descriptor now starts at 60, clear of the 0-9 scripts use and of `{var}`
  descriptors from 10.
- **An arithmetic error in an expansion ends a non-interactive script**, as
  in bash: `echo $(( 1 / 0 ))`, `x=$((08))`, `${a[1/0]}`, in a function, in
  a condition. `(( ))` and `let` still only fail, with status 1 (and `let`
  no longer leaves its error to fail the next command); an interactive
  shell abandons the line; inside `try` only the command fails. The desktop
  calculator, which relied on `$(( ))` failing quietly, evaluates with
  `(( v = in, 1 ))` now.
- **Case conversion knows more than ASCII**: `${x^}`, `${x^^}`, `${x,}`,
  `${x,,}`, `${x@U}`, `${x@L}`, `${x@u}`, `declare -u`/`-l` and `str
  upper`/`lower` change the cased letters of Latin (with Turkish `ı`/`İ` and
  the `ǅ` digraphs), Greek and Cyrillic, and Armenian, by a table of hibr's
  own, whatever the locale -- the same stance as ADR 0021. Bytes that are
  not UTF-8 pass through untouched.
- **`kill -l` and `kill -L`** list the signals, or translate a number (an
  exit status above 128 too) or a name; `kill -s name` and `kill -n num`
  work; signal names are matched in any case; the signal table knows the
  standard set rather than twelve.
- **`recv` on a UDP socket takes a whole datagram**: a line drops its
  trailing newline, `-n` keeps that many bytes. It read a byte at a time,
  which on a datagram socket throws the rest of the datagram away and then
  waits for ever.

`tests/900-bash-gaps.t` compares the shell behaviour against bash;
`tests/905-case-udp.t` records the case table, `recv` over UDP and `try`.

## 0.69

**The documentation, all of it, held to what the shell does.**

- **Every example is checked or says why not.** `tests/531-doc-examples.t`
  reads every page with examples -- all of `docs/`, the READMEs, the
  decision records and the desktop guides. A ```` ```sh ```` block must be
  followed by its real ```` ```output ```` or carry `<!-- not run: why -->`
  (it needs a terminal, root or the network), and any other code block must
  say what it is. Every example was run and its output pasted back.
- **That found the pages wrong in places.** `language.md`'s declared-arguments
  transcript could not have come from its declarations; `cookbook.md` and
  `llm.md` each described `set -e` backwards in one line; `display.md` still
  said `mod load screen` and that panes have no order; `prompt.md` said
  right-hand prompts did not exist; the desktop README described a Displays
  pane that works differently; and builtin counts, ABI numbers, module lists,
  test counts and callback tables were stale across a dozen pages.
- **Every page was then read end to end** for what 0.57-0.68 added and the
  pages had not caught up with, and for duplication; the README and the docs
  index were rewritten around a start-here path, and the README's
  measurements were taken again on this release (398 KB, 87 ms on the loop
  against bash's 211, 1852 kB at startup).
- **`docs/tutorial.md`: from bash to hibr in ten minutes**, install to
  `--plan`, nine checked examples.
- **`man hibr`.** `docs/hibr.1.in`, with the version and module directory
  filled in by `make`, installed under `share/man/man1`.

**Four bugs the checking found, fixed:**

- A plain assignment refused because the variable is readonly, or typed and
  given a value of the wrong kind, ends a non-interactive script with status
  1, as bash does; hibr carried on. An interactive shell abandons the line; a
  prefix assignment (`r=2 cmd`) still only warns; a strict-mode refusal
  stays catchable.
- `send` and `recv` refuse a descriptor that is not a number: `send "" x`
  wrote to standard input.
- The `sys` module's `epoch` fills `:=`.
- The console module's messages said `screen:`, its old name.

Six more were found and are recorded in `docs/backlog.md` rather than
fixed in a documentation release.

## 0.68

**Four differences from bash, found by running real system scripts under
both shells and read one at a time.** All twelve invocations of the four
scripts that showed them now agree with bash:

- **A `set -e` pipeline whose last stage fails stops with that stage's
  status**, as bash does: `set -e; false | (exit 2)` exits 2, not 1. hibr
  still stops when only an earlier stage fails (ADR 0002), and then the
  failing stage's status is the one it has. (`uz`)
- **`select` over an empty list does nothing**, without a prompt or a read,
  and at end of input it writes its newline to standard output, not to
  standard error. (`tzselect`, which probes for `select` that way)
- **`$[ … ]` is arithmetic**, bash's old spelling of `$(( … ))`; hibr left
  it as text. (`byobu-ulevel`, which shifts by `$[$OPTIND-1]`)
- **`tests/corpus.py` normalises a `mktemp` suffix** inside the sandbox, the
  way it already normalises the sandbox path: `aptitude-run-state-bundle`
  differed only in its temporary directory's random name.

`tests/895-corpus-four.t` compares each against bash. The full set of 465
was not re-run here.

**The desktop's backlog entry said five steps of six.** The sixth, the
terminal window -- a hibr inside a hibr -- had been built for some time;
the backlog and decision 0020 now say so.

## 0.67

**A quoted key works inside arithmetic.** `$(( m["content-type"] + 1 ))`,
`(( m["x-y"] = 5 ))` and `for (( m["i"] = 0; ... ))` reach the literal key,
the same as `${m["content-type"]}` always has (ADR 0006). The text of
`$(( ))` and `(( ))` used to lose its quotes before the evaluator read it,
so the key was evaluated -- `content` minus `type`, key 0 -- and the only
way round it was to read the field into a variable first. A quoted variable
works as the key too (`$(( m["$k"] ))`), and so does a key with an
apostrophe in it. Quotes outside a subscript are removed as before, as bash
does. Cost: whether a piece of arithmetic holds a quoted key is decided
once and kept, so an arithmetic loop pays 0.04-0.14%.
`tests/890-arith-quoted-keys.t`.

## 0.66

**`hibr --plan script`: a dry run.** The script's own logic runs --
variables, functions, loops, reading files -- but everything that would
change the machine is refused, and each refusal is listed with its line:

```
hibr: plan: job.sh:1: would write out.txt
hibr: plan: job.sh:2: would run curl -sO https://example.com/app.tgz
hibr: plan: job.sh:5: would run rm -rf build
```

Writes outside the plan's own scratch `$TMPDIR` open `/dev/null` instead, as
do network endpoints; a program runs only if it is known to read (`cat`,
`grep`, `find` without `-delete`/`-exec`, `sed` without `-i`, `git status`
and the other reading subcommands, ...), and anything else fails with status
1 and no output -- so a plan stops where the script needed a result it could
not have, rather than carrying on with empty values and listing writes to
the wrong paths. `exec`, `kill` (but `-0`), `listen`, `mod load` and `need`
are refused. The scratch `$TMPDIR` is real and removed at the end, so temp
files work; the record goes to a copy of standard error a script's
`2>/dev/null` cannot reach; with `--agent` each record is a JSON line keyed
`plan`. `awk` and every program that runs another (`env` with arguments,
`timeout`, `xargs`, `sudo`, `sh -c`) are refused: whether they write cannot
be told from their arguments. It is not a sandbox, and says so: a program on
the read-only list runs for real. ADR 0027; `tests/885-plan.t` checks every
kind of refusal and that nothing the script named was created.

A policy that runs for real, the other half of the backlog item, waits for a
machine with Landlock: this one's kernel has it switched off.

## 0.65

**`[[ =~ ]]` fills `BASH_REMATCH` as well as `M`.** Measured, not argued:
every reply `tools/llm-measure/` got from a model writing hibr without its
reference page read its captures from `BASH_REMATCH` -- six of six -- and
printed nothing, because hibr filled only `M`. ADR 0004 had left the name out
so scripts would not work "by accident"; these were bash scripts failing for
a name, so it is amended. `=~` copies its captures into `BASH_REMATCH` after
filling `M` and clears it on a miss, as bash does; `M` stays the documented
place, and `match`, which bash does not have, fills only `M`. The same
replies, unchanged, now pass that task six times of six, and three that
pulled JSON fields out with a regex pass as well. `tests/880-bash-rematch.t`
compares against bash.

The README's "not implemented" list said `declare`, `shopt`, `set -o`,
coprocesses, anchored replacement, extended globs and `BASH_REMATCH`; all of
them work now. What is left there is bash's compound coprocess, `coproc name
{ ...; }`, since hibr's `coproc` takes a command.

## 0.64

**Dragging no longer leaves a window crawling after the mouse.** Every drag
report was drawn as its own frame, so whenever a frame took longer than the
gap between reports -- a big terminal, a busy screen, a held session -- the
reports queued up and the window went on replaying them in slow motion after
the hand had stopped. Redraw Skip only thinned that by a fixed count. Now a
drag or a wheel is drawn only once it has caught up: while the next report
is already waiting, the frame is skipped, and Redraw Skip thins what is left.
A burst of a hundred drag reports drew 102 frames and now draws 2; a hundred
and fifty, on a 140-column desktop with the Control Panel open, took 1.2
seconds to catch up, held or not, and now take 0.03. `console waiting` is
the new question the console module answers for it. `tests/desktop.py`
counts the frames, through a frame count the idle marker now carries.

Measured at the same time, so that it is not taken for the cause: an idle
desktop of this release, held or not, uses 0.2% of a core, and Redraw Skip
does work -- set to 5, it cut the same burst from 1.2 seconds to 0.23 before
this change.

## 0.63

**What `docs/llm.md` is worth, measured.** `tools/llm-measure/` sets a model
thirteen tasks -- three ordinary, four where a bash habit means something
else in hibr, six that need something only hibr has -- and runs what it
writes. From an empty directory, with no project context and tools off, three
runs each:

| | plain | trap | feature |
|---|---|---|---|
| Haiku 4.5, no page | 9/9 | 7/12 | 10/18 |
| Haiku 4.5, with the page | 9/9 | 12/12 | 18/18 |
| Sonnet 5.5, no page | 9/9 | 9/12 | 5/18 |
| Sonnet 5.5, with the page | 9/9 | 12/12 | 18/18 |

Ordinary bash was never broken. Without the page every regex capture read
`BASH_REMATCH` (6 of 6) and nothing parsed JSON or declared arguments. The
"with the page" rows are the page as fixed by this release: the page as 0.62
shipped it left Haiku at 11/12 and 16/18, and each miss named a gap --
`json get` on an array gives JSON text, a regex with a blank after `=~` goes
in a variable, `arr sort` is textual without `-n`, and `fn` always has a
parameter list. `docs/llm.md` says all four now. Since those fixes came from
these tasks, a fresh set is the honest next check; the runs, both prompts
and the replies are kept in `tools/llm-measure/runs/2026-10-01/`.

**The cookbook and the data page are held to their examples.**
`tests/531-doc-examples.t` runs each example with an output block in
`docs/llm.md`, `docs/cookbook.md` and `docs/data.md` on every build; a hidden
`<!-- setup -->` comment supplies the log file or helper an example assumes.
`docs/data.md`'s results were `# 11`-style comments and are printed and
compared now -- every one was right. Running them found 0.62's background
function bug.

## 0.62

**A function run in the background or as a pipeline stage ran only up to
its first program.** `f &` and `f | cat` fork a child for the stage, and
the child may replace itself with the stage's program rather than fork
again. That permission was a flag on the whole child, so when the stage was
a function it was used by the first program *inside* the function: the
child became that program, and everything after it in the body -- the
`echo`, the `return 3` -- never ran. The status was the program's. `f() {
sleep 1; return 3; }; f & wait $!` reported 0; bash reports 3. `ex_cmd` now
takes the flag as it starts and clears it, so only the command the fork was
made for can use it. A plain program in a pipeline or in the background is
still exec'd without a second fork. Found by running the cookbook's `wait
-n` example, which printed status 0. `tests/875-function-in-child.t`
compares each case against bash.

**Control Panel, Displays:**

- **The pane list could not be clicked while Displays was showing** -- only
  the arrow keys left it. The pane's own drawing cleared every clickable
  region in the window, the list's included, which the panel had just
  registered. It clears nothing now; the panel already has.
- **"Primary:" and its dropdown were on two rows.** They share one, the way
  every other pane lays out a label and its choice.

`tests/desktop.py` checks both. `tests/531-llm.t` is `tests/531-doc-examples.t`
now, ready to hold `docs/cookbook.md` and `docs/data.md` to their examples
as well as `docs/llm.md`.

## 0.61

**`return`, `break` and `continue` where they mean nothing are reported, and
the script runs on**, as in bash. A `return` at the top level of a script,
of `-c` text or of `eval`, a `break` or `continue` outside any loop, and
hibr's own `ret` outside a function all used to end the whole script
silently. Now each says why -- `return: can only `return' from a function or
sourced script` -- and the script continues: status 2 for `return` and
`ret`, 0 for `break` and `continue`, bash's numbers.

**A function cannot break its caller's loop**, and neither can a `( )`
subshell, as in bash: `f() { break; }` called from a loop used to end that
loop, and now it is an error inside `f` and the loop goes on. A `$( )`, a
pipeline stage or a `&` job inside a loop may still `break`, which ends only
itself, and `eval` and `source` are not boundaries, both as in bash. A loop
counts its depth, and a function call and a `( )` start it again from none.

`tests/870-return-break-outside.t` compares each of these against bash. The
cost: 9 instructions per function call (0.055% on a loop calling one) and
nothing measurable on a plain loop.

## 0.60

**A script runs as it is read, as in bash.** hibr used to read a whole
script, parse all of it, and only then run it. Now it reads one complete
command, runs it and reads the next -- for a script file, `-c` text,
`source`, `eval` and standard input -- so:

- **A late syntax error** stops the script there, after the lines before it
  have run, with status 2. Before, none of it ran.
- **A script on a pipe streams.** `(echo 'echo a'; sleep 1; echo 'echo b') |
  hibr` prints `a` at once, not after the second; stdout is flushed before
  each wait for more input. Measured: `a` at 0.002s, `b` at 1.00s.
- **A `read` in a script on standard input takes the script's next line**,
  as POSIX asks and bash does: a pipe is read a byte at a time, and a file
  on standard input a chunk at a time, seeked back to just past the line.
- **"Unexpected end of input" is status 2**, as bash's is, not 1.

**`checkfirst`: parse the whole script first, when that is what you want.**
`hibr --checkfirst`, `set -o checkfirst` or `shopt -s checkfirst` parses a
script file, `-c` text, standard input (then read whole) or a `source`d file
before running any of it, and runs none of it when it does not parse -- the
old behaviour, now a choice. Agent mode turns it on. `eval`, traps and
`$(…)` run as read either way. ADR 0026 records both, and the one case a
pipe cannot match: a command still incomplete after 64 lines is re-parsed
only as it grows by a quarter, since every line made a 3,000-line piped
function take 3.1 seconds (now 0.02), and lines already waiting past its end
are read with it -- so a `read` straight after such a command, in a piped
script, misses the line bash would give it. From a file it is exact.

Cost: the `while` loop's instruction count moved 0.001%; sourcing the whole
desktop is 0.02% cheaper, since each command's parse memory is released as
it finishes rather than when the script ends. `tests/860-read-as-you-go.t`
runs a late syntax error through a file, a pipe, a redirect, `-c`, `source`
and `-n`, with and without `checkfirst`, and a `read` taking the script's
next line from a pipe and from a file.

**Found by fuzzing the new pipe path, and fixed:** a malformed `${…}` in the
regex after `=~` -- `[[ x =~ ${^(a) ]]` -- crashed the parser on a null
word. It did in 0.59 too; only the new path's fuzz run reached it. It is a
syntax error with status 2 now, as in bash (`tests/865-regex-bad-sub.t`),
and an error inside a re-lexed `${…}` now stops the parse rather than being
dropped.

## 0.59

**`hibr --explain script`: the mistakes in a script, named, without running
it.** It parses, runs nothing, and reports each finding with its line, what
is wrong and what to write instead; with `--agent` each is a line of JSON
keyed `warning`. Status is 0 when clean, 1 when something was found, 2 for
a syntax error. Ten rules, each for a mistake models (and people) make:
`unquoted-path` (`rm $f`), `cd-unchecked`, `for-ls`, `test-unquoted`
(`[ $x = y ]`), `bind-program` (`x := uname`), `unset-quoted-key`
(`unset 'm[$k]'`), `local-self-ref` (`local a=$1 b=${m[$a]}`),
`local-masks-status` (`$?` after `local x=$(cmd)`), `dead-return` (a failing
`return` after `ret`) and `bare-key` (`${m[row]}`). The rules are a module,
`mods/lint/`, offering `"lint"`; `--explain` asks for it, so a script that
is run rather than explained never loads it. bash's `set -e`-in-a-condition
trap is not a rule, because hibr's errexit already reaches those functions
(ADR 0002).

Every rule was run over hibr's own 68 example scripts and `tests/self.hibr`
before shipping, and each finding was either fixed or the rule narrowed: 62
findings became 2, both in `tests/self.hibr`, which reads an unquoted key on
purpose.
`test-unquoted` passes `$#`, `$?`, `${#x}` and a variable the script only
ever sets to a number; `dead-return` only a failing `return`;
`bind-program` a name some module makes a builtin (`ls`); `bare-key` an
array made in another file, which may be `-A` there. Then over 259 shell
scripts in `/usr/bin` and `/usr/sbin`, parse only: no crash, and a sample
of what it reported read as real.

`tests/850-lint.t` has a firing case and a quiet case beside it for every
rule, and fails if any example stops linting clean.

**Found by it, and fixed:**

- **`dt_textkey` told every caller it had handled every key.** It ended an
  unhandled key with `ret "$text $cur"; return 1`, and `ret` had already
  returned 0 -- the `dead-return` rule's own example. It returns 1 now.

**Found while testing it against system scripts, and fixed:**

- **bash's `function` keyword did not parse at all**: `function f { ...; }`
  and `function f() { ...; }` were both syntax errors, which stopped
  `iptables-apply` and `ip6tables-apply` before their first line. Both forms
  work, as does a name bash allows there and `name()` does not
  (`function e-f`).
- **An `exit` inside a condition exited 0.** `if exit 3; then :; fi` exited
  with the `if`'s own status rather than 3, and the same for `while`,
  `until` and `!`, for `return` inside them, and for a `set -e` stop in a
  function called from one -- which is how it was found: that script
  printed its errexit message and then reported success. Each keeps the
  status it was stopped with now. `tests/855-exit-in-condition.t` compares
  every case against bash. The check costs 0.12% of the instructions on a
  `while` loop.

## 0.58

**Agent mode: `hibr --agent`, or `set -o agent`.** For a script a program
runs rather than a person -- a language model's, a build's, a harness's:

- **Errors are one line of JSON each** on stderr, keyed by level, with the
  file, the line and the source line's text, and a column for a syntax
  error: `{"error":"missing: unbound variable","file":"run.sh","line":2,
  "source":"echo \"$missing\""}`. Nothing is invented -- no codes, no hints.
- **`set -u` and strict expansion** are on: an unset variable is an error,
  and an unquoted expansion is one argument. `set -e` is left to the
  script, since hibr scopes it differently from bash (ADR 0002).
- **Nothing waits on a terminal:** a terminal on standard input is replaced
  by `/dev/null`, so `read`, a pager or an editor ends at once.
- **`HIBR_TIMEOUT=seconds`** bounds each foreground process: TERM, then KILL
  two seconds later, and status 124, as timeout(1) reports. Under it each
  foreground child gets a process group of its own, so what the child
  started is ended with it, rather than left running and holding the
  output pipe open. It bounds processes, not the shell's own work -- a
  builtin, a function or a `while` loop is not a process to end.

ADR 0025 records each choice. `tests/605-agent.t` covers the JSON, `set -u`,
strict expansion, piped input and the timeout; `tests/editor.py` checks a
terminal read through a pty; the language guide and `docs/llm.md` show it.

**Found while building it, and fixed for everyone:**

- **A syntax error exited with status 0**; it is 2, as in bash. An agent
  would have taken a script that never ran for one that succeeded.
- **An empty `then`, `else`, `do`, `{ }` or `( )` was accepted**: `if true;
  then fi` passed, and `while true; do done` looped for ever. Each is a
  syntax error naming the token that came too soon, as bash says it --
  `syntax error near unexpected token `fi'`. The error that used to say only
  "near unexpected token" names it too, and after the first error nothing
  more is reported, so `eval "if then"` is one error and status 2.
- **An error could come out before what the script printed ahead of it**,
  because stdout is buffered in a pipe and stderr is not; `echo start; echo
  "$missing"` logged the error first. Errors flush stdout now. Eight
  recorded tests had captured the old order and are re-recorded; each
  recording is the same size, since only where lines fall has changed.

**Array elements keep a bracketed key whole.** `m=([c d]=2)` split at the
blank into `[c` and `d]=2`, and `a=([k]=$v)` with a spaced `$v` made two
elements. An element opening with `[` is read to its matching `]`, and a
`[key]=value` element is expanded as an assignment -- never split or globbed
-- while a plain element still splits, as in bash.
`tests/604-assoc-keys.t` compares with bash.

One difference is recorded, not fixed: bash runs a script a command at a
time as it reads it, and hibr parses the whole script first, so a syntax
error late in a file stops hibr before its first line runs. It is in the
backlog under "Stay a drop-in for bash", and `docs/llm.md`'s table says so.

HIBR_VER -> 0.58.

Verified: tests/all.py, all thirteen suites green in 167 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference;
`tests/fuzz.py` 400 parser rounds on the sanitizer build, no crash or hang.

## 0.57

**A page for a language model: `docs/llm.md`, and `llms.txt`.** The whole
language on one page. One table covers every place hibr deliberately
differs from bash; then each addition has a run example: signatures and
types, `ret` and `:=`, nested maps, `str` and `arr`, `match` and `rsub`,
`json`, `try` and `fail`, `opt` and `args`, sockets, and `strict`, and
the page ends with the mistakes to avoid. Every example was run and its
output pasted under it, and `tests/531-llm.t` runs each again on every
build, naming any whose output no longer matches -- a check none of the
other guides has had. `llms.txt` at the root points a model to it, and the
project README and the docs index link it.

**Arrays declared `-A` take every subscript as a literal key, as in bash.**
Writing the page's table found this. bash reads an indexed array's
subscript as arithmetic and an associative array's as a key; hibr has one
kind of array and read both as arithmetic. So everyday bash like
`declare -A seen; seen[$line]=1` sent any line holding a dash, a plus or a
space to key `0` or to a parse error, silently or noisily. `declare -A`,
`local -A` and `typeset -A` now mark the variable, and its subscripts stay
literal after expansion: `h[content-type]`, `h[$k]` with `k=a-b`, and
`h[-1]`, as bash has them. Other arrays keep hibr's rules, `declare -p`
prints `-a` or `-A` accordingly, and ADR 0006 records it.
`tests/604-assoc-keys.t` compares with bash. The desktop's maps are all
`declare -gA`, so the unquoted-subscript trap no longer applies to them. It
costs 0.04% on a loop reading a map: the check runs only where a subscript
would otherwise be evaluated. The first version, a lookup on every
subscript, cost 0.7%. One gap is known and recorded: `declare -A m=([c d]=2)`
splits at the space, where bash keeps `c d` as one key.

**`args` works when a function calls it more than once.** `opt` added to
one table for the whole shell and never replaced, so a function that
declared its options and parsed them got them right on its first call and
only defaults afterwards. A redeclared option overwrote the parsed value
with its default, and every function's options leaked into every later
`args`, a required one failing calls elsewhere. The page's `args` example
showed it, printing `count=1` for `--count=5`. Now `args` uses its
declarations up after parsing, and redeclaring a name replaces its entry,
so a function declares and parses on every call, and two functions never
see each other's options. `tests/350-args.t`'s own workaround, `opt -clear`,
is now documented rather than required.

HIBR_VER -> 0.57.

Verified: tests/all.py, all thirteen suites green in 166 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference.

## 0.56

Desktop moves to **0.34** alongside this release.

**A dialog shows which button Enter presses.** While focus is in a dialog's
field or list, its first button is drawn as the default: Apply in the
wallpaper picker, Rename, Save, OK, Yes. It gets its label in the accent
colour, bold, because that button is what Enter does from there. A button
that has focus is still filled with the accent, so the two never look
alike. In the `brackets` style a focused button now fills as well, and the
default is `[Apply]` in the accent. Every dialog puts its action first, and
Enter from each dialog's field does that action, so marking the first
button was enough.

**"Confirm Starts On" is gone.** 0.54 added it to Appearance's Dialog
Buttons, but it only ever affected the Yes/No question boxes -- "Quit
hibr?", the shortcut question, the unregistered-file prompt -- while its
label read as though it covered dialogs in general. Question boxes start on
Yes, as they did before 0.54, and are now marked like any other default. A
saved `DT_CONFIRMDEF` line is left as a harmless assignment, and the next
save drops it.

**Every desktop function but one is reached by a test.** The census went
from 73 functions no suite called to 1:

- **Dead code removed:** `dt_put`, `dt_iconplace`, `files_top` and
  `files_end`, which nothing called and no key or menu reached. `dt_put` also
  added a frame offset that contradicted the rule that an app draws in its
  window's own coordinates.
- **Every dropdown in every Control Panel pane**, walked by one data-driven
  test: open it, take another value, check it changed. The rows come from
  each pane's own `_rows` and `_drop`, so a new dropdown is tested without
  being named. That is 21 checks, and 30 callbacks nothing else reached.
- **The Window menu's own** Zoom, Hide and Close, from the menu bar and from
  a title bar. **The desktop menu's** Change Wallpaper, shift-click ranges
  of desktop icons, and the **Control Strip's** Theme and Wallpaper modules.
- **Apps:** the calculator's Use Answer, and Note Pad joining lines. From
  Files: hidden files, File > Home, and Get Info applying a new name. A
  picture dragged from Files onto Image Viewer, closing Notifications and
  Image Viewer, a terminal's own right-click menu, and Task Manager's View
  Details from both its menus.
- **Date & Time's dialogs through to their OK**, with nothing able to
  change: the one function that runs sudo is replaced by a stub that only
  records what it was asked, and a fake `sudo` first on `PATH` writes a
  marker a check refuses. Time Zone asks to set the zone chosen; Set Date &
  Time and Clock Format open, take a click and close.
- **Task Manager's End Task** is tested directly, not through the window,
  by the new `tests/603-tasks-direct.t` on a `sleep` of the test's own:
  through the window it would end whatever row the machine running the
  suite has selected. The same test covers the `ps` fallback macOS uses.

The one left is `displays_detach`, which needs a held session with a second
client. **96 more parameters are typed** from what these tests passed them:
18 `int` and 78 `int?`, with no literal call site disagreeing.

**The pty harness sees colour.** `tests/screen.py` keeps the pen each cell
was drawn with, and `sc.style(r, c)` gives its colours and boldness, so a
check can say what is highlighted and not only what is written. That is how
the default button is tested, in both styles.

HIBR_VER -> 0.56.

Verified: tests/all.py, all thirteen suites green in 162 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
uifuzz clean on three fresh seeds of 300 events each; the census at 1 of 537.

## 0.55

Desktop moves to **0.33** alongside this release.

**A type can allow empty: `int?`.** A declared parameter's type is checked on
every call, and `int` refuses the empty string, which is not an integer. So
an optional integer could not be typed at all: `int col = ""` failed the
first time the argument was left out. Now a type may end in `?`, and
`int? col = ""` accepts an integer or nothing, and refuses anything else. It
works for every type and for a return type (`-> int?`), and plain `int`
still refuses empty. ADR 0024 records why a mark in the signature won over
not checking defaults. `tests/220-fn.t` covers both forms.

**The desktop's parameters are typed.** The census now records each
argument's shape, and it found 562 parameters that were an integer on every
call the suites made: 528 are now `int` and 34 `int?`, in the 299 functions
the suites call. Before applying them, every literal call site in the
source was checked against the plan, and none disagreed. The 145 functions
no suite calls are left untyped, because the census has nothing to say
about them -- which is 0.44.1's lesson. A wrong argument now fails at the
call, naming the parameter, rather than drawing in the wrong place. Three
fresh fuzz seeds of 300 events each ran clean on top of the suites.

**A type check costs a fifth of what it did.** The check compared the
type's name against every type there is, on every call: 428 instructions per
typed argument. The type is now read once, when the signature is parsed,
into bits on the parameter, and a call only looks at the value: about 100
instructions, most of them the loop over the digits. An unknown type is now
reported once, when the function is defined, not on every call.

HIBR_VER -> 0.55.

Verified: tests/all.py, all thirteen suites green in 163 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 500 snippets against bash with no difference.

## 0.54

Desktop moves to **0.32** alongside this release.

**Dialog buttons have a group of their own in Appearance.** Under a new
**Dialog Buttons** heading:

- **Style** -- `filled`, as they have been since 0.45 (a block of colour,
  the accent while focused), or `brackets`: `[OK]` on the window's face,
  the focused one in the accent. The width is the same either way, so no
  dialog's layout moves when it changes.
- **Shadow** -- the button shadow, moved here from the Shadows heading.
- **Confirm Starts On** -- `yes`, as before, so enter goes ahead, or `no`,
  so enter is the safe way out of "Quit hibr?" and every other confirm box.
  Every other dialog starts in its text field or list, where enter already
  means the dialog's own action.

They are `DT_DLGBTN` and `DT_CONFIRMDEF` in `wm/settings.hibr`, kept with
the rest of the settings. The Appearance tests find the rows under their new
names, and new checks draw `[Yes]` and `[No]` in brackets and cancel a
confirm box with a bare enter when it starts on no.

**The census records each argument's shape.** Every call it logs now says
whether each argument was an integer, empty or anything else, and
`tests/census.py --types` lists every declared desktop parameter by what the
suites passed it. The first run found 558 parameters that were an integer on
every call, 30 of them optional, and 4 that take an integer or nothing on
purpose. That is the groundwork for typing them, the next release.

HIBR_VER -> 0.54.

Verified: tests/all.py, all thirteen suites green in 163 seconds.

## 0.53

**Under `-S`, what is written inside `${x:+…}` splits and globs.** The word
inside `${x:+word}`, `${x:-word}` and `${x:=word}` is text the author typed.
Until now it was treated as the expansion's result, so under `set -S` and
`strict expansion` it neither split nor globbed. That is what nearly broke
the Files app's hidden-file listing in 0.52. Now it behaves as it would
outside the braces, and only an expansion *inside* it is protected:
`${on:+a b}` is two arguments, `${on:+./.*/}` lists the hidden directories,
and `${on:+$g}` with `g='*.c'` is the single argument `*.c`. ADR 0009 and
`docs/language.md` record the rule with its example run, and
`tests/340-strict.t` checks six forms of it.

**The default mode now agrees with bash on those words too.** Honouring what
was written meant keeping the word's own quoting instead of flattening it to
a string, which is what hibr had done: `${x:-"a b"}` split, `${x:-"*"}`
globbed the directory, and `${x:-"$y"}` split `$y`. A new `xarg` expands the
word with its per-byte quote mask. `tests/602-tilde-operators.t` compares
these with bash.

**A tilde expands where bash expands it.** hibr never expanded one in an
assignment -- `x=~`, `PATH=~/bin:~/x`, `local l=~/l` and `export E=~/b` all
kept the `~` -- nor in `case ~ in` or `${x:-~}`, because the single-word fast
path returned before tilde expansion ran. Now an assignment's value expands
a tilde after the `=` and after each `:`, as bash does, and single words
expand a leading one. A quoted `"${x:-~}"` and arithmetic (`$((~0))`) are
left alone, as in bash. One bash quirk is not copied: bash also expands
`echo a=~`, a command argument that only looks like an assignment.

**Tilde expansion no longer edits the parse tree.** It used to advance the
first part of the word in place and restore it afterwards, and a `$(…)` in
the same word forks in between. The child then ran with that part still
cut short. Once assignments went through the same step, a recursive function
lost the `n=` of its own `local n=$1` inside a substitution, and
`tests/130-local.t` caught it. Each expansion now works on a copy of the
part.

Measured by instruction count against 0.52, with glibc's address-sensitive
`strcmp` taken out: a loop of assignments is 0.56% cheaper, a bare `while`
0.45%, a loop calling a function with two `local`s 0.18%, and a `case` loop
unchanged. The first version cost 3.5%, scanning every assignment for a
tilde character by character; one `memchr` gate brought that down, and
dropping the save and restore of the first part paid for the rest.

HIBR_VER -> 0.53.

Verified: tests/all.py, all thirteen suites green in 162 seconds;
tests/asan.py, every suite against the sanitizer build with no report;
`tests/diff.py` 1,000 snippets against bash with no difference;
`tests/602-tilde-operators.t` compared with bash. `tests/corpus.py` was not
run this time.

## 0.52

Desktop moves to **0.31** alongside this release.

**The whole desktop runs under every strict check.** In 0.51 that was the
window manager and its widgets. Now it is `desktop.hibr` and every app,
game, desk accessory, Control Panel pane and strip module: 33 more files,
each saying `strict`. `tests/540-examples.t` fails if any of them stops
saying it. `session.hibr` stays as it was, because it is the user's own
script.

The method was 0.51's. `tests/census.py --expansion` listed ten places in
the apps where an expansion split: Minesweeper's neighbour lists, the
snake's body, a `/proc/stat` line in Task Manager, a terminal's colours,
the Keyboard pane's app names, the puzzle's four directions, and Files'
selection. Each became `read -ra` into an array. The puzzle's directions
became an array constant, which also takes a `set --` out of a
400-iteration loop.

A search for the forms the suites might not reach found one the probe could
not see. Files lists hidden entries with `${hidden:+"$d"/.*}`, and under
strict expansion the text inside `${…:+…}` is the expansion's result, so it
would have listed a file literally named `.*` instead. It now adds that
glob on a line of its own. `docs/language.md` records the rule with the
example run. Whether `-S` *should* treat that text as written is a real
question about the shell, and it is in `docs/backlog.md` for the owner
rather than decided here. With every site converted and strict switched
off, the probe listed nothing.

**`tests/strictvars.py` finds every global a strict function would create,
by reading.** `strict vars` refuses those at run time, but only on paths
that run, and each refusal stops its path before the next name is reached.
Turning it on for the apps found six in the first round of suites and a
seventh in the second. The checker reads every function in every strict
file and fails on any assignment that is not a parameter, a local, a
file-level name or one of the shell's own: `x=`, `x[k]=`, `x := f`, `read`,
`for`, `printf -v` and `(( ))`. It found the seventh without a suite run,
found nothing else, and is now a suite in `tests/all.py` that takes under a
second. The seven are all out-parameters, where a function hands back a
list or a pair: `DTP_LAT`, `DTP_LON`, `DTP_ROW`, `DTP_COL`, `DTP_WHY` and
`DTZ_HIT` in the Date & Time pane, and `FB_CRUMBS` in Files. Each is now
declared just above the function that fills it.

HIBR_VER -> 0.52.

Verified: tests/all.py, all thirteen suites green in 161 seconds, strictvars
included; tests/asan.py, every suite against the sanitizer build with no
report; `tests/census.py --expansion` listing nothing in the apps with strict
switched off; uifuzz clean on three fresh seeds of 300 events each.

## 0.51

Desktop moves to **0.30** alongside this release.

**The window manager and its widgets run under every strict check.** Since
0.49 all 27 files in `wm/` and `widgets/` said `strict functions vars`. Now
they say `strict`, and `strict expansion` is on as well, so an expansion
there never splits or globs.

Getting there took evidence rather than reading. The census build
(`make census`) gained a probe for the places strict expansion would change.
It logs an unquoted expansion that splits, and a value holding `*`, `?`,
`[` or `\` wherever it could glob or match as a pattern, with its file and
line. `python3 tests/census.py --expansion` runs the suites under that
build and lists the sites. The first run listed 20. Ten were a value with a
`?` in it being assigned or used as a `case` word, which neither splits nor
globs, so the probe now looks at each word's expansion flags and stays
quiet there. The other ten, plus three more loops found by searching for
the forms the suites might not reach, were lists the window manager splits
on purpose: the pane list, a selection of icons, the window ids, the
confirm box's geometry, a mouse report. Each now goes through `read -ra`
into an array, or `read -r` for a fixed tuple. With those converted and
strict still off, the probe listed nothing. Then `strict` went on, and
`tests/540-examples.t` now fails if any file in `wm/` or `widgets/` stops
saying it.

The probe also found something the documentation had never said: under
`set -S`, a pattern that comes from a variable (`case $x in $p)`,
`[[ $x == $p ]]`, `${x#$p}`) matches only itself, as a quoted `"$p"` does.
`docs/language.md` and ADR 0009 now say so, with the example run. The window
manager had no such pattern anywhere.

**`$LINENO` is right in a `for`, `case`, `if`, loop, `(( ))` or `[[ ]]`.**
Only simple commands set the current line, so `for i in $LINENO` and
`[[ $LINENO == 5 ]]` read the line of whatever command ran before. A strict
check refusing a loop variable therefore named the line that called the
function rather than the loop. `tests/600-strict.expected` moves by exactly
that one line, and a new `tests/601-lineno.t` compares these cases with
bash. It costs one store per compound command: 6 instructions, 0.04% of a
`case` loop. The first measurement said 0.33% on a loop that never reached
the change, and all of that was glibc's `strcmp` running different paths for
the same calls once the binary's strings moved.

HIBR_VER -> 0.51.

Verified: tests/all.py, all twelve suites green in 162 seconds with the window
manager fully strict; tests/asan.py, every suite against the sanitizer build
with no report; `tests/census.py --expansion` listing nothing in `wm/` or
`widgets/` before strict went on.

## 0.50

Desktop moves to **0.29** alongside this release.

**`x := rsub ...` binds its result instead of printing it.** `rsub` wrote
its answer to standard output unless given a variable to put it in, and
never looked at the result slot, so `x := rsub ...` printed the text and
left `x` empty -- `:=` was silently a no-op for it, as it is for a program
on the PATH. It now answers the way `str` does: into a named variable, into
the slot under `:=`, and to standard output otherwise, with `$RET` set in
every case. `tests/210-regex.t` records both a bound match and a bound miss.

**Four testing tools, from asking how the suites could be quicker and worth
more.** All four are described in `tests/README.md`.

- **`tests/affected.py`** names the suites a change reaches, from `git diff`
  or from paths given: `--run` runs only those, and `--why` says which path
  picked each suite. What a module reaches is read from the source, not
  kept in a list: every module's `hibr_require` calls, resolved through the
  others' `hibr_provide`. So a change to `mods/cat` reaches `hvi` and both
  desktop suites, because hvi asks for the `highlight` interface and cat
  provides it. A path it does not recognise reaches every suite.
- **`tests/census.py`** reports which desktop functions no suite calls.
  `make census` builds the shell a second time with `-DHIBR_CENSUS`, and
  that build logs every function call to `$HIBR_CENSUS`. The real shell
  compiles the logging out and pays nothing. It lists what was never called,
  file by file, along with any call whose arguments did not bind, and keeps
  the count so the next run can say which way it moved. The first run found
  79 of 540 never called; with the fuzzer below added to the
  runs, the count is 71.
- **`tests/uifuzz.py`** sends random keys, clicks, drags and wheel turns
  into one app's window. It then checks that the desktop is still running,
  still answering, and has printed nothing. Runs are seeded, and a failure
  prints the command that replays it. It runs as a suite in `tests/all.py`
  at a fixed seed; `SEED=random` goes looking somewhere new. It found the
  bug below on its first run. Task Manager, Files, Terminal and the Date &
  Time pane are left out on purpose: they signal processes, move files, run
  a shell and run sudo, on whatever machine is running the tests.
- **`tests/asan.py`** runs every suite against `make asan`: the shell *and
  every module* built with AddressSanitizer and UBSan into `build/asan`.
  Until now the console, term, pty, hold and img modules ran end to end only
  as the tcc build. Sanitizer reports go to files rather than to stderr, so
  a report from a desktop, whose stderr is its log, or from a forked child
  is still found, and any report fails the run. `HIBR_TESTMODS` points the
  pty harness at another module directory, and `HIBR_TESTLOGS` gives
  `tests/all.py` another log directory.

**A terminal window whose program had exited held a whole core.** Once
the program inside a terminal window exits, the pty's master side reads as
ready for ever. The desktop watches that descriptor so a program's output
wakes it at once, and a window that closes on a clean exit stops watching.
A window left open to show a non-zero status did not, so the desktop woke,
drew and woke again as fast as it could: 1,836 frames in six seconds, 100%
of a core, until the window was closed. The window now stops watching the
descriptor the moment it sees the program has gone.

The ASan run found this: the terminal's shell exited there for reasons of
its own, and the idle check failed. The ordinary suite never could. That
check counted voluntary context switches, and a loop that never blocks
makes none, so a spinning desktop measured as perfectly asleep. The idle
checks now read the CPU time the desktop used as well, and a new one opens
a terminal on `false` and requires it to stay under 5% of a core.

**The detach shortcut printed a `hold` error on a desktop that is not
held.** The Detach menu item is dimmed when there is no session to detach
from, but its key, `ctrl-\` by default, called `hold detach` anyway, and
hold complained into the log. The key now says "Not held -- nothing to
detach from", the same thing the dimmed item means.

HIBR_VER -> 0.50.

`tests/mon.py`'s "its pids are real processes" asked this of the three
heaviest rows, which under a parallel sanitizer run are the other suites'
children, gone before `/proc` was read. It now asks it of most of the
table.

Verified: tests/all.py, all twelve suites green in 162 seconds, uifuzz
included; tests/asan.py, every suite against the sanitizer shell and
modules with no sanitizer report; the exited-terminal check fails at 100%
of a core without the fix and passes with it.

## 0.49.1

**On macOS, `match` and `rsub` read and wrote the wrong memory -- and closing
a window froze the desktop.** hibr declares the C library's regex types
itself, because tcc cannot read glibc's `<regex.h>`, and it declared a match
offset as a 32-bit `int`, which is glibc's `regoff_t`. On macOS and the BSDs
it is 64 bits. So on a Mac every capture the library wrote overran the array
hibr gave it, and hibr read each offset from the wrong half: a `match` with
captures could loop for ever and an `rsub -g` could crash. The desktop draws
a pressed close button in a lighter shade of its red, worked out with
`match` -- which is why closing any window hung it, reported as "if I close
any window on Mac, everything gets stuck". Reproduced on Linux by declaring
the Mac's width there: `match` hung and `rsub -g` segfaulted.

`include/re.h` now uses each C library's own width -- 64 bits on macOS and
the BSDs, `int` on glibc, `long` on musl -- and a new `src/recheck.c`, compiled
by every compiler that can read the real `<regex.h>` (gcc here, clang on a
Mac; not tcc), asserts that hibr's declarations match it: the offset, the
match record, room for a compiled pattern, and the flag values. Getting any
of them wrong now stops the build instead of corrupting a running shell.

Found from a log sent from the Mac, which ended mid-frame, just after the
title bar's button colours were drawn.

HIBR_VER -> 0.49.1.

Verified: tests/all.py, every suite green in 153 seconds; tests/run.sh under
ASan and UBSan 93/93; the check fails the build when the width is forced
wrong.

## 0.49

Desktop moves to **0.28** alongside this release. **The module ABI is now
15**: a module built for 14 is refused until it is rebuilt. Every bundled
module is rebuilt with the shell.

**`strict`: a file can ask to be refused what is usually a mistake.** Perl's
`use strict`, as three named checks, for the file that runs it:

- **`strict functions`** -- defining a function a second time in the same
  file is refused, naming both lines:
  `wm/menus.hibr:313: dt_drop is already defined at line 98`. The first stays.
  Another file may still replace it, which is how a file of your own
  overrides a bundled one.
- **`strict vars`** -- a function creating a global because it forgot
  `local` is refused: `total is not declared -- local total, or declare -g
  total`. Assigning something that already exists, a local of its own or of
  a caller, or anything made with `local`, `declare` or `declare -g`, is not.
- **`strict expansion`** -- `set -S`, for this file alone.

`strict` alone is all three; `strict off vars` puts one back and `strict -p`
lists what is on. "The file" is `$BASH_SOURCE`, so a function is checked by
the file that defined it, wherever it is called from. A refused command fails
-- so `set -e` stops -- and a script that never says `strict` pays 0.25% on a
loop, measured. The design and what it deliberately does not catch are in
`docs/adr/0023`.

**`$LINENO`**, which hibr never had: the running command's line, counted in
its own file -- a function's in the file that defined it -- the same as bash
on every case compared. It is also what lets every strict message name a
line, and why the ABI moved: a node now carries the line it was parsed from.

**The window manager and the widgets run under `strict functions vars`.**
What it found:

- `dt_appmenu` read each app's kind into a global `kind` it never declared.
- Shared state -- the pending shortcut being reassigned, the wallpaper's
  geometry, the menu bar's last hit, the button shadow's cache, Quit's
  button focus -- was created from inside functions; each is declared at the
  top of its file now, where a reader can see it.

**Found on the way, and fixed:**

- **alt-c, alt-x and alt-v were never reserved.** `DT_KEYHELD[alt-c]`, a key
  with a dash in it and no quotes, is arithmetic: `alt` minus `c`, key 0. All
  three were one entry named 0, so Keyboard would let any of them be taken;
  capturing ctrl-\ also printed an arithmetic error. Every subscript in the
  shortcut code is quoted now, and a test takes alt-c.
- **The suites were blind to errors in a running desktop.** It sends its
  stderr to `desktop.log` so an error cannot draw over the screen, and 0.48's
  check for shell errors read only the terminal -- so its "none found in the
  desktop suites" was wrong. Every session's `desktop.log` is read too now.
- **Task Manager logged an error each time a process ended mid-scan**, from a
  redirection written in the order that lets the error escape.
- **The suites use this build's modules**, not whatever is installed:
  `tests/screen.py` puts `build/mods` first on `HIBR_MODPATH`.

HIBR_VER -> 0.49, HIBR_ABI -> 15, DT_VER -> 0.28.

Verified: tests/all.py -- run.sh 93/93 (and under ASan and UBSan, leak-free),
desktop.py 299/299, apps.py 272/272, most 23/23, hvi 32/32, console 61/61,
cat 29/29, mon 16/16, mtr 12/12, editor 11/11, term_diff 66/66 -- with
desktop.log read for errors; tests/fuzz.py 500 rounds under ASan, no crash
or hang.

## 0.48

Desktop moves to **0.27** alongside this release.

**The whole test run takes two and a half minutes, not thirty-five.** The
full-screen suites spent 99% of their time asleep -- `most.py` did 0.2
seconds of work in 35 -- because every key waited a fixed 0.45s, every
session a fixed start, and every quit a full 1.2s whether or not the program
had already gone. Now:

- **The desktop says when it is ready.** Run with `HIBR_TESTIDLE`, it prints
  an escape a terminal ignores each time a frame is on screen and it is about
  to wait, carrying how many bytes of input it has read -- `console consumed`,
  new -- so the harness waits for the frame that includes its key and no
  longer, and a frame a timer asked for is never mistaken for it.
  `desktop.py` went from about 15 minutes to 150 seconds, `apps.py` from
  about 10 to 75.
- **A session ends when its program does**, not after a fixed wait.
- **Every suite runs at once**, one per core: `tests/all.py`, or
  `make check-all`, with a line per suite and full logs in `build/test-logs`.

Speeding it up exposed two tests that had only passed because they were
slow: a snake and a brick game were being given time by pressing a harmless
key several times (a pause is now written as a pause), and a terminal
window's shell could be sent ctrl-c before it had set its trap (a terminal
test now gives its program a moment to start). And running side by side
explained `term_diff`'s one odd run in 0.46: every case started a tmux
server on the same socket and killed it, and a kill returns before the
server has gone, so under load the next case's start failed now and then.
Each case has a socket of its own now, and removes it after.

**A shell error printed anywhere fails the suite.** A too-many-arguments or a
command-not-found in a desktop session used to scroll past and leave the
test green if the screen still looked right -- the kind of error 0.44.1 was
made of. Every session is now scanned, and a suite fails on any shell error
nothing asked for; a test that provokes one on purpose declares it beside
itself. None was found in the desktop suites.

Four more ideas are in `docs/backlog.md` under Testing: running only the
suites a change touches, a standing report of desktop functions no suite
calls, fuzzing each app, and the pty suites under the sanitizers.

HIBR_VER -> 0.48, DT_VER -> 0.27.

Verified: tests/all.py, four full runs green after the last change -- run.sh
92/92, desktop.py 299/299, apps.py 271/271, most 23/23, hvi 32/32, console
61/61, cat 29/29, mon 16/16, mtr 12/12, editor 11/11, term_diff 66/66 -- in
153 seconds each.

## 0.47

**A command in a shell with many functions costs what one in a shell with
few does.** Every simple command asks first whether its name is a function,
and a miss -- every builtin, every program -- compared it against every
function defined. With the desktop's own 201 loaded that was about 11,600
instructions per command. From 16 functions on, the lookup goes through an
index now, and the same loop over builtins, with the desktop loaded, takes
418M instructions instead of 878M: 2.2 times faster per command. Loading the
desktop itself is 3.6% cheaper, since each definition used to scan for an
earlier one of the same name. The cost is one comparison per lookup below 16
functions -- 0.17% on a loop with none at all, measured against the same
build without it.

**`.` and `source` look a bare name up on `PATH`**, as bash does: the first
readable file there, executable or not, then the current directory. hibr's
option table had always said `sourcepath` was on, and it was not; now it is.
A folder of files that only define functions, put on `PATH`, is a library --
`. mylib` -- with `$BASH_SOURCE` the path it was found at.

**A command that is not found says so through its own redirections.**
`cmd 2>/dev/null || fallback` printed "command not found" regardless, which
bash does not; found while testing the lookup above.

HIBR_VER -> 0.47. The desktop is unchanged.

Verified: tests/run.sh 92/92 (and under ASan and UBSan), tests/apps.py
270/270, tests/desktop.py 298/298.

## 0.46

Desktop moves to **0.26** alongside this release.

**The Control Panel is organised the way a person looks for things.** The
picker lists two groups under headings: **System** -- Appearance, Control
Strip, Date & Time, Desktop, Displays, File Types, Keyboard, Notifications,
Windows -- and **Apps**, one pane per app with something to choose: About
hibr, Files, Task Manager, Terminal. Behaviour, which had become a drawer of
everything, is gone:

- Appearance has the theme, the wallpaper glyph, **Wallpaper Image…** -- the
  image picker, a window of its own now with Apply and Close buttons -- a new
  **Wallpaper Mode** row, the menu bar's spacing and icon, and the four
  shadows (windows, menus, menu bar, buttons) under a Shadows heading.
- **Desktop** has the icons and Redraw Skip.
- **Windows** is what Window Style was, plus Titlebar Click.
- **About hibr** has its refresh; **Files** its default view and Reset All
  Views; **Terminal** gains Cursor and Cursor Blink.

An app's pane is always listed, and says so when its app is not loaded,
rather than going blank or moving the list about. And there is a rule now,
with a test behind it: **everything an app keeps across restarts has a row
in some pane.** `tests/540-examples.t` reads every `dt_keep` and the
desktop's own list, and fails on one no pane shows -- which found the
wallpaper mode on its first run, settable only inside the picker until now.
Three things kept but not chosen (where the Control Strip sits, how long it
is, whether it is folded) are named there with the reason.

**Keyboard** replaces Shortcuts and App Shortcuts: one list, the desktop's
actions then every app's, and the Control Strip's key, which no pane had
shown.

- **A ✕ clears a shortcut**, beside every key that is set, and delete or
  backspace clears the selected row. A cleared shortcut is saved as cleared,
  so one that ships with a default does not come back at the next start.
- **A key never has two owners.** Pressing one another action holds asks --
  "ctrl-\\ is Detach's: give it to Control Panel?" -- and Yes moves it.
- **Keys the desktop keeps are refused with the reason**: f10 and escape
  open the menu bar, alt-c, alt-x and alt-v are Copy, Cut and Paste.
- A duplicate that only a settings file edited by hand could make is marked
  ⚠ on both rows.

The shortcut handling is a part of the window manager of its own,
`wm/keys.hibr`.

**The full-screen test suites count what they check.** `report(N)` used to
print N minus the failures, however many checks had actually run, so a
check silently skipped still counted as passed. `tests/screen.py` counts
every check made now, and a suite whose count differs from its plan fails
and says so. Its first run found five plans wrong: `console.py`'s was one
too high and `cat.py`, `most.py`, `hvi.py` and `editor.py` were each one too
low. Every check in them is unconditional, so none of the five had been
hiding a skipped check -- the plans had drifted as checks came and went --
and each is now the count the suite makes.

HIBR_VER -> 0.46, DT_VER -> 0.26.

Verified: tests/run.sh 90/90, tests/apps.py 270/270, tests/desktop.py
298/298, tests/console.py 60/60, tests/cat.py 28/28, tests/most.py 22/22,
tests/hvi.py 31/31, tests/editor.py 10/10, tests/mon.py 15/15, tests/mtr.py
11/11, tests/term_diff.py 66/66 -- each count now the checks actually made.
(A term_diff run that overlapped another one, both driving tmux, stopped
at 62 partway through its own checks; alone it makes all 66 in 12 seconds.)
tests/750-pty.t failed once in the full run and passed 23 times alone
since; nothing in this release touches the pty module, so it is recorded
here as intermittent rather than as fixed.

## 0.45

Desktop moves to **0.25** alongside this release.

**Dialogs have buttons.** Quit's confirm box, Rename, Get Info, File Type,
Clock Format, Time Zone and Set Date & Time each end in a row of real
buttons -- Yes and No; Rename and Cancel; Apply and Close; Save, Delete and
Cancel; Set (sudo) and Cancel -- in place of a grey line saying which keys
did what. A button is filled: the accent colour while it has focus, muted
while it has not, with Turbo Vision's shadow, a `▄` beside it and a row of
`▀` under it. Tab and shift-tab move through a dialog's fields and then its
buttons; the arrows move between the buttons once one has focus, and leave a
text field its cursor keys and a list its scrolling until then; enter does
whatever has focus, or the dialog's own action from a field; escape cancels;
a click does the button it lands on. y and n still answer Quit at once. File
Type's Delete is a button now, where before only a hint said ctrl-d.

The shadow is its own setting, **Button Shadow** in Behaviour, beside the
window, menu and bar ones. Its colour is the face darkened by the theme's
own shadow depth -- twice over, since a thin row of half blocks at a
window's depth vanished into midnight's near-black face -- and comes from a
new `console shade colour [pct]`, which answers the colour `console darken`
would produce, so there is one formula for both.

The buttons are a widget, `widgets/button.hibr`: `dt_button` and
`dt_buttons` draw one or a row, `dt_dlgbtns` centres a dialog's along its
bottom, and `dt_focus` and `dt_focuskey` move focus. A button can be drawn
into a window's pane or, as the confirm box's are, onto the screen itself,
and `dt_hit` finds it either way.

**Four themes**: **black** (pure black, near-black windows, soft grey text,
one cool accent), **neon** (synthwave -- purple-black, electric-cyan text,
hot-magenta accent), **phosphor** (a green monochrome CRT) and **amber** (the
amber one), each one hue family throughout. Every button reads at 5.5:1 or
better on all four; paper's unfocused one is 3.3:1, its muted colour being
a mid grey, and is the one theme where it falls short. Black's shadows cannot show -- nothing darkens black --
which is the look it is for. A terminal window takes each theme's text and
background, not yet its sixteen program colours.

HIBR_VER -> 0.45, DT_VER -> 0.25.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/console.py
61/61, tests/apps.py 246/246, tests/desktop.py 298/298.

## 0.44.2

Desktop moves to **0.24.2**. A second fix to 0.44.

**Set Date & Time set the clock to nothing.** The date reaches the command
that sets it through a small `sh -c '...'` script run under sudo, which reads
it as its own `$1`. The 0.44 conversion rewrote positionals to parameter
names everywhere outside single quotes -- and judged quoting a line at a
time, so on the script's second line, still inside the quote opened on the
first, `$1` looked like the function's own and became `$id`. It is `$1`
again. Those two were the only positionals anywhere in the desktop inside a
single-quoted string that spans lines, checked with the quoting followed
across lines this time; there are no heredocs to check.

`tests/apps.py` now runs that script for real against a `timedatectl` that
only reports what it was asked -- with sudo taken off the front, so nothing
on the machine running it changes -- and fails on 0.44.1.

`tests/desktop.py` counted Files windows by their whole title, `Files
[~/hibr]`, which is only that when the checkout lives at `~/hibr`; run from
anywhere else, one check failed. It counts by the start of the title now.

HIBR_VER -> 0.44.2, DT_VER -> 0.24.2.

Verified: tests/run.sh 90/90, tests/apps.py 235/235, tests/desktop.py 294/294.

## 0.44.1

Desktop moves to **0.24.1**. A fix to 0.44, which it should not have needed.

**Thirteen callbacks refused what the window manager hands them.** 0.44
wrote each function's parameters from what the test suites showed it being
called with, and for a callback the suites never reach, that was only the
names its old `local` line gave -- three for a `_click` the window manager
calls with four, two for a `_wheel` it calls with four. A declared function
refuses an argument it has no name for, so the call failed and the body
never ran: clicking in Rename, File Type, Time Zone or Set Date & Time did
nothing, nor did the wheel over Task Manager or the notification history,
nor dropping a file on the image viewer, and Process Details and Set Date &
Time drew nothing. Each now names its whole contract, the extra names
optional so a direct call with fewer still works, and a drop takes its paths
as `...paths` instead of shifting past four arguments to reach them.

`tests/540-examples.t` now checks every callback against the arguments it
is called with -- read from the desktop's own `dt_app`, `dt_new`, `cp_pane`
and `cs_module` calls, so a new app is covered without being listed -- and
it names all thirteen when run against 0.44. `tests/apps.py` calls
`tasks_wheel` the way the window manager does as well as the way the test
used to, which is how the census had seen two arguments where there are
four. `dt_confirm_key`'s parameter is a key, not an `id`, which the
conversion had guessed from its suffix.

HIBR_VER -> 0.44.1, DT_VER -> 0.24.1.

Verified: tests/run.sh 90/90, tests/desktop.py 294/294, tests/apps.py 234/234.

## 0.44

Desktop moves to **0.24** alongside this release.

**Every desktop function that takes arguments declares them.** 400 of them,
across the window manager, the widgets, every app, pane, desk accessory and
Control Strip module, went from `name() { local id=$1 h=$2 ...` to
`fn name(id, h, w, row, col) {`. The signature is now the first thing a
reader sees; a callback's says exactly what the window manager hands it,
and `ARCHITECTURE.md` lists every one. Three things came with it:

- **Cheaper calls.** Binding a declared parameter is done in C, where the
  `local` line it replaces was an expansion and an assignment per word:
  526M instructions against 711M for 20,000 calls of four arguments. Loading
  the whole desktop costs 0.25% fewer.
- **The `local` trap is closed.** A parameter is bound before the body
  runs, so `local b=${M[$id]}` can see `id`. `About hibr`'s bar width read
  `w` in the same `local` statement that assigned it, and was wrong until
  now; nothing had caught it because `w` was bare inside arithmetic.
- **A wrong number of arguments fails the call** rather than running with a
  missing value. So that the conversion changed nothing else, every call's
  argument count was recorded across the suites first, and the signatures
  were written from what was seen: a parameter the suites never showed being
  passed is optional (`= ""`), and a function handed more than it names
  takes `...rest`. The 71 functions the suites never call have every
  parameter optional. With the conversion applied the suites pass unchanged
  and no call anywhere in them failed to bind.

Types are not declared yet -- an `int` would refuse the empty window id the
Control Strip passes on purpose -- and are in `docs/backlog.md` as the
second pass. The undeclared form still works, for an app of your own.

`tests/540-examples.t` had not been parsing `apps/` or checking
`apps/Games/` for a function defined twice; it does both now, and knows
both ways of defining one.

HIBR_VER -> 0.44, DT_VER -> 0.24.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233, tests/term_diff.py 66/66 -- the last three
unchanged by the conversion, and under a build that logged every failed
bind, none across 419,306 calls.

## 0.43

Desktop moves to **0.23** alongside this release.

**`desktop.hibr` is a table of contents now.** Five thousand lines became a
page that asks for a display and sources the window manager's parts, one
concern to a file, from `examples/desktop/wm/` -- state, settings, the
session, windows, frames, drawing, wallpaper, input, menus, the bar,
context menus, apps, files, handlers, icons, hold, notifications, confirm,
the Control Strip -- and the widget library from `examples/desktop/widgets/`:
hit regions, checkbox, dropdown, slider, text field, scrollbar. Each part
owns its own state and settings, and has a header saying what it holds;
`wm/README.md` and `widgets/README.md` map them, and the latter sets out
what makes a file a library, since hibr has no separate notion of one. The
split moved text and rewrote none: loaded, the old file and the new parts
leave the same 185 function definitions, byte for byte, and the same
variables and maps behind. `tests/540-examples.t` now fails on a function
defined twice across all the parts together, not only within one file.

Three things in the shell itself made that possible, each checked against
bash:

- **`BASH_SOURCE`**: the file the running code came from -- the script, a
  file being sourced, or, inside a function, the file the function was
  defined in, wherever it is called from -- which is how `desktop.hibr`
  finds its own directory however a session reached it. A function carries
  its file from definition; a call costs 0.55% more instructions on a loop
  that does nothing but call a function, and nothing on one that does not.
- **`declare -f` and `declare -F`**, which bash scripts use to print and list
  functions: `-F` exactly as bash does, `-f` printing a definition as it was
  written, which reads back in as the same function.
- **`[0]` of a computed name** (`${RANDOM[0]}`, `${SECONDS[0]}`) reads its
  value, as `[0]` of any scalar does; and an assignment to `EPOCHSECONDS` or
  `EPOCHREALTIME` is ignored, as in bash, rather than hiding the clock.
- **`HIBR`**: this shell's own absolute path, asked of the system rather
  than guessed from the name it was started under, as bash's `BASH` is. A
  held desktop restarts itself under `hold new`, and it used to do so as
  plain `hibr`, which is whichever one `PATH` finds first -- so a desktop
  run from a build directory came back up under the installed shell. Until
  now the two could run the same file and nobody noticed; with the desktop
  asking for `BASH_SOURCE`, an older installed shell found none of its parts
  and the held session ended at once. It restarts under `"$HIBR"` now, and
  `desktop.hibr` run by a shell without `BASH_SOURCE` says which version it
  needs instead of reporting twenty missing files.

`docs/language.md` had still described `RANDOM=5` as reading 5 for ever
after; that stopped being true in 0.42 and it now says so. Three tickets
go into `docs/backlog.md` under "The language": `.` searching `PATH` for a
bare name, as bash's does -- which is all a library would need; looking a
function up without reading every name, since every command in the
desktop pays for its four hundred; and a per-file strict mode. All three
are agreed, and follow the dialog buttons and the Control Panel.

HIBR_VER -> 0.43, DT_VER -> 0.23.

Verified: tests/run.sh 90/90 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233, tests/term_diff.py 66/66.

## 0.42

Desktop moves to **0.22** alongside this release.

**Files sorts by a heading in the details view**, the way Task Manager does:
click Name, Size, Modified or Mode to sort by it, and again to turn it
round, ▲ or ▼ on the heading. `..` stays first and folders stay ahead of
files; names sort without regard to case, sizes largest first, times newest
first -- the one `ls -l` the view already makes now runs with `-t`, and the
order of its lines is each entry's age. The selection and any marks stay on
their entries. Until a heading is clicked the order is the one it always
was.

**A pressed title-bar button changes colour, not shape.** It used to be
drawn inverted, a block of its own colour behind the glyph. Now only the
glyph changes: a grey button takes the theme's accent, and a coloured one --
close's red, the traffic lights -- a lighter version of its own colour.

Ties in both lists now keep their order when a number column is sorted
descending: the key turns round rather than the list, so equal sizes and
equal CPU still read A to Z and by pid.

**`RANDOM=n` seeds the generator, and `SECONDS=n` restarts the count**, as
in bash. Both used to become plain variables the moment they were assigned,
reading the same value for ever after -- found because a Minesweeper test
needed a seeded layout: it failed one run in 72, when the one safe cell
among 72 happened to be the corner it opened first and won the game before
a mine could be hit. The test is seeded now. `tests/585-dynamic-vars.t`
checks both against bash.

HIBR_VER -> 0.42, DT_VER -> 0.22.

Verified: tests/run.sh 87/87 (and under ASan and UBSan), tests/desktop.py
294/294, tests/apps.py 233/233.

## 0.41

Desktop moves to **0.21** alongside this release.

**Task Manager sorts by any column, either way.** Click a heading -- PID,
Name, Owner, CPU% or Mem -- to sort by it, and click it again to turn the
order round; the sorted heading carries ▲ or ▼. Numbers start largest first
and names at A, case aside. The sort now happens in C (`arr sort`) over one
padded key per row, where an insertion sort in the shell used to take one
comparison at a time and would have crawled the moment a click reversed a
few hundred processes.

**The wheel scrolls the list** three rows at a time, leaving the selection
where it is, and **a scrollbar** in the list's right margin shows where the
view is; a click on its track jumps there.

**Task Manager has its own Control Panel pane**: Refresh (moved from
Behaviour), Scrollbar on or off, and Sort By and Order, the order a new
window starts in.

HIBR_VER -> 0.41, DT_VER -> 0.21.

Verified: tests/run.sh 86/86, tests/desktop.py 294/294, tests/apps.py
230/230.

## 0.40

Desktop moves to **0.20** alongside this release.

**The notification icon is a choice**, in Appearance: flag ⚑ (the default),
bell 🔔, note ♪, mail ✉, dot ●, diamond ◆ or star ✱, with the unseen count
beside whichever it is. The bell 0.39 introduced is an emoji: a terminal
without an emoji font -- the Linux console, many older setups -- draws it as
an empty box, and one with an older width table counts it as one cell and
pushes the clock along. The flag is not an emoji, so every monospace font
has it, one cell wide; the bell is still there for a terminal that shows it.

HIBR_VER -> 0.40, DT_VER -> 0.20.

Verified: tests/run.sh 86/86, tests/desktop.py 294/294, tests/apps.py
218/218.

## 0.39

Desktop moves to **0.19** alongside this release; the `term` module to 0.24.

**Terminal windows speak UTF-8 to their programs.** The `?` and the `?` in
a diamond left in terminal windows came from programs running in the C
locale -- inherited from a desktop started without a UTF-8 one, and on a
machine that has only `C.utf8` installed, a `LANG=en_GB.UTF-8` is the C
locale too, since a locale that cannot load leaves every category in C.
screen, ls and ncurses programs print `?` for what they believe cannot be
shown, and raw bytes for the rest, which a UTF-8 terminal shows as U+FFFD.
A program started in a terminal window now gets a UTF-8 `LC_CTYPE` when the
locale it would inherit is not one, checked by asking the C library rather
than reading the name; language, sorting and dates stay as they were.

**The notification bell is a bell**, 🔔, with the number of notes not yet
seen beside it (9+ past nine) and nothing beside it once the history has
been opened. It replaces the dot, and the circle before that.

`tests/screen.py`'s terminal model now lets a wide character cover the cell
after it, as a real terminal does; it had left that cell's old character
showing wherever a damage-based redraw moved a wide glyph along.

HIBR_VER -> 0.39, DT_VER -> 0.19.

Verified: tests/run.sh 86/86, tests/term_diff.py 66/66, tests/desktop.py
292/292, tests/apps.py 216/216, and the console, most, hvi, mon, cat and
editor pty suites.

## 0.38

Desktop moves to **0.18** alongside this release.

**Terminal windows wear the theme.** Their default text and background are
now the theme's own ink and face, like every other window, and a program
that asks what its background is (OSC 10/11) is told -- which is how Claude
Code, vim and others choose a light or dark scheme for themselves. Control
Panel's Terminal pane has a Colours setting: `theme`, the default, or
`terminal`, which leaves them to the real terminal the desktop runs in; that
one hibr cannot see, so the question goes unanswered rather than guessed. A
theme change reaches every open terminal window on its next frame.

**Menu Bar Spacing is a slider** in Appearance, 1 to 4 cells between the
notification dot, the clock and the application menu; 2 by default.

HIBR_VER -> 0.38, DT_VER -> 0.18.

Seven tickets recorded in `docs/backlog.md` under "For language models":
a reference written for a model to read, an agent mode, a linter for the
mistakes models make, a dry run and a policy for commands an agent runs,
an MCP server, staying a drop-in for bash, and being where models look.

Verified: tests/run.sh 86/86, tests/term_diff.py 64/64, tests/desktop.py
292/292, tests/apps.py 216/216.

## 0.37

Desktop moves to **0.17** alongside this release.

**The right-hand side of the menu bar is spaced like the left.** The menu
titles on the left sit two cells apart, from the space either side of each
name; the notification dot, the clock and the application menu on the right
were one cell apart and read as crowded. They are two apart now, and the
clicks follow them, whatever clock format is chosen.

HIBR_VER -> 0.37, DT_VER -> 0.17.

Verified: tests/run.sh 86/86, tests/desktop.py 292/292, tests/apps.py
212/212.

## 0.36

Desktop moves to **0.16** alongside this release; the `term` module to 0.23.

**The terminal emulator is rebuilt around a real VT parser.** Programs
built on ncurses and modern TUIs such as Claude Code, screen, htop and nano
drew stuck underlines, jumped text about and swapped letters in a hibr
terminal window. Every cause was found by comparing the emulator against
tmux on the same bytes, not by guessing:

- The parser threw away a sequence's `?`, `>` and `<` prefix, so a
  keyboard option (`CSI > 4 ; 2 m`) became "underline on" and the kitty
  keyboard queries (`CSI ? u`, `CSI > 1 u`) became "restore cursor".
  `vt.c` is now Paul Williams' DEC state machine, which dispatches on
  prefix, intermediates and final together, runs C0 controls inside
  sequences, lets CAN and SUB abandon one, and consumes DCS, OSC, APC, PM
  and SOS strings whole, however they are split across reads, instead of
  printing them.
- Combining marks, joiners and variation selectors took a column of their
  own; they now join the character before them. A wide character is kept
  whole, and writing over either half blanks the other.
- Insert mode, REP, CHT/CBT/HTS/TBC tab stops, origin mode, margins for
  cursor movement, DEC line drawing (`ESC ( 0`, SO/SI), full DECSC/DECRC
  with one slot per screen, 47/1047/1048/1049 each done properly, soft and
  hard reset, application cursor keys, focus reports, synchronized output,
  conceal, colon sub-parameters, underline colour, and replies to DA2,
  XTVERSION, DECRQM, DECRQSS, XTGETTCAP and the window size.
- The program is given `TERM=xterm-256color` and `COLORTERM=truecolor`,
  what this emulator implements, instead of inheriting whatever the desktop
  itself runs in.

**`tests/term_diff.py`** makes this permanent: 62 checks feeding the same
bytes to the emulator and to a private tmux server, cell by cell, over
short sequences and the recorded output of less, nano, vi, screen and
whiptail. Where xterm and tmux disagree, hibr follows xterm and the test
says why. The emulator was also run through 700 random byte streams under
ASan and UBSan with leak detection, and stayed clean.

HIBR_VER -> 0.36, DT_VER -> 0.16.

Verified: tests/run.sh 86/86, tests/term_diff.py 62/62, tests/desktop.py
291/291, tests/apps.py 212/212.

## 0.35

Desktop moves to **0.15** alongside this release.

**The menu bar clock's format is a setting**, in Control Panel's Date &
Time pane: a dropdown of examples (14:05, 14:05:09, 2:05 PM, 2:05:09 PM,
Tue 14:05, Tue 29 Sep 2:05 PM, 2026-09-29 14:05 and more), or Custom…
for any strftime format, checked as it is typed and limited to 24 columns
so it cannot run into the menus. A format with seconds asks for one frame
a second; one without asks once a minute. The clock and everything beside
it are placed from the width of what was actually drawn, so a wider
format moves left and its clicks follow it.

**The notification icon is a dot**: ● while there are notes you have not
seen, ○ once the history has been opened.

**Date & Time can change the time zone**, through a finder that narrows
tzdata's own list as you type, and **the date and time** where the machine
owns its clock. In a container, which shares its host's clock, it says
"set by the host" instead of offering something that cannot work. Both run
sudo in a terminal window of their own, so a password prompt and the
result stay on screen; setting the clock turns automatic time (NTP) off
first, and the dialog says so. A zone changed while the pane is open is
picked up within five seconds, by every clock the desktop draws.

**A dropdown value with a space in it never reached its callback whole.**
Menu items kept their command as one string and ran it split, so "2:05 PM"
arrived as two arguments. Items now keep their words separately. The
File Type dialog, and Date & Time's three, now name themselves in the
application menu rather than showing their internal prefix.

HIBR_VER -> 0.35, DT_VER -> 0.15.

Verified: tests/run.sh 86/86, tests/desktop.py 291/291, tests/apps.py
212/212.

## 0.34

Desktop moves to **0.14** alongside this release.

**An idle desktop sleeps instead of redrawing on a timer.** A live session
was measured at 19% of a core with nothing happening: it redrew the whole
desktop five times a second. The default refresh (`DT_TICK`) had been
raised from 200ms to 2000ms long ago, but that never reached anyone whose
settings file had saved the old value, and the shipped `session.hibr` set
200 itself. The tick is gone. The desktop now wakes for input, a resize,
output from a terminal window's program, or the soonest thing that asked
for a frame: the menu-bar clock asks for the next minute, the Clock desk
accessory and the Date & Time pane for the next second, a blinking cursor
for its next half-second, Minesweeper's timer, games, Tasks, About, notes
and the desktop icons' mount rescan for their own. Measured in a pty with
a terminal window open: about 0.2 wakes a second and 0.2% CPU, or 1.5%
with a blinking cursor. The Behaviour pane's Refresh row is gone with it,
and `tests/desktop.py` now counts the desktop's own wakes while idle, so a
tick cannot come back unnoticed.

**A resize signal that arrived just before the console started waiting was
lost until the wait timed out.** Found because the tick had been hiding it:
choosing a new primary display made `hold` signal the desktop while it was
still handling the click, and the bar stayed on the old display for up to
a minute. `cn_wait` now counts resizes and waits in `pselect`, so each one
ends exactly one wait, whenever it lands.

HIBR_VER -> 0.34, DT_VER -> 0.14.

Verified: tests/run.sh 86/86, tests/desktop.py 278/278, tests/apps.py
212/212, and the console, most, hvi, mon, cat and editor pty suites.

## 0.33

Desktop moves to **0.13** alongside this release.

**A Growl-style notification center**, the other idea ticketed in 0.32's
own `docs/backlog.md` and now built. `dt_note "$msg"` keeps its exact
signature and all ~30 existing call sites are untouched, but several can
now be on screen at once, corner-stacked (`DT_NOTEPOS`, four corners,
top-right by default) rather than one replacing whatever was already
showing -- the old single `DT_NOTE` slot is gone. A new `dt_notify "$msg"
cmd args...` is the clickable form: clicking that note runs `cmd` and
dismisses it; clicking a plain `dt_note` just dismisses it. Every note
that has shown, on screen or already gone, is kept in a capped, in-memory
history (50 entries) -- a new bell in the menu bar, and a "View History"
row in a new Notifications Control Panel pane (which also sets the
corner and the timeout), open a read-only window over it, timestamped.
Trash and shortcut-rebind notes are now clickable, straight to the
Trash folder or to Control Panel. Themed the same way everything else
in the desktop is (`DT_ACTIVE`/`DT_FACE`/`DT_INK`), since it draws with
the same calls `dt_confirm` already does.

Two batched notes that used to silently coalesce into one line under the
old single-slot mechanism (`dt_fileop`'s and `dt_trash`'s own "moved N
items" wording, each followed immediately by a more specific second
`dt_note` call that used to just overwrite the first) would have shown
as two separate stacked notes for the exact same batch under the new
queue -- `dt_tally` now takes the specific detail as an optional
argument instead, so there is exactly one note per batch, same as before.

HIBR_VER -> 0.33, DT_VER -> 0.13.

Verified: tests/run.sh 86/86, tests/desktop.py 276/276, tests/apps.py
212/212.

## 0.32

Desktop moves to **0.12** alongside this release.

**A new "File Types" pane in Control Panel** for `dt_handler`'s own
extension-to-program table -- list, add, edit and delete a mapping
through the UI, not only through a `dt_handler` line in a script of your
own (which still works exactly as before, and is what `dt_save` now
writes these back out as, the same way it already does for shortcuts
and icon positions). Double-clicking a file with no registered handler,
no `.png`/`.txt` default and no executable bit now asks first --
"Unregistered extension .ext -- open in hvi anyway?" -- rather than
silently opening it; declining leaves it alone.

**A real, pre-existing bug found building that pane's own Command
field**, and fixed everywhere it already existed: `dt_textkey`'s own
`"text cursor"` return has no separator of its own besides a plain
space, and every caller extracting the cursor back out
(`${res#* }`, the *shortest*-prefix form, splitting at the *first*
space) silently assumed the text itself never has one. A command line
always does ("feh -g"); Files' own Rename (a filename can) and Get Info
(name, owner, group) had the identical latent bug, just harder to hit in
practice. Typing past an embedded space scrambled every character after
it -- confirmed by the *saved* value, not just the display, once
isolated. Fixed at every one of the five call sites to `${res##* }`,
the longest-prefix form, which correctly splits at the *last* space
regardless of how many are in the text itself.

Two tickets recorded in `docs/backlog.md`, not built this release: a
Growl-style notification queue/history (today there is only ever one
note on screen at a time, replaced rather than queued, with no history
to look back at), and an "ultra small" `mods/db.c` -- a column-store
engine sketch (mmap'd row groups, zone maps for scan-skipping,
branch-light vectorized filters) with the real gaps it would need
closed before it is more than a demo: no static per-group row limit
inherited without deciding to, a real insert path, a composeable query
surface instead of two hardcoded comparisons, and an honest statement
of what "single writer, `msync`, no crash recovery" actually means.

HIBR_VER -> 0.32, DT_VER -> 0.12.

Verified: tests/run.sh 86/86, tests/desktop.py 267/267, tests/apps.py
212/212.

## 0.31

Desktop moves to **0.11** alongside this release.

**A shortcut still would not register after the first one that did**,
reported live as "I tried ctrl-l, alt-ctrl-l, and l, and none of them
registered" -- and reproduced exactly: the first attempt on a fresh row
worked, and every attempt after it silently failed, regardless of the
key. Root cause was a second bug in the same capture flow 0.29 had
already fixed one bug in. `panel_click`'s own "a second click on an
already-selected row activates it" means a shortcut row already selected
from a previous attempt arms capture on the very first click of the
next one -- so the click right after that, the other half of an ordinary
double-click or just a habitual re-click, arrives as its own event and
was read by `dt_event`'s own capture check as "the next key," silently
cancelling the capture it had itself just armed, before the intended
key was ever pressed. A mouse event no longer cancels a capture in
progress; only escape does.

**A terminal window's title now reads "Terminal [x]", not just "x"** --
whether x is what the program inside reported through its own OSC
title, the name of a file opened for editing, or the name of an
executable run directly (0.30's own new feature). The window stays
identifiable as a terminal no matter what it is showing.

**Files can now run an executable directly, from a script of your own**:
`dt_handler <ext> [term] <cmd...>` (documented in
`examples/desktop/README.md`, called from `session.hibr`, never from the
desktop itself) is where an extension-to-program mapping is added --
there is no default one, and none is meant to ship, since what opens
what is a choice the user's own session file makes.

**The wallpaper can now scale, zoom, or center, not only stretch** --
`DT_WALLMODE`, a new row in Control Panel's own Wallpaper pane (a
dropdown, or `m` on the keyboard): stretch ignores the image's own
aspect ratio (the original and only behaviour); scale fits it entirely
within the screen, keeping that ratio, the glyph pattern showing
through any letterboxed margin; zoom fills the screen, keeping the
ratio, with the overflow cropped; center is the image's own native
size, unscaled, centred. The same cell-aspect correction (`2*sw/sh`,
since a cell is one column but two source pixel rows) the wallpaper
picker's own preview already used for "fitted, not stretched."

HIBR_VER -> 0.31, DT_VER -> 0.11.

Verified: tests/run.sh 86/86, tests/desktop.py 267/267, tests/apps.py
203/203.

## 0.30

Desktop moves to **0.10** alongside this release.

**Double-clicking an executable in Files now runs it, in a terminal
window, instead of opening it in hvi** -- closing the file-handler
question left open in 0.29 (extension → handler mapping, `.txt` → Note
Pad). The executable bit decides run vs edit; it does not decide
windowed-app vs terminal. A hibr desktop app is only an app while it is
*sourced into the running desktop process* -- `dt_apps()` reads it so
its `dt_app`/`<name>_draw` registration runs in the desktop's own
namespace -- so making Files source an arbitrary double-clicked file
into that same live process, on a click, would cross a trust boundary
the hibr menu's own `DT_APPDIRS` allow-list deliberately does not. A
script wanting a window belongs there, where the menu already finds
it. Two details the naive version would have gotten wrong: the
existing `DT_OPENCMD` plumbing (built for a registered handler's own
interpreter, e.g. `python3 script.py`) always appends the path as a
final argument, which for an executable *of* itself would silently run
it against its own path a second time as an unwanted argument -- a new
`DT_OPENSELF`/`TW[$id]["selfrun"]` flag skips that append; and a
terminal window closes itself on a clean exit, which for a registered
handler is the point but for a double-clicked script would flash and
vanish before there was anything to read, so a self-run window stays
open regardless of status, same as a non-zero exit already does.

Verified: tests/run.sh 86/86, tests/desktop.py 264/264, tests/apps.py
200/200.

## 0.29

Desktop moves to **0.9** alongside this release.

**Control Panel shortcuts that appeared to do nothing when you tried to
change them**, reported live as "I cannot change shortcuts/keybindings
in general -- they work, but I cannot change them," specifically for
combinations like `alt-ctrl-x`. Root cause: the "press a key to rebind
this" capture treats a lone Escape as a silent cancel, and `cn_key`'s
own Alt/Escape disambiguation -- the same "is this a lone Escape or the
first byte of Alt-something" wait every terminal program needs -- could
be cut short by a *different*, unrelated terminal window's own program
merely having something to say at the wrong moment, deciding "Escape"
before the combination's second byte was ever read. Fixed in two parts:
`cn_key` now tracks a real wall-clock deadline for the disambiguation
instead of collapsing it the moment any watched pty stirs, and a
cancelled capture now says "Cancelled" instead of silently doing
nothing either way.

**File manager, four small things asked for together**: opening a `.txt`
file now offers Note Pad, the same way `.png` already offers Image
Viewer. A window's own title now names what it is showing --
`Files [~/some/dir]`, `~` for home the same way a prompt's `\w` already
would -- an app knows what it is showing better than the menu that
opened it. The path shown at the top of the window is a breadcrumb now:
click it for a dropdown of every ancestor directory, root included, and
choose one to jump straight there.

**A terminal window's own title now follows what the program inside it
reports**, through the same OSC 0/1/2 escape every real terminal
already honours -- a shell's own `PS1` is the "settable based on
variables" way to choose one (`\[\e]0;Terminal [\w]\a\]`, see
`docs/interactive.md`'s prompt escapes), not something new added here.
On by default (`DT_TERMTITLE`), and a new **Terminal** pane in Control
Panel holds it next to the terminal's scrollbar toggle, moved there out
of Behaviour since both are terminal settings, not general ones.

**Games no longer litter the hibr menu** -- Snake, Mines and Bricks moved
into their own `Games` subfolder, which the existing app-folder
mechanism already turns into a submenu automatically, the same way any
folder of apps does.

**Task Manager's own gap under its graphs**: `dt_win` already excludes
both border rows before an app's own `_draw` ever runs, and the layout
math subtracted three rows for them (as if the borders were still
included) instead of the one actual header row -- so a real border's
worth of dead space sat under the graphs for no reason. Also: the PID
column widens to whatever the widest PID on screen actually needs
(previously a fixed 6 characters, overrun and misaligned at seven
digits).

**The wallpaper picker's own mouse wheel did nothing** -- Control
Panel's generic scroll fallback only ever applies to a pane with no
`_draw` of its own; Wallpaper draws its own body and needs its own
`_wheel`, which nothing had written yet.

**`sysinfo`'s own uptime read now knows macOS**, via `sysctl` and
`KERN_BOOTTIME` rather than `/proc/uptime`, which does not exist there;
it read 0 minutes on every Mac. Unverified beyond a syntax read --
`sys/sysctl.h` does not exist on this Linux box even under
`-D__APPLE__`, so this could not be compile-checked here at all.

**About hibr**: relabelled "Desktop 0.8" / "hibr 0.28" as "hibr desktop
v0.9" / "hibr v0.29" with a blank top margin row; added Hostname (a
real FQDN, `hostname -f`, not the legacy and usually-empty
`/etc/domainname`) and Kernel rows underneath the version lines.

HIBR_VER -> 0.29, DT_VER -> 0.9.

Verified: tests/run.sh 86/86, tests/desktop.py 264/264, tests/apps.py
197/197.

## 0.28

Desktop moves to **0.8** alongside this release.

**Task Manager misaligned itself for any process with a wide enough
PID**, reported live at seven digits (real on macOS; Linux's own default
32768 ceiling never reaches it, which is why this went unnoticed until
now). The PID column was a fixed 6 characters; a wider one overran it
unpadded and pushed every column after it out of place. The column now
widens to whatever the widest PID actually on screen needs, never
narrower than 6, so the common case costs nothing extra.

**Homebrew's `post_install` -- used across the last two releases to
write a starter `~/.hibrc` -- never reliably ran, and now we know why**:
it is deprecated in current Homebrew ("Warning: Calling `post_install`
is deprecated! Use `post_install_steps` instead", seen live), and its
declarative replacement (`post_install_steps`) has no way to write into
a user's home directory at all -- by design, every `base:` it offers is
relative to the formula's own prefix. Homebrew formulas not writing
into `$HOME` is a deliberate choice worth respecting, not a limitation
to route around: two formulas could collide on one dotfile, an upgrade
could clobber a customised one. The formula now uses `caveats` instead
-- always shown, never deprecated, and it tells the user what to add
rather than acting on their behalf.

**`rc_load`'s own "no startup file" line was debug-only**, which is
exactly why "does hibr even load `.hibrc`" took several rounds of the
wrong theory to chase down. Raised to `HIBR_LINF` -- visible under
`hibr -d 2`, still below the default level, so a user who has simply
never wanted a `.hibrc` is not nagged about it every login.

HIBR_VER -> 0.28, DT_VER -> 0.8.

Verified: tests/run.sh 86/86, tests/desktop.py 261/261, tests/apps.py
184/184.

## 0.27

Desktop moves to **0.7** alongside this release.

**The macOS black screen, actually fixed this time** -- 0.26's fix landed
in the wrong process. The desktop always auto-holds by default
(`dt_autohold` -> `hold new -d`), so `console open` runs inside a
detached, headless held process that never touches the real terminal at
all; the real terminal only receives anything once it attaches, through
entirely separate code in `mods/hold/cli.c`. The same one-second
settle delay 0.26 gave `cn_open`, confirmed live to be the right fix in
principle, is now also in `hd_attach` -- the function that actually owns
the real terminal on every ordinary desktop launch.

**Keyboard shortcuts silently doing nothing while a window is focused**,
reported live as "I cannot use ctrl-alt-t for Terminal, and I cannot
change it either." Not a key-decoding bug -- confirmed hibr correctly
decodes real Mac keypresses (`alt-ctrl-t` came back exactly as pressed).
The actual cause: `dt_event` gives the focused window's own key handler
first refusal on every key, and only checks global shortcuts if that
handler declines. Control Panel's own handler (`panel_key`,
`examples/desktop/apps/panel.hibr`) discarded whatever its active pane's
key handler actually returned and always reported "handled" -- so
Control Panel with certain panes open (Wallpaper, at least) swallowed
*every* global shortcut unconditionally. Fixed to propagate the real
result. Audited every other app with its own key handler
(`bricks`/`mines`/`snake`/`tasks`/`files`/`calc`/`notepad`/`puzzle`/
`term`) for the same shape; all already decline correctly. One
by-design exception worth knowing: while a Terminal window itself has
focus, shortcuts legitimately go to the shell running inside it instead
of the desktop -- correct behaviour for a terminal emulator, not a bug,
but it means trying to open a *second* terminal via its own shortcut
while a *first* one is focused won't work, and never will.

HIBR_VER -> 0.27, DT_VER -> 0.7.

Verified: tests/run.sh 86/86, tests/desktop.py 261/261, tests/apps.py
184/184. The Darwin branch of hd_attach is untested here -- no way to
build it without Mach headers -- syntax-checked with gcc -D__APPLE__ only.

## 0.26

Desktop moves to **0.6** alongside this release.

**The real fix for the macOS black screen**, after two false starts. The
actual cause: some Mac terminal apps do not render the alternate screen
until they have had a moment to finish their own window setup after
opening, confirmed with a plain `sleep 1` before `console open` fixing it
with nothing else different. `cn_open` now waits one second before
entering the alternate screen, on Darwin only. The two earlier attempts
(widening `cn_size`'s retry budget, then a `dt_run` startup self-heal)
were reasoned from a stale-size theory that turned out to be wrong: given
that a `console flush` already re-fits the grid to the real terminal size
on every single call, size staleness was never actually the problem, and
both fixes are removed as dead ends -- recorded in CLAUDE.md so the same
theory doesn't get re-tried.

**A display could not be dragged to the left of, or above, whatever
happened to be at hold's own (0,0)**, reported live: the Displays Control
Panel pane's drag-release handler clamped the new position to zero in
both row and column before sending it to `hold move`, even though the
same file's own drawing code already handles negative *relative*
positions correctly (a display "before" the primary in hold's own
coordinates is exactly what happens the moment the primary is not the
leftmost one) -- confirmed the underlying protocol has no such
restriction of its own. The clamp was the whole bug; removed.

Also fixed properly this time: writing a starter `~/.hibrc` -- including
the new `command_not_found` autoloader -- only ever happened via
`deploy.sh`'s own install path, which a plain `brew install hibr` never
runs at all. The Homebrew formula (both this repo's copy and the live
`osakka/homebrew-hibr` one) now writes the same file via `post_install`.

## 0.25

Desktop moves to **0.5** alongside this release.

**A process substitution's child could become a permanent zombie.**
`xpsub_done` closed a `<(...)`'s descriptor and tried a non-blocking
`waitpid`, but discarded the tracking record regardless of whether that
wait actually reaped anything -- for `while read; do ...; done < <(cmd)`,
the loop's own first `read` reaches that code microseconds after the fork,
almost always before `cmd` has exited, so the wait reliably answered
"not yet" and the pid was forgotten anyway. Confirmed with a hundred-
iteration loop: 97 zombies on the old code, 0 fixed. Not desktop- or
macOS-specific -- any fast loop around `<(...)` hit this on any platform;
it surfaced first in Task Manager's own macOS process scan because that
is where the pattern was already in real use. The record now stays
tracked until a wait actually resolves it, one way or the other.

**The black screen on some Mac terminals was confirmed indefinite**, not
merely slower than `cn_size`'s widened 1s retry budget: waiting alone
never revealed it on a real Mac, only an actual resize did, meaning
nothing was ever going to self-correct without one. `dt_run`'s startup
self-heal is back, and unconditional this time -- a forced `console
reassert` on the first idle tick regardless of whether the re-read size
even changed, since that covers a stale-size theory and a terminal-
render-lag theory at once, without needing to know which one it is.

**A bare command name can autoload its module.** `command_not_found` in
`.hibrc` -- written into a new install's starter file by default -- calls
the new `mod find <builtin>`, which walks the module path the same way
`need` already does but matches a candidate's own builtin table by name
instead of its declared interface. `console key`, `img draw`, anything a
module registers, now works without an explicit `mod load` first, the
same shape bash's `command_not_found_handle` is. Interactive only --
`.hibrc` is never read by a script -- and deliberately only reachable
after PATH and every builtin/function has already refused the name, so
it can never shadow a real program. (Considered and set aside: moving
`json.c` itself to a module on the strength of this -- the autoloader
doesn't help scripts, which is how `json` is actually used, and the
underlying typed-map data it operates on is core infrastructure either
way. Recorded in `docs/backlog.md`.)

Smaller: About hibr shows the desktop's own version on its own line above
hibr's, rather than combined onto one; `sysinfo` gained a Darwin picture
and an explicit fallback to it when `/etc/os-release` doesn't exist,
which it never does on macOS.

## 0.24

Desktop moves to **0.4** alongside this release. A macOS hardening pass,
found live on a real Mac rather than guessed at from this project's own
Linux development machine.

**A window's centred title** was centred within the room left over after
excluding the button cluster, not across the bar's true full width --
pushing it off-centre by about half the cluster's own width, in opposite
directions depending on which side the buttons dock. Both sides were
wrong; only one was reported.

**A black screen until the first manual resize**, on terminals that settle
their real size more slowly than a plain local pty does: `cn_size`'s own
retry budget widens from 200ms to 1s, and `dt_run` now re-checks the real
size once more on its first idle tick regardless of whether a resize ever
fires -- free, since that tick's own wait for a key takes as long as
`DT_TICK` anyway.

**Wallpaper and TLS silently never worked on macOS**: `libpng` and
`libssl` were dlopen'd by bare Linux `.so` names only. Both gain macOS
`.dylib` paths (Homebrew's own versioned builds, by full keg-only path --
confirmed against Apple's own developer forums that the system's
unversioned copies hard-abort third-party code that loads them) and,
more importantly, a negative cache: a missing library was previously
rediscovered-and-failed on every call, which for a wallpaper once set
meant three failed `dlopen`s and a log line on every single frame, a
real and separate source of the reported choppiness. The Homebrew
formula now `depends_on` both on macOS.

**`DT_DRAWSKIP`** (Control Panel, Behaviour) is renumbered 0-9, default
0: the loop compared it against `DT_DRAWSKIP - 1`, so the old default of
1 skipped nothing, and the number never meant what it said.

**A native `darwin` module**: `cpu` and `mem`, from one
`host_statistics(64)` call each, no fork and no wait for a second
sample. Task Manager's system-wide meters were never implemented on
macOS at all; About hibr's own `top -l 2 -n 0` blocked the whole
single-threaded draw loop for about a second every three, whenever its
window was open -- a second real, separate source of the choppiness.
Both now use the module when it loaded, falling back to the previous
`top`/`vm_stat`/`ps` behaviour otherwise. Built and reasoned through on
a Linux box with no Mach headers at all; verified loading and resolving
its symbols on a real Mac, the numeric correctness of the readings
themselves still to be confirmed there.

**`zone1970.tab`**, tzdata's own place table, is not reliably at
`/usr/share/zoneinfo` on macOS -- reported missing there, where that
path is usually a symlink chain down through a version-stamped
`/var/db/timezone/...` and apparently does not always resolve. The
Date & Time Control Panel pane and `examples/traceroute.hibr` now check
`$TZDIR`, then the standard path, then the direct macOS path, and fail
silently -- no map mark, rather than a shell error printed at every
desktop startup -- if none exist.

## 0.23

Desktop moves to **0.3** alongside this release.

**Multiple monitors**, built on `hold` learning to hold more than one
attached client at once (`-m`), each with its own viewport into the
union of every attached screen's size, its own front grid for damage
diffing, and mouse reports translated by its own offset before the
desktop ever sees them. The desktop confines the bar, the icon layout
and the control strip to the first screen rather than stretching them
across the union, and gains `--join` to attach a second terminal
beside an already-running session, `hold`'s own named-client and
query/move/drop/primary protocol for a Control Panel to drive it, and
a Move-to submenu and Displays menu on every window built straight
from `hold clients`.

The Control Panel's own **Displays pane** went through several real
redesigns rather than one: first a WYSIWYG picture of the actual
layout in place of a list, then a dropdown to choose the primary
display instead of a click gesture that turned out to collide with
dragging, then pinning the primary as the pane's own fixed anchor so
dragging a secondary never renumbers what "primary" means mid-drag.
It now drops the little close-box corner glyph that never did
anything a menu couldn't do better, and gains a right-click context
menu on any display's rectangle — Detach, and Identify, which flashes
the display's own name in a bordered box drawn on *that* display, not
wherever the Control Panel itself happens to be open.

Two bugs found while verifying that arc, both silent: `HIBR_HOLD` is
an ordinary environment variable, so a terminal already attached to
one session inherited it into anything it spawned, and `dt_autohold`'s
own `[ -n "$HIBR_HOLD" ]` guard mistook any non-empty value — even one
naming an unrelated session — for "already held", starting a nested
`desktop --session X` un-held with no visible symptom. Fixed by
comparing basenames instead of merely checking for a value. The second
was `DT_SELFARGS`, captured too late in `desktop.hibr`'s own top level
to still see `"$@"` once `session.hibr`'s own `opt`/`args` had already
consumed it into named variables — so a re-exec through `hold` silently
dropped `--session` and everything else on the command line, always
re-launching as plain "desktop". The capture now happens at the very
top of `session.hibr`, before anything reads its arguments.

A third, unrelated bug surfaced by the user's own bug report: the menu
bar's left/right arrow keys walked `MB[]` by raw index, assuming each
bar menu sat next to the one before it — true until a submenu (Desk
Accessories' own flyout) got allocated an entry in the same array,
after which arrowing right from the **hibr** menu could land on the
submenu instead of Edit. `dt_mbar` now walks the array skipping
anything that isn't itself a bar-level menu, keyed off each entry's
own `bar` flag rather than its position.

**A configurable redraw skip**, `DT_DRAWSKIP` (Control Panel, Behaviour
pane, 0–9, default 0 — today's behaviour unchanged unless raised): that
many consecutive `mouse drag` reports beyond the first are absorbed
without a redraw, while a press or release always forces one
immediately and resets the count. Measured with an instrumented
redraw counter: a fixed 20-drag sequence drew 77 frames at the
default and 45 at `DT_DRAWSKIP=4` — a real reduction on a slow link or
a slow terminal, at the cost of the pointer visibly catching up in
jumps rather than gliding. Built alongside it: `dt_slider`, a
reusable horizontal slider widget (`▸────●─── 10`) for the Control
Panel, joining the existing checkbox and dropdown widgets and driven
by the same left/right-arrow cycling convention as a dropdown.

Smaller fixes: `term draw` and `img draw` gain `-p pane` and lose
their last absolute-coordinate paths, so nothing a window draws can
land outside its own pane regardless of which module drew it;
`deploy.sh`'s own fingerprint now covers `examples/desktop` and its
launcher, not just the C sources, so a desktop-only change is not
silently skipped on `deploy.sh update`; and `tools/homebrew-sync.sh`
keeps `osakka/homebrew-hibr`'s formula on hibr's latest GitHub tag on
its own six-hourly timer, so a tagged release reaches `brew install
hibr` without a manual step.

## 0.22

Desktop moves to **0.2** alongside this release.

**A real colour preview for the Wallpaper Control Panel pane**, drawn
straight into its own window rather than a grayscale ASCII render
captured into text. `img draw` gains `-p pane`, and `dp_api` gains
`prect`, a named pane's own rectangle, so any module can translate and
clip its own drawing into a window without reading the window manager's
own position table — the same discipline `console put -p` already gives
text, reused rather than reinvented. The module ABI for "display" moves
to `DP_API_VER` 3, since a module built against the old headers can no
longer link — the same kind of break 0.21's own ABI move to 3 was, and
which stayed a minor release then too. `img`'s resampled-grid cache
grows from one entry to four, round-robin, since a pane's own preview
and the desktop's wallpaper can now both call `img draw` with different
files in the same frame.

Applying a wallpaper dithers the resample (a 4x4 ordered Bayer pattern)
before it is ever darkened under a window's shadow, so a real photo's
own faint gradients read as fine grain instead of a repeating band once
darkened; a window's shadow itself now rounds its own darken math rather
than truncating it.

Desktop notifications (`dt_note`) get a real border and a timeout,
rather than three plain rows that only ever cleared on the next click or
key — which is what made applying a wallpaper from a window filling
most of the screen look like the display had come out wrong, rather
than like a transient toast.

Task Manager: process name folded into the CPU-time read already
happening every scan, fixing a latent bug where a name containing a
space was silently truncated, and halving what the read still cost
beyond that. Its own refresh interval no longer gets silently
overridden by a hardcoded one. Terminals now wake on real pty output,
rather than polling every 30ms after each key.

Glob patterns: escaping an extglob-trigger character (`(` `)` `|` `!`
`@` `+`) now stays literal, matching bash's own extglob-on behaviour —
it silently did nothing before, since the pattern-quoting pass only
recognised `* ? [ \` as characters needing the escape reinserted.

`tools/next-version.sh` (`make next-version`) suggests the next
`HIBR_VER`/`DT_VER` from what changed since the last tag. Fixed here,
found while cutting this exact release: it originally suggested a major
bump (a jump to 1.0) for any module ABI change or `Breaking:` trailer,
without checking this project's own precedent first — 0.21 carried a
real ABI break (the `nsh` rename) and stayed a minor release, the
standard meaning of staying below 1.0 at all: anything may still break
between any two 0.x releases, and 1.0 is a deliberate declaration of
stability, not a mechanical consequence of one breaking change. The tool
now bumps the minor version for a break too, once still below 1.0, and
only suggests an actual major bump once the project already has one to
count from. Still a suggestion, not an authority either way: a single
commit spanning both core and desktop can carry a break that only
applies to one of them, which this release's own `DT_VER` needed a
human judgement call to catch regardless.

## 0.21

**Renamed from `nsh` to `hibr`** — Hackable In-process Bash Runtime, and
**حِبر**, Arabic for ink. Every macro, exported symbol, filename and
user-visible name changed with it: `$HIBR_RC`, `$HIBR_MODPATH`,
`$HIBR_HISTFILE`, `$HIBR_TLS_INSECURE`, `$HIBR_DEBUG`, `~/.hibrc`,
`~/.hibr_history`, and `include/hibr.h`. The module ABI moved to **3**, since
a module built against the old headers can no longer link. Reasoning and the
names considered: [decision 0015](docs/adr/0015-the-name-is-hibr.md).

Documentation reorganised. The README is an overview; the detail moved to
`docs/`, every directory carries its own `README.md`, and the deliberate
divergences from bash each became a decision record under `docs/adr/`.

`deploy.sh` builds with the right module directory, runs the suite, keeps the
previous installation for rollback, verifies the copy it installed, and can
keep itself current with a systemd timer or cron. A `MODDIR`-related bug is
fixed along the way: `make install PREFIX=X` produced a binary that could not
find its own modules, because the module directory is compiled in and changing
it did not force a rebuild.

Every count in the git segment carries its own colour — staged green, modified
yellow, untracked cyan, conflicted and deleted red, ahead green, behind red —
rather than the whole block sharing one style, which it previously did not
have at all: `status_style`, `ahead_behind_style` and `state_style` had no
defaults, so the most information-dense part of the prompt rendered plain.

A prompt built in process: `PROMPT_FN` names a function, builtin or module
builtin whose result slot becomes the prompt, with `$STATUS` and `$DURATION`
set for it. The `prompt` module renders segments from a `PROMPT[...]` map with
a `[text](style)` and `(conditional)` format language — directory, git branch
and repository state, exit status, command duration, jobs, and the usual
environment and language markers, all without forking. Modules are searched for
on `$HIBR_MODPATH` and in the install directory, so `mod load prompt` works.
Multi-line prompts are measured correctly by the line editor.

The module now reads git's object store itself: a DEFLATE decoder, loose
objects, pack indexes v1 and v2, and delta chains through both `OFS_DELTA` and
`REF_DELTA`, with alternates followed. No zlib, no `git`, no forks.
`prompt object [-p] <sha>` reads one object, for inspection.

The `git` segment now reports the working tree: `.git/index` in versions 2, 3
and 4, staged changes by diffing the index against HEAD's tree, modified and
deleted files by stat data and by hashing when that is inconclusive, untracked
files with `.gitignore`, `info/exclude` and `core.excludesFile` honoured, exact
renames, conflicts, and distance from the upstream branch by walking both
histories. It carries its own SHA-1 for this. All of it in process.

Fixed a signed overflow in the lexer: a run of more than ten digits before `<`
or `>` overflowed the descriptor number it was scanning for.

## 0.20
Arithmetic statements `((…))`, `let`, `for ((;;))`, a rebuilt evaluator with
assignment operators, `++`/`--`, `?:`, comma and `**`, two's-complement overflow
defined and UBSan-clean. `+=` for strings, arrays and map entries. `[[ ]]` with
short-circuiting and `=~` into `M`. `select`, `!!` history expansion,
`history -s/-p`, `CDPATH`.

## 0.19
`${x:off:len}`, `${a[@]:off:len}`, `${x^^}` family, `$RANDOM $SECONDS $PPID
$UID $EUID $HOSTNAME`, `printf -v`, `echo -e`, `unset a[k]`, `wait PID`,
`local a=(…)`. `:=` clears the slot and suppresses builtin output.

## 0.18
Declared arguments with `opt` and `args`; `title` renames the process. `ls`
module. `printf` width overflow fixed.

## 0.17
Module ABI v2: map access, result slot, errors, sockets, and `/dev/<name>/`
scheme registration; `http` reference module. `make install`, `make check`,
`--help`, `-n`. Parser fuzzer; recursion bounded. Opt-in strict expansion
`set -S`.

## 0.16 and earlier
Brace expansion, `$'…'`, globstar. Completion over PATH, variables and user
hooks; `&>`, `<<<`, `>|`, `{fd}>`, `read` options, `command`, `builtin`,
`getopts`, `umask`, `time`, `disown`. `fail`/`try`/`trap ERR`. JSON, `str`,
`arr`. Networking and TLS. Daily-driver features: `.hibrc`, prompts, aliases,
dirstack, `^R`. Nested maps, regex, typed `fn`, result slots. Job control,
`case`, `local`, heredocs, errexit, the line editor, modules, and the core
shell.
