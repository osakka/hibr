# 0049 — Every callback has a budget, and an app that keeps breaking it is paused

Status: accepted (0.99.400)

## Context

Every app in the desktop runs in the desktop's one process, between frames:
while a window's `_draw`, `_key` or a ticker runs, nothing else does -- not
the other windows, not the bar, not a video in another window, on any
display. That is the design (ADR 0020: windows are drawn, not composited)
and it is what makes an open app cost nothing.

Its failure is one slow callback stopping everything. Task Manager read the
process table in the shell once a second, 200 to 350 ms on the owner's mini
PC with a browser engine running, and every display stood still for that
long, every second, for many releases (0.99.399). Nobody saw it, for one
reason: the only timing kept was a total, and 233 ms a second spread over
27 frames reads as 8.7 ms a frame. And nothing stopped the next app doing
the same: the rule that slow work is a job (`dt_job`) was written for
transfers, never for every callback, and nothing measured it.

## Decision

**One contract, in `WRITING.md`.** Every callback returns inside the frame.
Anything that may take longer -- scanning `/proc` or a large folder,
starting a program, the network, decoding -- runs as a job, `dt_job`, whose
callback reads the result in a later frame. `dt_job` is the one way.

**One timing path, `dt_spent` (`wm/budget.hibr`).** Every callback the
desktop calls is timed the same way: tickers, a window's draw, its `_idle`,
and its key, click, mouse and wheel handlers, the last through one caller,
`dt_appcall`. The five copies of timing code there were before -- tickers,
window draws, frame parts, the flush, the frame -- are that one function
now. Each call pays two reads of the clock and a comparison; `dt_spent`
itself runs only for a run of a millisecond or more, or while
`desktop ctl stats` has its timing on. It keeps each callback's **longest
single run** always, beside the total and count the stats keep, and the
stats show it as `max_ms`: the figure that would have named Task Manager
in one reading.

**Two marks, and each is a line in the log.** A callback longer than a
frame, `DT_SLOWMS` (50 ms), is a note for whoever wrote it: `slow: NAME
took N ms`, at most once a minute for each. A callback longer than
`DT_HOLDMS` (150 ms) has stalled every display -- the gap a playing video
shows, and Task Manager's scan was 224 ms -- and is `held: NAME held the
desktop N ms`. The first mark is advice because the first draw of a heavy
pane, or one large folder listed, can pass it and stall nothing anybody
notices: the sweep that set these numbers found the Control Panel's
heavier panes at 54 to 133 ms under a loaded gate and 2.8 ms alone. The
frame's own parts -- the whole frame, its flush, the wallpaper, the menus
-- are timed and never named, since a slow frame is a slow callback inside
it, which names itself.

**A busy machine is not a stalled desktop.** A stall is a callback's time
less what the kernel kept the desktop waiting for a processor meanwhile,
from `/proc/<pid>/schedstat`, read once each time the loop wakes and again
only when a callback has run past the stall mark. Waiting on a program the
callback started, or on the disk, is its own; being taken off the processor
because everything else wanted it is not. A loaded gate timed a ticker
that writes one number to a file at 441 ms by the clock; such a run is a
`slow:` note saying how much of it was waiting, never a stall, and never
counted towards pausing an app.

**The stall line is the gate.** `tests/screen.py` reads every session's
`desktop.log` as it closes and fails the suite on a `held:` line, as it
already did on a shell error. The gate runs many desktops at once, so its
mark is twice the desktop's, and eight times that under the sanitizers,
whose build under a full gate drew the Control Panel past four. So a stalling callback cannot ship, and the line a person
reads in their own log is exactly what the release refused.

**An app that keeps stalling is paused.** Three stalls of one app's
callbacks in a minute pause it: its windows show a notice saying how long
it held the desktop, its draw, input handlers and tickers stop being
called, and Window > Resume puts it back. Its `_idle` is still called --
a terminal's program left unread keeps its pty readable, and the desktop
would spin -- and its windows always close. `desktop ctl stats` lists
paused apps.

**Before it ships, lint names it.** `hibr --explain` reports a `$( )`, a
backquote or a `<( )` inside a function the desktop calls back -- each
starts a process every time the callback runs -- unless the line says why
with `# fork: reason`. The catalogue runs it on what it publishes, so this
reaches apps nobody here wrote.

## What the first gate found

The gate this shipped through named four stalls that are real, each waiting
on a program from inside a callback: the Bluetooth reader (bluetoothctl
five times and more, 200 to 360 ms on an idle machine), the network reader
after a connect (nmcli, about 185 ms), the Browser's draw and YouTube's
input handlers (both waiting on the browser engine). All four were fixed
before it shipped rather than declared expected. The readers -- and the
sound reader beside them -- are jobs (Gitea #412). The browser module got
an asynchronous render (`web set T render async`) that Browser 2.17 turns
on, and `web send` for a script nobody reads the answer of. The
lint rule then found 41 more places a callback started a program -- the
Vault unlocking and logging in from a key, Sheet's formulas, Date & Time's
D-Bus question -- each now a job, done in-process, or bounded and saying
its bound.

What the measure judges was settled on the way. The desktop's log and its
pausing go by the clock less the time the kernel kept it waiting for a
processor, which is what a person feels; the suites go by the desktop's
own processor time (`DT_HOLDBY=cpu`), because a gate runs dozens of
desktops at once and a clock there measures the gate. With that measure
the last stall the Browser's suite named was not a wait at all but sixel's
palette taken from every page picture, 600 ms of the desktop's own
processor a page; pages take the fixed palette now, as films do.

## What it does not do

None of this stops the first slow run. The shell cannot take a callback
away mid-way: cutting one off on a timer was weighed and refused, because
it leaves the app's state half written -- a `_key` stopped between two
assignments -- and because a module's C cannot be interrupted at all. What
the desktop can promise is that a slow callback is caught three times: by
lint before it is written down, by the gate before it ships, and by the log
and the pause the first day it runs anywhere else. Taking that promise
further needs apps in processes of their own, which is ADR 0050.

The two marks are constants, not Control Panel rows: they are
diagnostics, not choices a person makes, so the rule that every setting is
a row does not apply.

## Alternatives

- **Threads.** Every callback is a script; the shell's unit of concurrency
  is the child process, and a job is one. Threads in the script loop would
  share every variable the desktop has between them.
- **A coprocess worker per app.** A second way to do background work beside
  `dt_job`. If starting a job is ever measured to be too costly, `dt_job`
  can run on a kept worker underneath, with the same call.
- **A deadline that aborts.** Refused above.
