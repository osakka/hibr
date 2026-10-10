# Changelog

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
