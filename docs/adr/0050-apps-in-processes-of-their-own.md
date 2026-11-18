# 0050 — Apps in processes of their own

Status: decided (owner, 2026-11-18): every installable runs in a process of
its own, by one path, with no switch -- the Decision below. The first plan
(0.99.400) and the prototype that measured it follow it, as history.

## Context

ADR 0049 makes a slow callback caught three times -- lint, the gate, the log
and the pause -- and says plainly what it cannot do: the first slow run
still stops every display, because every app runs in the desktop's own
process. X has the same rule for its server and a different answer for its
clients: each is a process of its own, so a client that hangs hangs only
its own windows.

The desktop cannot do that today, for one reason recorded in CLAUDE.md:
a window draws into the one grid the console owns, through
`console put -p`, and a pane has no cells of its own. Giving each pane its
own cells and composing them at flush was built once and worked -- until
the picture modules, which write at absolute coordinates (`img draw`, and
media, web and the pager through it), could not be placed in either order
against a pane's own chrome. `dp_api` has no paned put for a picture.

## Decision (owner, 2026-11-18)

**One path.** Every installable -- an app, a Control Panel pane, a Control
Strip module, a menu bar item, a saver, of every kind the catalogue carries
-- runs in a process of its own, always. There is no switch and no
in-process mode for an installable: a per-app "Separate Processes" choice
was proposed and refused (owner: "is this parallels, and breaks one source
of truth?"), because it is two ways to run every app, each needing its own
tests and fixes. What stays in the desktop's process is the platform alone
-- the window manager (`wm/`), the widgets, and the files ADR 0046 marks
`# platform:` (the Control Panel's window, the saver framework, the four
bar items the package ships) -- since that *is* the desktop (owner,
2026-11-18: ADR 0046's definition kept); `lib` is installable code
(Review 3), and `tools/appcalls.py` leaves the platform's files out of the
installables it reads. Going back is the previous release, not a toggle.

**Why the prototype is not the design.** The prototype (below) answered
"does it work, and what does it cost" with no new C by riding on the
terminal window: a hosted app is a program in a terminal, and every window
manager feature reached it through a hook in `apps/term.hibr`. That is a
bolt-on, and its gaps are its shape -- the bar told the app's name by
special case, titles that do not cross, dialogs drawn inside the app's own
screen, a fixed-size app made resizable. Its measurements and its suites
carry over; its route does not.

**The design.**

1. **A hosted window is a window, not a terminal.** It keeps its app's
   identity (Calculator is `calc`, not `term`), and the window manager calls
   it as it calls any window -- `_draw`, `_key`, `_click`, `_mouse`,
   `_wheel`, `_paste`, `_drop`, `_copy`, `_menus`, `_canclose`, `_close` --
   through **one** place, `dt_appcall`/`dt_appget`, which sends the call
   across when the app is not the platform's. A title, a second window, a
   dialog, a fixed size, an `_open` argument: each is the window manager's
   own, said by the app as a message, never a hook in another app.
2. **The window manager never waits on an app.** Every call across is one
   of two kinds, and the table of which is part of the contract: told (keys,
   clicks, the wheel, a paste, a drop, focus, a close) or answered from what
   the app last pushed (`_dirty`, `_menus`, `_copy`, `_canclose`, a key's
   own "handled") -- the app pushes, the desktop caches, nothing blocks.
   Input not answered within `DT_HOLDMS` marks the window "not responding";
   its keys go on being sent; it is never paused (ADR 0049's rule).
3. **What an app declares is read, not run.** Its `dt_app`, `cp_pane`,
   `cs_module`, `dt_baritem`, `dt_keep` and `dt_ticker` lines are read by the
   `inspect` module (ADR 0056; Review, 5) when the desktop scans the catalogue's
   scopes, so the menus, the Control Panel's list and the bar know every
   app without one line of its code running in the desktop. Its tickers and
   hooks run in its own process, where a slow one stops only it.
4. **One process per app, holding all its windows**, forked from one
   prepared host (the zygote, measured below: +1.1 MB an app). The windows
   of one app share its globals today on purpose (Mail and its compose
   window); per window would change what an app is.
5. **One channel each way, made for it** (see the Review, 2: the desktop
   holds a hosted window's cells). The prototype carried messages
   in-band in the terminal stream (OSC up, a bracketed paste down) and its
   cells through a terminal emulator; the design gives each app process a
   socket of its own to the desktop for messages, and its cells reach the
   desktop's console as cells -- the "pane drawn from another process" of
   the first plan's step 2 -- with pictures by descriptor as the prototype
   already does (`frames`). Which carriage of cells is cheapest is measured
   before it is chosen: the desktop's frame budget (0.7 ms, 1% of a core) is
   the bar.
6. **Settings stay the desktop's.** It owns `desktop.json`; an app sends
   its kept settings up and only names its own file keeps are accepted
   (done in the prototype).
7. **A crash or a hang is the app's alone.** An ended app's windows say so
   and offer to start it again; a Restart Desktop finds the host by its pid.
8. **Moved in one release, every kind** (owner, 2026-11-18: "one release,
   all kinds"), once the Review's counts are in. The suites already open apps
   through the window manager, so they run against hosted apps unchanged;
   `tests/isolation.py` goes into `all.py` then, passing without `ISO=1`,
   which no longer exists. The prototype's terminal hooks (`term_draw`'s
   notes, `term_menus`, `term_drop`, `term_canclose`, `term_paste`'s mark
   and the rest) are removed in the same change: the terminal window is a
   terminal again.

## Requirement: we stay as sensitive to a slow app as we are now (owner, 2026-11-18)

Today a slow app freezes the desktop, so it is seen, reported and fixed.
Once it can stop only its own window nobody notices, and apps get worse
quietly. Isolation does not ship unless detection is at least as sharp as
today, measured from both sides:

- **Inside the app's process, the same instruments.** Every callback is
  timed with the same budgets (`DT_SLOWMS`, `DT_HOLDMS`) and the slowest
  call named with its file and line (`slowest`), as now. A stall is a
  `held:` line about the app, and **it fails the suites as a held: line
  does today** -- the prototype's `slow: ... held its own window` wording
  is withdrawn for that reason.
- **From the desktop's side, what the person feels.** It knows when each
  key or click went down and when the app's answer and cells came back, so
  it measures every app's input-to-screen latency, frame rate, CPU and
  memory per process, continuously, against the desktop's own budget (1% of
  a core with programs running, the frame budget), and names an app over it
  in the log and in a per-app view in Task Manager.
- **The gate keeps its teeth.** Suites assert an app's latency and CPU
  budget, not only that the desktop did not freeze. Whether the catalogue
  also runs an app's suites before it publishes is the owner's to say: the
  package work it belongs to is parked (ADR 0058-0062).
- **Checked, not trusted.** A check fails against a build with the
  detection taken out -- a stall that logs nothing, a latency nobody
  measures -- so the requirement cannot be lost quietly; and an app over its
  budget is shown to the person as well as logged, in a form the owner
  chooses.

## Review (advisor, 2026-11-18): what the design must settle

1. **An app runtime, not a whole desktop in a hosted mode.** The prototype's
   child is the window manager with `DT_HOSTED` branches in settings,
   budget, fullscreen, bar and input -- the parallel moved inside `wm/`.
   The app's process runs an app runtime: the widgets, and the platform
   calls an app makes, each **local** (drawing into its own window, text
   and scroll helpers, regions, timers, translation), a **request** sent to
   the desktop (a window opened, closed, retitled or focused, a note, a
   dialog, a file picker, a setting kept, a launch) or **gone**. Measured
   with `inspect calls` over the catalogue's 119 installables, none
   unreadable: they call **352** distinct platform functions -- the most
   `dt_item` (422 calls in 46 files), `dt_notep` (359), `dt_tr`, `dt_has`,
   `dt_tput`, `dt_want`, `dt_del`, `dt_sep`, `dt_wreg`, `dt_keep`, `dt_new`.
   That table, generated and checked by a tool, is this design's centre; a
   new platform function an installable calls fails the gate until it is
   classified. **Done (2026-11-18):** `tools/appcalls.py` reads every
   installable -- 216, the catalogue's and the shipped tree's, none
   unreadable -- and classes the **373** platform functions they call:
   **207 local** (the widgets, translation, glyphs, jobs, regions, timers,
   pure helpers, and facts about the display pushed to the app), **112
   requests** (windows, notes, dialogs, the clipboard, openers, file undo,
   chrome, the input method, the saver, the wallpaper, the app registry),
   **47 declarations** (menus, bar and strip items, kept settings) and **7
   gone** (the prototype's own plumbing). The table is
   `docs/adr/0050-calls.tsv`, rewritten by `tools/appcalls.py --write`.
2. **The desktop holds each hosted window's cells.** A move, an uncovering,
   a workspace switch or a restart repaints a hosted window without asking
   the app -- or the desktop would wait on it. So the console keeps a hosted
   pane's cells: that is the part of #421 (panes own their cells) this
   design needs, which was wrongly said not to be needed. Only the wire is
   open -- an escape stream through an emulator, as the prototype does, or
   cell runs as `hold` carries between processes -- and it is chosen
   against these: idle costs nothing; an app echoing a key is on screen in
   the next frame; an app redrawing a full window ten times a second costs
   the desktop no more than 1% of a core beside the app's own; and the
   heaviest installable, the Terminal, with three programs each writing
   every 10 ms -- the case CLAUDE.md has a baseline for -- costs the desktop
   no more than it does today.
3. **Every kind, said.** Of the catalogue's thirteen kinds (CAT_KINDS):
   apps (31) and system apps (7) are processes with windows; Control Panel
   panes (48), Control Strip modules (9), menu bar items (10) and savers
   (6) draw into the desktop's own surfaces, so each needs a contract of
   pushed data -- a bar item's text and glyphs, a strip module's cells, a
   pane's rows -- the desktop draws from, and a process that lives without a
   window open (a bar item, a ticker); themes (10), colour schemes (16) and
   languages (4) are data, read and never run; modules are native code in
   whichever process loads them; command-line programs (3) are processes
   already; `lib` (5) is code sourced by installables, so it runs in theirs
   -- it is not platform, as the Decision first said; bundles (6) only
   group. **Panes, counted (2026-11-18), unique by name:** of **51** panes,
   **27** set a setting the desktop keeps (a `DT_KEEP` name), **12** draw for
   themselves, **7** start programs (network, Bluetooth, power), and none is
   marked platform. **Decided (owner, 2026-11-18): a pane's code runs in a
   process of its own** -- a part in its app's process -- like every
   installable; it declares its rows, the Control Panel's window (platform)
   draws them, and a change to a desktop setting is a **request the desktop
   accepts only for a setting that pane declared on its rows**, refused
   otherwise, as the keep rule refuses a hosted app's. The 12 that draw
   draw into a region of the Control Panel's window, as a hosted window
   draws into its pane. **The unit is the installable, not the `dt_app` name**: a part
   shares its owner's state (Audio Player's pane reads the app's names), and
   one file registers several apps (Files: `rn`, `gi`, `ow`).
4. **A declined key is not an answer to cache.** `_key` returning 1 is what
   lets Escape reach Menu Bar after vi declines it. The protocol: a key goes
   down with a number, the app answers took or declined by number, the
   desktop holds that key's own fallback until the answer or `DT_HOLDMS`,
   and **holds the keys typed after it too**, sending each only once the
   one before is answered -- sending them on would hand the letter typed
   after Escape to the app instead of the menu it opened. Only a plain key
   that is also on a shortcut's list needs the round trip; every other key
   goes down at once. The key's number is also the latency measurement the
   sensitivity requirement asks for: one mechanism, not two. The table of told and pushed
   calls is built from ARCHITECTURE.md's callback table and the arity table
   in `tests/540-examples.t`, not a new list.
5. **"Read, not run" is checked, not assumed.** Registrations sit behind
   `command -v dt_app &&`, take values from variables, and include openers
   and locations; a gate check fails any installable whose registrations
   `inspect` cannot read, `dt_keep` is read by the same reader (the
   prototype's line scan is a second parser and goes), and the startup cost
   of reading them all is measured.
6. **What apps share today.** The Browser and the Video Player share one
   web module and engine set in the desktop's process. hibr-browser's
   answer (2026-11-18): two processes with an engine set each work -- the
   module has no state outside its process; a second idle set adds about
   110 MB (the engine library's pages are shared), and exists only while a
   Video window is open; the Video Player must open its tab on a profile of
   its own (`web open "" video`), since WebKit takes one network process per
   data store, so a sign-in made in the Browser is no longer the Video
   Player's; the Browser's memory gauge counts engine processes by name and
   needs the module to list its own (`web pids`); and a page that hangs the
   Video Player's engine no longer stops the Browser. Two processes, not one
   installable. Calendar, Contacts and Mail share `lib/pim.hibr`'s sync
   state the same way, and are settled the same way before the move.
6b. **The owner's desktop, counted (Blit, 2026-11-18, read only):** 87
   installed entries -- 28 panes, 20 apps, 8 bar items, 7 system apps, 5
   libraries, 2 savers, 2 strip modules, and data -- and the desktop's own
   process at 104 MB PSS, about 15% of a core while in use. A stall on that
   machine the same day, `held: pass 7068 ms ... pactl set-sink-volume`, was
   the sound bar item waiting on its server in the frame (fixed in Sound
   1.5.2): exactly what isolation ends for good.
7. **"The suites run unchanged" is counted first.** `tests/apps.py` reaches
   into an app from the test session -- calling its functions, reading its
   maps -- in about 44 places, the other suites in a handful; fixtures
   define apps inline; the idle marker stops proving a key was drawn once
   the app draws in another process; the census, `coverage.tsv` and
   `affected.py` are per process; hibr-browser's and huiamp's suites drive
   their apps the same way. Each is counted and given its answer before the
   one release.
8. **Told to every app process:** the theme, colour scheme, glyph set,
   language and cell size when they change; and a bound on what the desktop
   reads from one app a frame. **After an update** an app process must not
   go on running the old runtime against a new desktop -- the stale-session
   trap CLAUDE.md records for hold. The hello carries a protocol version;
   on a mismatch, or when the runtime was replaced on disk, each app process
   re-executes itself into the new one, keeping its descriptors as Restart
   Desktop keeps a terminal's -- so a terminal's programs, the hard case,
   survive an update -- and an app that cannot is restarted and says so.
   App processes survive a Restart Desktop and a supervisor's restart, as a
   terminal's programs do.
9. **Not a goal: containment.** An app's process has the person's rights;
   this buys stability, not a sandbox.

## The first plan (2026-11-01), superseded

In three steps, each a release that stands on its own:

1. **Panes own their cells.** A paned put and fill are offered beside
   `dp_api` (a sibling `"pane"` interface, as `"scroll"` was, so `dp_api`'s
   own version does not move); `dp->image` has taken a pane since version
   5. The five modules that write at absolute coordinates inside a window
   -- term, img, media, web and xscr -- move onto them (most, hvi, mon and
   trace take the whole console and need nothing), and the console
   composes panes at flush, bottom to top. Measured against today before it
   is kept: a frame with nothing changed must not cost more. The design and
   its review are on Gitea #421.
2. **A pane can be drawn from another process.** The console offers a pane's
   cells over a socket -- a write of cells, as `hold` already carries cells
   between processes -- so an app can draw its window from a process of its
   own. The window manager keeps focus, stacking, the menus and input, and
   hands an app its keys and clicks as messages.
3. **Apps move out one at a time**, starting with the ones that do the most
   work (the Browser, YouTube, Task Manager, Files), each a child of the
   desktop that a hang, a crash or a slow callback takes down alone. An app
   in a process of its own needs no budget, no pause and no lint rule: what
   it does with its own time is its own.

## Costs, said plainly

- Memory: a hibr process per app, about 2 MB resident each.
- Latency: a key reaches an app as a message and its window comes back as
  cells, two hops where today there are none.
- Work: a paned put beside `dp_api` and five modules, then a protocol, then each app.
- What stays in-process stays fast; an app moved out cannot be cheaper to
  draw than it is now, only unable to stop anyone else.

## Status

Step 1 accepted (owner, 2026-11-01): panes own their cells, and pictures
draw through panes, kept only if a frame costs no more than today, with a
second opinion on the rendering before it is built. Steps 2 and 3 --
drawing a pane from another process, and moving apps out -- are decided on
step 1's measurement, not before. Asked as "what is our end-all solution
for a bad app?": this is it, since every other layer (lint, the budget's
diagnostics, jobs and asynchronous verbs) protects only against an app
whose author can be asked to change it. Pausing, hiding or withholding
keys from an app is not an answer and does not come back (0.99.401).

Prototype (owner's go-ahead 2026-11-17, branch `iso-proto`, in no release):
the route that needs no step 1. An app runs in a desktop of its own
(`lib/apphost.hibr`) on a pty and is shown through the terminal window, so
its cells come back as a program's output does and its input goes down the
pty, never waited on; a hosted desktop stamps a file each time it goes
idle, and input left unanswered past `DT_HOLDMS` marks the window "not
responding" and writes `held: isolated:<app>` (`wm/isolate.hibr`).
`tests/isolation.py` passes all ten with `ISO=1` and fails all three
answers in-process, as before. Measured (`tools/isomeasure.py`, three runs,
24x80):

| | in-process | isolated |
|---|---|---|
| window open | 0.09 s | 0.25 s |
| key to screen, median | ~12 ms | ~12 ms |
| key to screen, worst | 13-15 ms | 21-26 ms |
| idle CPU, desktop + host | ~0.2% | ~0.2% + 0% |
| a window redrawn 10 times a second | 3.2-3.8% | 3.2-4.2% + host 2.6-4.0% |
| host's private memory | -- | 14.1 MB |

The 2 MB this ADR guessed is 14 MB measured: the host parses the whole
window manager again. So (owner, 2026-11-17: "continue with the
shared-memory start") the default is now a **zygote**: `apphost.hibr
--zygote` parses it once and waits on a socket, and each app is a fork of
it, asked for by `lib/appstub.hibr`, the terminal window's own program,
which parses almost nothing and passes the app its resizes. Measured by PSS
(a page shared copy-on-write counted once between the processes sharing
it), everything below the desktop:

| apps isolated | a desktop each (`DT_ISOFORK=0`) | forks of one host (default) |
|---|---|---|
| 1 | 14.6 MB | 15.9 MB |
| 3 | 43.2 MB | 18.3 MB |
| each further app | +14.4 MB | +1.1 MB (its own 0.97 MB, its stub's 0.16 MB) |

Two windows opened before the host had bound its socket started two hosts
at first; the desktop knows its host by pid, not by its socket, since. Not covered: the app's
menus (its own desktop's, drawn in its window), its settings (read at
start, not saved), the clipboard, drops and pictures.

Dry run with real apps, hosted (2026-11-17): Calculator, Files and the
Audio Player each open, draw, take keys and put their menus on the bar --
7 + 8 = 15 in a calculator in a process of its own. A hosted window now
looks like the same app's ordinary one: the app is named as itself on the
bar, and its window covers its own screen from one cell past each edge, so
its body starts at row 1, column 1 as every app assumes. Not yet, each
found by the run: an app's retitle (Files [folder]), its second windows and
dialogs (they open inside its own screen), its menu bar item and its
Control Panel pane (both its own desktop's, unseen), a fixed-size app's
window (it is resizable, as a terminal is).

Pixels, the same day: where the desktop shows pixels, a hosted window gets
a frame socket of its own (`frames serve`), handed to the app's console with
kitty made certain and its pty told what a cell measures (`term size ...
CW CH`). The console then does what it does on Blit -- the picture's pixels
go to that socket as a sealed memfd and only an `a=p` placement goes down
its terminal -- and the desktop's emulator keeps the placement (`term
images`), where the window places the socket's frames (`frames place`) on
each draw. The pixels never pass through the desktop's frame, which is the
point: decoding them there would have put every frame of a film in it.
One picture a window for now (`frames serve` gives a socket one picture);
`tests/isopix.py` checks it from both ends.
