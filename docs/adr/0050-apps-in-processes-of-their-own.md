# 0050 — Apps in processes of their own

Status: decided (owner, 2026-11-18): every installable runs in a process of
its own, by one path, with no switch -- the Decision below. The first plan
(0.99.400) and the prototype that measured it follow it, as history.
The prototype was removed (2026-11-21, #605) because the spine replaces it.

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
7. **A crash or a hang is the app's alone.** What an ended app's windows
   do is the person's to choose (owner, 2026-11-18), a row in Control Panel
   > Windows with three choices: **keep them showing their last picture and
   offer Reopen** (the default), **reopen the app at once**, or **close
   them**. Whichever is chosen, one note says the app has gone -- an event,
   once -- and the log says why it ended (its exit status or signal). A late
   app's window is marked, never paused or withheld, as the requirement
   below says. A Restart Desktop finds the host by its pid.
8. **Moved in one release, every kind** (owner, 2026-11-18: "one release,
   all kinds"), once the Review's counts are in. The suites already open apps
   through the window manager, so they run against hosted apps unchanged;
   `tests/isolation.py` goes into `all.py` then, passing without `ISO=1`,
   which no longer exists. The prototype's terminal hooks (`term_draw`'s
   notes, `term_menus`, `term_drop`, `term_canclose`, `term_paste`'s mark
   and the rest) are removed in the same change: the terminal window is a
   terminal again.
   **Done means, all of it, before that release ships:**
   - every catalogue bundle runs unmodified, or is listed with the change it
     needed and an `hibr:` floor at the release, so an older desktop keeps
     the version it can run;
   - `tests/isolation.py` is in `all.py` and passes with no switch, and the
     prototype's hooks and every `DT_HOSTED` branch grep to zero;
   - the budgets -- idle, a key's echo, a full window redrawn ten times a
     second, the Terminal's three writers -- are measured against 0.99.549,
     the last release with no isolation code on main;
   - a gate check holds what an idle pass of `dt_run` costs, since inert
     prototype code once cost 135 us a pass and nothing noticed;
   - the sensitivity checks fail against a build with detection taken out;
   - the owner is offered a trial of the candidate before it ships, as a
     second session on the mini PC beside the live one, never attached to it;
   - hibr-browser and huiamp are sent the calls table before the flip, since
     their apps live in their own repositories.
   **Updates, settled before the flip:** an app process meeting a new
   runtime either re-executes itself, keeping its descriptors (losing its
   script state and the zygote's shared pages), or the new zygote forks a
   fresh one and is handed the old one's descriptors; both are measured --
   memory, time to the first frame, what a terminal's programs see -- and
   the cheaper that keeps a terminal's programs running is taken.

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
- **Wall clock, not only processor time.** `tests/screen.py` counts a
  stall in processor time (`DT_HOLDBY=cpu`) so a loaded gate cannot trip
  it -- and a desktop waiting on a program spends none, so today no suite
  catches a desktop that waits: Sound 1.5.1's 5 s wait on a stuck mixer
  passed every suite (found 2026-11-18, `tests/sound.py` now asks by the
  wall clock with a two-second budget). Isolation moves exactly this kind
  of stall into app processes, so the per-app measure counts waiting as
  well as working, and the suites ask both.
- **Checked, not trusted.** A check fails against a build with the
  detection taken out -- a stall that logs nothing, a latency nobody
  measures -- so the requirement cannot be lost quietly; and an app over its
  budget is shown to the person as well as logged, in a form the owner
  chooses.

**The desktop's side, as built (2026-11-19, `wm/latency.hibr`).** Each of
the person's inputs -- a key, a press, a release, the wheel, never the
pointer moving or a drag -- is numbered as it is acted on, with the clock
and the processor read then (not where the wait returned: a key that comes
during a resize waits out the settling, which is the desktop's wait, never
the focused app's); the window whose input
handler it reaches is noted where every handler runs (`dt_appcall`), and
two times are kept for that window's app: input to answer, when the
desktop has finished with the input, and input to screen, at the flush of
the frame after the answer, when it drew the window -- decided at that
frame and never carried to a later one, since a terminal stays clean until
its program echoes and a slow program is not its window's app being late. Per app, with the worst of each, in
`desktop ctl stats` (`latency`). Past `DT_HOLDMS` to the screen it is a
`late:` line, judged by the same rule as `held:` (the processor the desktop
used, under `DT_HOLDBY=cpu`, which the suites set, or the clock), and the
suites fail on it as on a stall. That catches what `held:` cannot: an app
no single callback of which is a stall -- a key and the draw after it each
at 60% of the mark -- whose input still reaches the screen late. In one
process the pass that ran both is caught as well, as `held: pass`, which
names no app; `late:` is what names it, and once the app runs in a process
of its own the desktop's pass is quick and `late:` is the only line that
sees it. `tests/inputlat.py` drives exactly that, and a fast app beside it,
and fails against the build before. Input no window takes, and one whose window
that frame does not draw, is counted as answered without a draw, never as
late. The number is the one the declined-key protocol needs once
the answer comes from another process: one mechanism. 

**How it is shown (owner, 2026-11-19).** As live state, by the clock --
what the person feels, where the log's rule decides blame: while the
focused window has the person's input unanswered past a first mark, the
pointer is `progress` (an arrow with a spinner, not `wait`, since the
desktop still takes clicks), and past a second the window's title bar
carries the not-responding mark, which names the window and is the only
sign on a terminal that cannot change the pointer. Both go the moment the
answer comes, and on every other way out -- the focus moving, the window
closing, the app dying, a detach, Quit -- since a pointer shape left
behind outlives the desktop; on a held session the shape goes to the
display showing the window, as a pointer lock does. On by default, with a
row in Control Panel > Mouse that turns it off and sets both marks (0.5 s
and 3 s by default). Beside it: Close and Quit App always work on a
waiting window (Close Anyway); Task Manager has a row per app -- latency,
late count, CPU, memory, not responding, End; one note when an app is let
go or dies, an event, never a note per slow key; and nothing is dimmed,
blocked or withheld. It is built with the input slice: today a slow
callback runs inside the desktop's own loop, which can change nothing on
the screen until it returns.

## The app interface: who owns it, how it ships, how it stays one (2026-11-18)

The functions an app process takes from the desktop's own files (the
manifest, `tools/appcalls.py --manifest`: 269 functions in about 50 files)
are **not libraries**. `lib` is catalogue code several apps share, updated
by the catalogue on its own schedule; these are what an app may call of
the desktop, and updated apart from it an app would speak a protocol the
running desktop does not. So:

- **Owned and versioned by hibr**, shipped in the hibr package, and read
  from `$DT_HERE` only -- never from a scope a person or the catalogue can
  write.
- **The calls table is the contract** (`programs/desktop/interface.tsv`, shipped
  beside `conventions.json` so other repositories check against the
  installed copy, not a checkout). Each row carries the function's
  signature and the release it first appeared in. A release may add rows;
  a row removed or changed is a breaking change, and the gate fails on one
  against the last tag's table. A catalogue entry's `hibr:` floor is at
  least the newest row it uses, checked rather than written by hand.
  An optional parameter added at the end is no break: the row's `args`
  column records the release it arrived in (`3@0.99.555`), and a call
  passing that many words needs that release, so a floor follows the words
  a call passes, not only the functions it names (`dt_openers PATH KIND
  all` raises File Types; `dt_openers PATH` does not). A deliberate break --
  the meaning of a word changing -- is one named entry in `BREAKS`
  (`tools/appcalls.py`), with the tag it is held against and why; at the
  next tag it is stale and fails until removed, so an exception never
  outlives the release that made it. The first: `dt_openerverb`'s fifth word,
  a function the desktop called into, is a test letter since openers are
  declared data. Its floor keeps a newer entry off an older desktop; the
  other way about is not covered. An older catalogue copy on a newer desktop
  runs as it is: Archives' verbs are refused and logged; Vault's settings
  pane, whose jobs now say their status by exiting, reports a failed PIN
  clear as done; dBASE, and an Image Viewer installed in a person's own
  scope, lose their openers with nothing said. Nothing tells the person an
  installed entry is older than the desktop needs -- the Catalogue offers
  Update only when opened -- and that is the owner's to decide before a
  release carries a change of meaning.
- **One load order.** The shared files are laid out by concern (owner,
  2026-11-18: a few files, each named for what it does) and loaded by one
  loader file: `desktop.hibr` sources it first, the app runtime sources only
  it. A header line on each shared file is a check, not a second list.
  `tests/999-idle-tickers.t` must come out byte-identical across the move,
  since a ticker registered before `dt_ticker` exists is silently dropped.
- **One version per session.** The zygote is loaded when the desktop
  starts and every app process forks from it, so an app runs the code the
  running desktop was started with, even after an upgrade on disk; a
  zygote needed later is forked from the old one, never read from disk.
  The hello carries `HIBR_VERSION` and a protocol number, and a process
  started any other way that disagrees is refused: that app does not open,
  and a note says hibr was updated and offers Restart Desktop, which keeps
  the terminals' programs (owner, 2026-11-18) -- nothing runs half old and
  half new. (An edited development tree with the same version is not
  caught; that is said, not hidden.)
- **Background work is declared** (owner, 2026-11-18). An app process
  lives while the app has a window, unless the app declares work with
  none -- Calendar's reminders (`cv_alarms`), the calendar and contacts
  sync timer (`pe_tick`) -- in which case it keeps a process for the
  session, as a bar item does, measured and blamed like any app and shown
  in Task Manager. What only serves an open window (Files watching the
  folder it shows) is not background work and goes with the window.
- **Libraries are a second tier.** Loaded once, into the zygote, in
  `dt_libs`' order -- as today, "sourced once per desktop" -- so Calendar
  and Contacts never run two copies of `lib/pim` against one store after a
  `catalogue update`; a new copy reaches apps at the next Restart Desktop.
  The manifest separates them from the platform's files.

## The socket, as built (2026-11-18)

`runtime.hibr` (the app side), `wm/rtserve.hibr` (the desktop's end) and
`wm/rtwire.hibr` (the shared wire), driven end to end by `tests/rtchan.py`.
What it holds, each a check there:

- **Nothing listens until an app process is started**, so an idle desktop
  pays nothing; a pass with ten idle connections costs under 100 us,
  because the display says which connections woke it (`console woke`) and
  only those are read.
- **The desktop never waits on an app.** A connection is read with
  `recv -p`, which takes what is waiting and never waits: half a line and a
  pause hold the desktop up not at all, and the line is carried whole when
  its end comes. One connection has at most `DT_RTLINES` (64) lines carried
  a pass, so a flood holds nothing up; a line longer than `DT_RTLINEMAX`
  with no end lets its connection go. Nor in the other direction: every
  answer goes out through `dt_rtsay`, `send -p`, which writes what the app
  takes now and keeps the rest for it, offered again every pass -- a plain
  `send` to an app that had stopped reading waited until the app went,
  7.7 s measured, with the whole desktop stopped behind it. An app that
  never reads again is let go once `DT_RTQMAX` (1 MiB) waits for it, and
  the log names it, so a hung app cannot grow the desktop's memory. Each
  half is a check that fails with that half taken out.
- **A peer that has gone cannot end the desktop**: every answer goes out
  through `send`, which a gone peer fails rather than signals -- `send -p`
  as much as the plain one, with SIGPIPE blocked around its write. The first
  version answered with `printf`, which a vanished client turned into a
  SIGPIPE that ended the desktop -- and no suite could see it, because the
  harness ran every desktop with SIGPIPE ignored, as Python leaves it, where
  a real one has it at its default. `tests/screen.py` now gives each
  session the default, so every pty suite sees what a person's desktop
  would.
- **A carried request is timed** under its own name, as a callback is
  (`dt_spent`), with the app's name beside it.
- **An app's own process has a loop of its own** (`dt_rtrun`, 2026-11-19):
  one wait for everything -- the desktop's socket, what the app watches,
  its timers -- through `console key`, which in a process that did not open
  the console waits on those and the time alone; then the app's tickers,
  through the same runner the desktop uses (`dt_tickers`, moved whole into
  the shared `wm/loop.hibr` rather than copied); then what the desktop said,
  through the same line reader (`dt_rtlines`, in the shared wire), at most
  `DT_RTLINES` a pass. The one verb the desktop says today is `tell NAME
  words` (`dt_rttell`), which runs the app's own function -- never one the
  app interface defines, recorded before the app's file is read, and never
  by eval. Idle, it spends nothing; three hundred tells run whole and in
  order; a fifo it watches wakes it and its own ticker reads it; and it ends
  on its own when the desktop closes its socket, and when the desktop is
  killed outright. The app writes with a plain `send`: the desktop never
  blocks reading, so it never waits, and a full buffer is the brake on an
  app that floods.
- **An app process holds three of the desktop's descriptors and no more**
  (`dt_rtspawn`): standard input and output `/dev/null`, standard error the
  desktop's log, and everything else -- the terminal under another number,
  the listening socket, other apps' connections -- closed before it starts.
  Building it found three faults, each a check now: the shell's own saved
  copy of a redirected descriptor was inherited by every program started
  inside a function or group with a redirection, and by `exec prog <file`
  (`rd_save`, now kept at 10 or above and closed on exec, as bash keeps
  it), so an app started from the desktop held its terminal; the runtime's
  connect, `exec {fd}<> ... 2> /dev/null`, threw away the app's standard
  error for good, since an exec of redirections alone is permanent (CLAUDE.md
  records the trap; it was walked into again); and an app process is in the
  desktop's process group, so a desktop dying outright hung it up before it
  read that its socket had ended -- the runtime handles SIGHUP (never
  ignores it, which programs it starts would inherit) and goes in order on
  the socket's end. The zygote, which starts app processes from C, will give
  each a session of its own, which no portable shell command can.
- **Whose a process is comes from its launch** (2026-11-19, protocol 2):
  `dt_rtspawn` issues a token for the app it starts, hands it to the
  process in its environment (never in `ps`), and records whose it is; the
  hello carries it, the desktop takes the app from its record -- what the
  hello says of itself is only compared, and a difference logged -- and the
  token is spent, so a second hello with it is refused, and so is one with
  none. The runtime takes the token out of its environment as it says
  hello, so nothing the app starts is handed it. Its point is blame: the
  wrong app is never named for what another did. And `dt_rttell` addresses
  a launch, not a name, so two processes of one app are two addresses.
  Every socket the desktop and an app make with the shell's net builtins is
  close-on-exec now (accept's and connect's beside the listening one,
  `connect -U` for the app's end): a child the desktop started after an
  app connected held the desktop's end, and a child an app started held the
  app's, so neither side saw the other go while any such child lived.
- **One path for the desktop's shared state** (owner, 2026-11-19: one
  source of truth, no duplication, no compatibility, no placeholders, and
  every installable, tree and catalogue, converted in the one release --
  "bundles run unmodified" is reversed). An app's own process reads the
  desktop's state as a local, read-only copy: 30 reads through a function
  measured 160 us against 46 us as plain expansions, so reads stay
  expansions, and the copy is generated from the desktop's one owner and
  never written by its reader. What is shared is said once, in the file
  that owns it -- `dt_share [-s] [-o FIELD] NAME...` (`wm/rtwire.hibr`),
  and every kept setting, which `dt_keep` shares; `-s` is a secret (the
  vault's session key and items), `-o app` a map whose entries each belong
  to the app their `app` field names (the window table). Nothing else is
  hand-kept: what changed comes from the variable table itself, its shape
  travels with it, and who reads it is generated from the inventory
  (`mirrors.hibr`, `DT_RTAUD`).
  - **The change is seen at the variable** (core, owner's yes 2026-11-19):
    one attribute bit (`A_WATCH`), one hook pointer and six checks in the
    functions that hand out or replace a variable's content (`v_assign`,
    `v_del`, `v_setp`, `v_delp`, `v_path` when it creates, and creation
    while a watched name waits to be declared again, asked only where that
    name's hash falls). The hook is asked before the write, is told whether
    the write changes anything, and may refuse it. An unwatched write costs
    4 instructions (scalar) or 9 (subscripted), 0.13% on a write-heavy loop,
    every other function identical. The verbs are a module, `varwatch`
    (`add`, `drop`, `seq`, `moved`, `list`, `guard`, `apply`, `asks`);
    `tests/999-varwatch.t` drives 38 write forms -- json parse, read -a,
    mapfile, `:=`, namerefs, a job's hand-back, unset and re-declared, a
    local shadowing the global (not reported) -- and fails on a labelled
    line with any one of the six hooks taken out.
  - **One pass, one wire form.** Each pass asks `varwatch seq`; unchanged,
    it is one comparison (8 us idle, measured -- a `2> /dev/null` on it had
    made it 17). Changed, `varwatch moved` names each changed name, or first-
    level key of a map, and each goes to every connection that reads it:
    `json NAME KIND TEXT` for the whole (KIND its attribute, `A` a map
    declared -A, `a` a list, `s` a scalar, so the app declares it as the
    desktop does), `jsonk NAME KEY JSON` / `unsetk NAME KEY` for an entry.
    A write of the same value is not reported, so the bar laying out its
    items every frame sends nothing; the layout had written each width
    twice a frame and now writes it once. An entry that changes pass after
    pass (a window dragged) is held until it settles or `DT_RTSETTLE` (250
    ms) passes -- a drag sends its start and its end, where a window's
    record is 137 us to encode -- and a tell or a hello sends what is held
    first, so a call never reads a place the app has not been sent. Counted
    in a shipped desktop with every shared name watched: a full frame asks
    the module 30 times, about a microsecond; idle, a few standby scans a
    minute.
  - **The copy is the app's to read, never to write.** In an app's process
    `varwatch guard on` makes the variable table refuse every write to a
    shared name but the runtime's own applying of what arrived ("DT_FOCUS:
    the desktop's, read only in an app's own process"), and the runtime
    runs under `keepgoing`, so the refusal fails that command rather than
    ending the app's loop. An installable that writes the desktop's state
    today (165 names, `tools/appstate.py --writes`) is a request to convert.
  - **Deleted by it:** the hot, settings, hello, registry and window doors
    and everything they kept -- 77 hand-placed `dt_rtreg` marks and
    `dt_rtregk`, the per-door caches (`DT_RTSENT`, `DT_RTHOTSIG`,
    `DT_RTWSIG`, `DT_RTREGS`, `DT_RTGEN`, `DT_RTLINE`), the harness's hunt
    for unmarked writes, the job reader's mark, the survey's hand tables
    (`DOOR`, `FIELDDOOR`, `SECRET`, `SETDOOR`) and `mirrors.hibr`'s door,
    map, field and declaration tables. The survey's one gate now is: a
    desktop name an installable reads is shared, the app interface's own,
    or named in `CONVERT` with the slice that converts it (call, answer,
    gone, kept) -- 233 shared, 37 to convert.
  - **Review fixes** (2026-11-19): three places graft a map onto a
    variable after its assignment was asked about -- `json parse NAME`,
    `NAME=()` and `:=` of a map -- and the first two went unreported on a
    watched empty scalar; they ask again (three rows of the test, two of
    which failed before). A hello runs the pass before its connection is a
    reader, so it is never sent an entry of a name it has not had whole.
    The survey reads what is shared from the desktop itself, loaded with
    its platform files as a session loads them, so there is one list. Each
    name's whole line is kept once made and dropped when the variable table
    reports it changed: a hello encoded every name its file reads, 237 for
    a file the inventory never read, which was 31 ms a launch. Four
    functions in shared files write a shared name, which an app's guard
    refuses: `dt_hotmark` (DT), `dt_imecheck` (DT_IME), `dt_locload`
    (DT_LANG) and `dt_davclear` (DRW); each becomes a request
    (`tools/appstate.py --shared`).
  - **The inventory is the parser's** (2026-11-19): the survey reads which
    installable reads and writes each platform global from `inspect vars`
    (one process over every installable, 15 s), never a word match; a name
    local to, or a parameter of, its function is that function's own, and a
    name only passed by name (`dt_keep NAME`) is a registration, not an
    access. Against the word match it classes every name the same, and
    corrects five counts -- `:=` targets the regex read as reads (`CP`, 23
    files to 4) and writes it never saw (`DT_T`, `DT_WHY`, `PM`,
    `DT_POPCURNEXT`). The platform functions that write a shared name are
    found the same way.
- **Quit and Restart Desktop close it**: the socket goes and every app
  process's connection ends. That is not yet what Requirement 8 says --
  app processes surviving a restart -- which needs the connection kept
  across the restart's exec or made again; it is built with the zygote,
  and until then an app process ends with its desktop.

### Who is sent what (2026-11-20)

`mirrors.hibr`'s `DT_RTAUD` names, for each shared name, the installables
sent it, and it is derived, never chosen. An installable is sent what it
reads, what the shared functions it calls read -- to the end of their chain,
since a wrapper that reads nothing still runs the reader it calls -- and what
its bundle's parts read. A shared function runs only in a process that
calls it, so what it reads goes there and nowhere else; only a name code
every process loads reads at its top level goes to every process. A
function handed over by name rather than called -- a ticker, a job's
`then`, a callback -- is no call the parser sees: an installable naming one
counts as calling it, and shared code naming one more often than it calls
it hands it to any process, so what it reads goes to every process. The
first rule, "read anywhere in shared code: everyone", sent 69 names to
every app; this one sends 59, and the note history, the bar's clock and the
vault's state reach only the apps that read them. Functions that only
answer from shared state live in shared files (`winfacts`, `orders`,
`namefacts`, `wallfacts`, `prayerfacts`, `vaultfacts`), so an app answers
them from its own copy: the asked requests went from 16 to 8.

### What makes a request asked (2026-11-20)

A request is asked, never by choice, when an installable needs an answer
from it, and the parser says so (`tools/appcalls.py`, `asked()`): its value
taken (`x := f`, `$(f)`), its status tested where it stands (`inspect calls`'
`tested`: the condition of `if`, `while` and `until`, the left of `&&` and
`||`, what `!` negates, the last command of a tested group), or what it
changed read back in the same function after the call (`tools/appstate.py`'s
`backs`, by the parser's positions: ADR 0050's read-your-own-writes). Told,
any of these would answer 0 and leave the caller reading its own stale copy.
The old rule saw only `:=` and `$( )` and called 8 requests asked; this one
finds 27, from 54 once a request's predicates that only read shared state
moved to the app's own copy (`dt_hidden`, `dt_away`, `dt_capped`,
`dt_isblank`, `dt_btnhidden`, `dt_signmark`). Two status reads `tested`
cannot see are held at none (`statusholes`): `$?` read after a told request,
and a function ending in one whose own status a caller tests. An ask reachable
from a frame path -- `_draw`, `_dirty`, `_idle`, a ticker -- is a round trip
each time the path runs; the count is held (`FRAMEASKS`, 41) and only ever
lowered: 39 since the openers became declared data and `dt_isbundle` left
Files' scan. What is left there is conditional (a title changed, a note on an
error, a bell) or is a location's own (`dt_locof`, `dt_loccan`, once a
folder). Counting only code, not comments, as handing a function over
brought the names sent to every process to 24 of 246.

### Openers as declared data (2026-11-20)

What opens a file, and what else can be done to it, is data an installable
declares (`dt_opener NAME TITLE TYPES HOW [APP] [KIND]`, `dt_openerverb
TYPES LABEL APP [CALL] [TEST]`, TEST a letter: `f` a file, `e` there, `d` a
folder, `x` runnable), and every process answers from its own copy
(`wm/openfacts.hibr`): Files never calls into Archives to ask, so a folder
of 300 bundles is no 300 round trips. Registered means present: unloading a
file sets its declarations aside and reinstating it puts them back, in
their places. What a declaration says is only as fresh as the last rescan
(`dt_onrescan`: Archives declares again the kinds its tools can read), so a
zip tool installed by apt is offered after Refresh Desktop or a restart, not
the moment it lands. From an app's process a declaration names only the
app's own files (`dt_rtmine`) and never replaces another's opener.

### What a setting redraws (2026-11-20, measured, open)

dt_set makes the next frame whole and rebuilds the menus and the bar on
every call, since a setting can change anything drawn and an installable's
own hint writes (DT_FORCEONCE and the rest) do nothing from its own process.
Correct, and blunt: a Control Panel slider step -- a setter and the frame
after it -- measured 9.7 ms before and **15.9 ms** after (40 steps,
DT_TIPMS, the panel open). The way out is the setting saying what it
affects, once, on the desktop's side: `dt_onset NAME EFFECT`, effects only
the desktop's own (a whole frame, the menus, the bar, a console call, a
cache cleared), never a function an installable names -- so a tooltip
delay redraws its pane and a colour the screen. Until then every setting
pays the whole frame -- and round two put dt_set on paths that are no
click: Sheet's recalculation of a trusted sheet, whose fingerprint changing
calls `ss_trustset`, and the terminal's transparency slider, a set per step.
`dt_onset` lands before a release carries them.

### Asked requests (2026-11-19, protocol 6)

A request whose answer the app takes (`asked` in `interface.tsv`) is sent
as `ask SEQ UNTIL NAME WORDS...` and answered `answer SEQ STATUS KIND VALUE`,
KIND `s` a scalar, `j` a map as JSON, `-` nothing. The desktop answers once
the pass has sent everything the request changed, so the app reads the
window it asked for, record and all, the moment the answer is in; it runs a
request only if it is one the app's own file makes, and never one read
after UNTIL, the moment the app stopped waiting (125, not run -- or it would
make a window the app had already given up on). It never waits on an app,
so an app asking from inside a call the desktop made to it holds neither
side. The app waits on the desktop's socket alone, for `DT_RTASKMS` (2000
ms, the owner's), applying shared lines in order as they come and keeping
tells for after; it answers its caller with the request's status, 125 when
nothing came in time, 126 when the desktop refused, and drops a late answer
by its number. The wait is the desktop's, so it is logged `asked:` and never
charged to the app as a stall. Measured (2026-11-20): **1.7 ms an ask, a hundred in a row, and 3
frames for the hundred.** It was 70 ms and a frame each until two things
were fixed the same day. The desktop waits out a held frame with `console
key -q`, which ignores watched descriptors, so the app's socket is watched as
input (`console watch -i`) and a request ends that wait. And a pass that holds
a frame back skips the tickers, so the held pass serves the app's socket
itself (`dt_rttick` beside `dt_drain`) without drawing: the frame comes once
when the hold ends, however many asks were answered in it. An ask is still a
round trip through the desktop's loop, which is why the asks reachable from a
frame path are counted (`FRAMEASKS`) and only ever lowered, and why a request
whose status nobody needs is told. `tests/rtchan.py` holds it under 15 ms an
ask, measured before anything has filled the screen (300 notes on screen cost
every frame 133 ms, which an earlier reading took for the ask's cost), and its
waits read the terminal meanwhile: a desktop whose frames nobody reads blocks
writing them and then answers nothing. Finding it turned up a core bug in every
release before: once `RET` had held a map, a later scalar result kept the
map's array mark, so the window's id went out as `[]`
(`tests/999-ret-scalar-after-map.t`).

## Why not a desktop service between apps and the desktop (owner's question, 2026-11-18)

The question: would a service or daemon apps talk to, rather than code
calls tightly coupled to the desktop's functions, make things naturally
asynchronous? The answer has two halves.

**There is no separate service, because the desktop already is one.** Apps
are clients on a socket of their own, and `interface.tsv` is the protocol:
X's shape, where the server is the thing that draws. A broker between them
would add a hop to every key and every frame, and a second holder of window
state beside the process that draws the windows -- two sources of truth for
the same screen, and a latency the frame budget (0.7 ms) cannot pay. The
function-call shape is kept on the app's side only so that 210 installables
run unchanged: `requests.hibr` turns each call into a message, and most of
them -- 93 of 106 requests -- are already sent and never waited for.

**What is still call-shaped is said, not hidden.**

- **The asked requests are the synchronous residue**: 31 of 106, 13 whose
  answer is taken with `:=` and 18 whose status is tested. Each becomes a
  pushed fact, an id the app allocates, or a callback -- a change to the
  apps that call them, not plumbing beneath them. That count is what
  "naturally asynchronous" costs, and it is the measure of the work.
- **The wire names the desktop's internal functions**, which were written
  for a trusted caller in the same process. So the desktop does not trust
  the wire: a process may make only the requests its own installable's
  files make (read, never run, when it says hello, as its settings are),
  a request naming a window must name one of its own, and its identity is
  the desktop's to assign when the zygote starts it, never what its hello
  claims. Until those three hold, nothing calls `dt_rtlisten` in a real
  session.
- **What a service would have bought is apps outliving the desktop**,
  which is the owner's decision below. What stays is reconnecting across a
  restart the desktop chose: Restart Desktop carries every app's pty across
  its exec, and each app connects again to the new desktop and declares
  its windows, menus and settings again; the desktop only caches what it is
  told. That becomes a conformance check: Restart Desktop, and the screen is
  the same.

**And `hold` is that kind of service already** (owner's follow-up, the same
day). It owns a program's terminal, keeps its screen -- cells, pictures,
title, clipboard, bell -- serves any number of displays and outlives every
one of them. It and the socket are two layers, not alternatives: `hold`
keeps what a program *looks like*, which is generic, and the socket carries
what an app *means* -- its menus, its settings, whose window, which key it
took -- none of which is screen. Everything not screen that `hold` carries
was added to it one thing at a time, and pushing an app's meaning through
the screen channel is the prototype's path this ADR replaced.

Three things can go away, and each has its keeper: a display (`hold` keeps
everything), an app once isolated (the desktop keeps its window's cells),
and the desktop process itself -- which nothing outlives today: a crash
brings the desktop back from its snapshot with every terminal's program
gone, because a pty's master belongs to the process that died. **The owner
decided that stays (2026-11-18): a crash may lose programs.** The desktop
keeps owning every app's and terminal's pty; Restart Desktop, which carries
the ptys across its exec, still keeps them, and a crash does not. A keeper
that outlived the desktop -- a `hold` session per app, or the supervisor
holding each master and lending it -- was the alternative, at a process and
a second screen emulation per app for the first.

## The socket's guards, and every call by a name (2026-11-18)

The wire names the desktop's own functions, written for a trusted caller
in the same process, so the desktop does not trust the wire. The guards
are against an app's bugs and against blaming the wrong app -- containment
stays a non-goal (Decision 9) -- and each is a check in `tests/rtchan.py`,
failing against the socket before them:

- only the desktop's own user connects (`$REMOTE` after `accept`), and the
  socket's folder must be the desktop's own, as the control socket's is;
- a hello names an installed app, or is refused;
- an app makes only the requests its own file makes: the told requests the
  file's text names, read once per file, never run;
- a request whose first word is a window (`interface.tsv`'s signature says
  so) names one of the app's own windows, or a dialog opened for one of
  them (a file picker), or is refused, and the window stays.
- a request naming another installable's thing -- an opener, a verb, a
  strip module's width, a note -- acts only on the caller's own: its file
  or one of its parts (`dt_rtmine`, the one answer), or the note's owner;
  the rest is refused and logged `refused: <request>: ...`, which fails a
  suite, and the thing stays. `dt_rtmine` knows an app's files; when a
  strip module, a pane or a bar item runs in a process of its own, it has
  to learn theirs, or each is refused its own.

Still owed, and the condition for any real session to listen: the app's
identity is the desktop's to give when the zygote starts it, not what its
hello says; until then the name in a hello is believed.

**Every call by a name, classed.** The platform runs code whose name it
does not write in 94 places (inspect's dynamic calls, `tools/callsites.py`,
`programs/desktop/callsites.tsv`): 33 a window's own callback, which goes
through `dt_appcall`/`dt_appget` to reach a hosted app; 20 a callback an
app handed it (a dialog's answer, a job's end, a menu command, a note's
action), which runs in the process of whoever handed it, so its owner is
recorded where it is stored; 21 another installable's hook (a bar item, a
strip module, a location, a bus handler), which runs in that installable's
process; 16 the desktop's own; and 4 a widget calling the app that drew
it, in the same process. **The window calls have one seam** since the
same day: `dt_appfn app callback args...`, in the desktop's own
`wm/rtserve.hibr`, which every one of the 31 calls into an app's own code
from the desktop's files goes through (a window's `_draw`, `_dirty`,
`_idle`, `_open`, `_close`, `_menus`, `_stash`, `_resume`, an app's
`_launch`, `_size`, `_about`, `_context`) -- its status and its `ret`
answer the callee's, at 1.2 us a call over a direct one, a forced
three-window frame unchanged within its noise (4.1-4.6 ms against
4.2-4.6). Every app is in the desktop's process today, so it calls; it
is where a hosted app's call will be sent. A shared file (`# interface:
shared`) runs in whichever process called it, so its sites are local, and
shared code calling into an app -- `dt_idlecall` was one, and moved to
`wm/session.hibr` -- fails the check, as does a call into an app by a
name anywhere but the seam. A window's input handlers reach it the same
way: the desktop's 30 calls of `dt_appcall`/`dt_appget` (a key, a click,
the mouse, the wheel, focus, a paste, a drop) go through `dt_wincall`/
`dt_winget`, which route by the window's app through `dt_appfn` and are
timed by `dt_appcall` in the process that runs it; a direct call from a
desktop file fails the check, which a census of dynamic calls alone could
not see, since that call is made inside the shared `budget.hibr`.
**And a callback an app hands in keeps its owner**: while `dt_appfn`
runs an app's code, `DT_CALLER` is that app, and every place the desktop
stores a callback it was handed -- a menu item's command, a note's action,
a confirmation's command, a dialog's answer, a file picker's choice, the
vault's pick and its jobs, a remote transfer's end -- stores the owner
beside it and calls it back through the seam with that owner; a told
request from an app's socket runs with `DT_CALLER` as the sender. None of
the 17 is called by a name any more, and the check fails one that is.
`tests/appseam.py` drives four of them from an app's own code and one from
the desktop's, and fails against the desktop before the owners were kept.
The seam costs about 2.5 us a call with `DT_CALLER` (a frame unchanged
within its noise). **And an installable's hooks keep theirs**: every
registration -- a bar item, a bar hook, a mode hook, a strip module, a
location, a bus handler, an opener's verb, a start-up or unload hook --
records the file that registered it (`DT_LOADING`) or the app whose code
did (`DT_CALLER`), and all 24 hook calls go through `dt_hookfn`,
`dt_prefixfn` (a hook named by a prefix, which is said rather than guessed
from the name) or `dt_appfn` with it. The seam has a file of its own,
`wm/seam.hibr`, loaded first of the desktop's own so a file registering a
hook while it loads finds it there. Its cost, counted rather than timed
(the machine's clock was too noisy that day): a forced frame of three
windows with the shipped bar items and strip modules makes 16 calls through
the seam -- about 11 callbacks, 5 of them hooks, each hook entering it
twice -- about 46 us, 1% of that frame and about 6.5% of the 0.7 ms a frame
the owner's CPU bar allows; an unforced frame makes fewer. (A first count
said 8: it copied the seam with `declare -f ... | sed 1d`, and hibr prints
a one-line function on one line, so the copy had no body and nothing it
should have called was counted. Copy a function by renaming its first
line, never by cutting lines from it.) **Tickers too** (found by the advisor after the hooks were called
done, since the check classed the ticker loop as the desktop's own): a
ticker records its owner when registered (`DT_TICKOWN`: the loading file,
or the app whose code registered it at run time), and the loop runs one
that has an owner through the seam and calls the desktop's own directly,
so a pass pays the seam only for what has one -- 10 of a full desktop's
25 tickers, about 25 us a pass, beside the 45 us a ticker's timing already
costs each. **And the platform's own installable-shaped files** (also the
advisor's catch: the census read `wm/` and the widgets only): the Control
Panel's window calls into panes in 18 places -- a pane's `_rows`, `_draw`,
`_key`, `_click`, `_mouse`, `_wheel`, `_do`, `_drop`, `_order`, `_slide`,
a row's setter and its namer -- and the saver framework into savers in 2.
`cp_pane` and `sv_saver` record their owner, and every one of the 20 goes
through `dt_prefixfn` or `dt_appfn`; the saver framework run on its own,
with no desktop (`sv_main`), calls its saver directly, which is local by
definition. The census now reads every `# platform:` file too. What is left of the platform's
calls by a name is 25: the seam itself, 12 of the desktop's own (a program
it starts, its own tickers) and 12 in shared code, which runs in whichever
process called it -- each followed up its callers, so one the desktop
reaches is local only for a stated reason (see "How each call into an app
is met" below: three were not, and go through the seam now). `tests/appcalls.py` fails on a site no rule classes, and on any difference from the committed table -- keyed on the
function and the call's own text, not its line -- so a new call inside a
listed function is classed on purpose.

## How each call into an app is met, and what happens when it is not answered (2026-11-18)

Once an app has a process, every call the desktop makes into it through
the seam has to be met some way across the socket, and a call that is not
answered has to have an outcome the person can live with. Both are decided
**per kind of call** -- a window's `_draw`, `_key`, `_menus`; a bar item's
press; a pane's rows; a callback an app handed over; a hook -- because the
wire is per kind, not per place in the source. `tools/seamcalls.py` takes
every seam call from inspect (127 in the platform's files, shared code
included), resolves the
variable ones to their kind from the field they were read out of
(`DT[$id]["draw"]`, `DT_BARI[...]["press"]`, `FP[$id]["cb"]`), and reads
what the caller does with the answer:

- **a value** (10): it is bound with `:=`, or `$RET` is read just after it;
- **a status** (24): it is tested (`if`, `!`, `&&`/`||` after it, a
  `then`: 14 -- but not `|| true`, which reads nothing), a `$?` on the
  next line reads it (2), or it is its
  function's last command or followed by a bare `return`, so its status is
  that function's -- counted only when some caller of that function reads
  *its* status in turn, followed up the chain (8 are, 5 are not, and
  `right) cp_do` is how a pane's `_do` becomes the Control Panel's "was
  this key handled"). A call merely preceded by `&&` (`[ -n "$dr" ] &&
  dt_appfn ...`) is none of these;
- **nothing** (93).

Each of the 69 kinds is then met one of three ways:

- **push** (20 kinds, 28 calls): the app sends it whenever it changes, and
  the desktop uses the last it sent -- a window's cells and dirty state,
  its menus, context and title menus, its name and size, a pane's rows and
  cells, a strip module's and a saver's cells, a bar item's, the words a
  list of choices shows (sent with the list when it opens). Everything a
  frame asks for is here, so **a frame never waits on an app process**:
  sixteen round trips a frame would not fit the 0.7 ms frame budget, and a
  hung app would stop the desktop, which is the thing this ADR exists to
  end. Not pushed yet means the last one stands.
- **ask** (17 kinds, 51 calls): a round trip the desktop waits for, at most
  `DT_HOLDMS`, each kind with its outcome when no answer comes -- a key the
  desktop also acts on is declined, its action runs and the key is revoked
  (the declined-key section above); the window goes when `_close` is not
  answered, and its process finishes its own close and is ended only if it
  has not gone a few seconds later, so an app that saves on close is not
  cut off; a restart goes on without `_stash` and the window reopens
  fresh; a handed callback leaves its dialog
  up; a location's `_ok` is refused with the reason it gave, which is the
  one answer that carries a global (`DT_WHY`) back with it; Copy finds
  nothing selected. Each such outcome also marks the window not
  responding.
- **tell** (32 kinds, 48 calls): sent, and the desktop goes on -- input,
  focus and lifecycle news, whose effect reaches the screen through the
  requests the callback makes, in order, and the next frame.

The table is `programs/desktop/seamcalls.tsv`, its kinds at the end with
how each is met and what no answer means. `tests/appcalls.py` fails when it
differs from the source, when a call resolves to a kind the tool does not
decide, and when a told kind's answer is read anywhere -- so a new kind,
or a new caller that starts reading what a told callback answers, is a
decision somebody makes rather than a default. Counting found the shapes a
reading by eye gets wrong, both in the direction that is a bug rather
than a delay: a tail through a `case` arm or an `else`, and a status read
one line later by `return $?`.

**And it found three calls into apps that went around the seam.** Shared
code -- the app interface -- runs in whichever process calls it, which the
census took to mean every call it makes by a name is local. Not when the
desktop is the one running it: the desktop's mouse path drags a
scrollbar, which told the window's `_scroll` directly; a text field the
desktop types into (and Copy, Cut and Paste in one, and the input
method's commit) called the window's `_key` through `dt_appcall` itself;
and building a list of choices called the namer the app had set, while
the desktop built its menu. All three ran in the desktop's process as
the desktop's, and would have kept doing so with apps in processes of
their own. The seam is therefore part of the shared interface now (in an
app's own process every owner is that process, and a call is a call), the
three go through it, and `tools/callsites.py` follows every shared
function's callers up to the first file of the desktop's own: a call by a
name in shared code the desktop reaches fails unless it is listed with the
reason it is not an app's (a text's pen is the drawing code's own; a job
runs a program). `tests/appseam.py` drives each of the three from the
desktop's side and fails against the code before.

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
   installable -- 210, the catalogue's and the shipped tree's less the
   platform's own `# platform:` files, none unreadable -- and classes the
   **360** platform functions they call: **206 local** (the widgets,
   translation, glyphs, jobs, regions, timers, pure helpers, and facts about
   the display pushed to the app), **106 requests** (windows, notes,
   dialogs, the clipboard, openers, file undo, chrome, the input method, the
   saver, the wallpaper, the app registry, the displays' layout), **43
   declarations** (menus, bar and strip items, kept settings) and **5 gone**
   (the prototype's hooks in `apps/term.hibr`, which go with it). The table
   is `programs/desktop/interface.tsv`, rewritten by `tools/appcalls.py --write`.
   **The platform's state, surveyed (2026-11-18):** `tools/appstate.py`
   (`docs/adr/0050-state.tsv`) finds installables touching **343** of the
   platform's 1,023 globals and writing **186** -- a word match, so a lower
   bound on what a parse would find; the first version missed the tables
   declared `declare -A` without `-g`, among them the window table `DT`,
   read directly in **27** installables and written in 3 (the Video app's
   mini player sets its own window's chrome, shadow and place there, which
   become requests), and `DT_APPS` read in 6. They fall into: pushed
   mirrors the app only reads -- the theme's colours (`DT_FACE` and the rest,
   read in about 100 files), the glyphs (`GL`, 115), the screen's size, the
   mirroring flag, the wheel's step, the focus (`DT_FOCUS`, 56); the app's
   own -- the dialog's geometry, the job table (`DTJ`), a text area's state;
   declarations -- a pane's rows (`CP`, written in 82); and requests -- a
   desktop setting a pane writes (the wallpaper, the buttons, the cursor),
   accepted against its declared rows, and hints such as `DT_FORCEONCE`.
   **Each mirror's door (2026-11-19).** Pushing every name every frame
   costs the frame what isolation was meant to save, and hooking every
   writer is a list nobody keeps; so each name the survey calls a mirror
   or a setting is given the one place the desktop already changes it
   through (`DOOR` in `tools/appstate.py`, the table's last column):
   **save** for the 174 settings (`dt_save` walks every kept setting on
   each change); **registry** for 40 tables changed only when something
   registers, goes or is rescanned (the app list, bar items, openers,
   places, keyboards, the clipboard and notification histories, the
   vault's state); **hot** for 14 scalars compared as one string a pass
   while an app is connected (the focus, the screen's size, the workspace,
   the primary display, the update marks); **hello** for this desktop's
   constants; **call** for 16 that move with input or a frame -- the
   pointer, a menu's place, a drag, a key's phase, a cell's pixels -- which
   travel in the call that uses them and are never mirrored; **answer** for
   the dialog geometry an asked request sets; and **gone** for the
   prototype's. A mirror with no door fails the survey, and so does a door
   for a name no installable reads (where the catalogue is read too). What
   a setting arriving derives in the app -- the glyph table from
   `DT_GLYPHSET`, the language catalogue from `DT_LANG` -- is rebuilt by
   one function both ends run after a batch of settings.
   **Told and asked (2026-11-18):** of the 106 requests, **93 are told** --
   sent, never waited for -- and **13 are asked**, their answer taken with
   `:=` (the table's `answer` column). They resolve three ways, none a
   round trip in a frame: **`dt_new`** (61 calls) takes an id the app
   process allocates from a block the desktop hands it at the hello, so the
   next line has it; **`dt_ids`, `dt_locof` and `dt_openertitle`** read the
   desktop's window list and its location and opener registries, which the
   desktop pushes as mirrors, as it pushes the theme; and the rest
   (`dt_launch`, `dt_place`, `dt_openbrowse`, `dt_walldrawn`, `dt_pbar`,
   `dt_notehist_what`) answer later, as a callback, each converted by hand.
   **Asked through the status as well:** a request whose exit status an
   installable tests (`if`, `while`, `!`, or a statement going on with `&&`
   or `||`) is asked too, which the `:=` count misses: predicates over the
   desktop's state -- `dt_hidden`, `dt_btnhidden`, `dt_loccan`, `dt_away`,
   `dt_capped`, `dt_termhere`, `dt_ptrlockcan`, and `dt_title` with them --
   become pushed facts; actions whose failure the app checks -- `dt_save`,
   `dt_fopsave`, `dt_shot`, `dt_setgeom`, `dt_saverstart`, `dt_saverload`,
   `dt_progsave`, `dt_progload`, `dt_notehist_run`, `dt_colours`,
   `dt_appvia` -- answer by callback, or the desktop reports the failure
   itself and the app's branch goes. That is 31 asked of 106, not 13.
   **Local, through the call graph:** `tools/appcalls.py` walks the
   platform's own calls from every local function; **29** reach a request
   (the `reaches` column). Most send one -- a note, the clipboard, a dialog,
   a launch -- which local code may, since nothing waits. Three read desktop
   state and become pushed facts: `dt_size` (the displays' layout),
   `dt_keyphases` (whether a window is hidden), `dt_cellpx` (what a display
   can draw). The walk is a **lower bound**: it starts only from functions
   installables call by name, so a local function reaching desktop state
   through a helper of its own passes it; the classed globals are its other
   input, and each local function's reachable bodies are checked against
   the refused globals once those are classed. **The input method is the
   one real question** (proposed, not decided): the text widgets
   (`dt_textdraw`, `dt_textkey`, the text area, search, the start menu's
   field) ask it for every key and every draw, and `wm/ime.hibr` keeps one
   engine in the desktop, the words a person teaches it with it. Proposed:
   an engine runs in each app process **as well as** the desktop's own (for
   its own fields: the start menu, its dialogs, the Control Panel's window),
   the method chosen pushed like the theme. A word taught needs no request
   and no push: hibr-ime appends it to the person's own file, and every
   engine sees the file change and reloads its rules itself (`im_fresh`,
   0.15 ms) -- hibr and Blit already teach side by side this way. What it
   costs, measured 2026-11-18: an Arabizi engine takes **100 ms** to open
   and **14.5 MB** (PSS), nearly all of it word lists -- so ten typing apps
   would be 145 MB unless the zygote loads the data while a keyboard names
   a method and every app shares its pages. **Decided (owner, 2026-11-18):
   in every app, the data shared**, and it measures as shared: a process
   that has loaded Arabizi (14.5 MB) forks three children, each opens its
   own engine in **0 ms** and types a word, and each owns **0.1 MB** of its
   own -- the four together cost about one engine. Loaded **after** the
   fork it is not shared: three running apps that each open one when the
   keyboard switches to Arabizi mid-session take **143 to 150 ms** each --
   a stall at the budget's own line -- and **14.6 MB** of their own. So the
   zygote loads, when it starts, every method a configured keyboard names
   (`ime_feed` checks the person's own layer each time, `im_fresh`, so a
   word taught later still reaches every engine); a keyboard added during
   the session costs the apps already running that load once, and the
   zygote starts again for the next. The cycle and teach chords are taken in the desktop's
   key path before the window, and only while the desktop's engine holds a
   word (`dt_imeaction`); with the word held in the app, the app tells the
   desktop it holds one, and those chords go down to it. A held word is
   committed by the app when its window loses the keys, so a hung app keeps
   its word uncommitted. The alternative -- every key to the desktop's
   engine and the preedit back -- is a round trip per key while a method is
   on. **The 343 globals, classed (2026-11-18)** in
   `docs/adr/0050-state.tsv` by `tools/appstate.py`: **173 settings** (a
   `DT_KEEP` name, as the desktop lists them: pushed to read, changed only
   by a request the desktop accepts against a pane's declared rows), **87
   mirrors** (desktop state pushed read-only: the theme's colours, the
   focus, the screen, the dialog geometry, the window table), **50 local**
   (widget and translation state, glyphs, jobs, a module's own status
   variable, the input method's state), **4 declarations** (a pane's rows),
   **9 arguments** (a value handed to a request through a global -- what
   to open, the clipboard's kind, a note's sender -- which becomes the
   request's argument), **4 gone** (hints to redraw, and the prototype's
   counter), and **16 requests**: desktop state an app changes today --
   the notification history's order, queue and unseen count, the Control
   Strip's modules, the menu editor's own entries, a display's wallpaper,
   the vault's state, the power action in progress, the displays' layout,
   the file picker -- each a named request.
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
   no more than it does today; nor does a screen saver redrawing the whole
   screen every frame, the heaviest hosted window there is.
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
   otherwise, as the keep rule refuses a hosted app's. **What "declared"
   is, mechanically:** a row names a setter function, not a variable, so
   the allowlist is the `DT_KEEP` names a process's own files assign, read
   from those files (inspect, never run) when it starts. That also covers
   the eight settings written outside a pane -- the image viewer's Set as
   Wallpaper (`DT_WALLIMG`), Prayer Times' place (`DT_PLAT`, `DT_PLON`),
   the cursor, shadow and wallpaper strip modules (`DT_CURSOR`,
   `DT_SHADOW`, `DT_GLYPH`, `DT_WALLIMG`) and the network bar item
   (`DT_NETRULE`) -- which have no rows to declare them on. **And an app
   reads what it just wrote:** a pane sets a setting and draws its row from
   it, the Video player sets its window's place and reads it back, `dt_new`
   is followed by `DT[$id]` -- so the runtime applies its own change to its
   mirror at once, sends the request, and puts the old value back if the
   desktop refuses it, saying so in the log. The 12 that draw
   draw into a region of the Control Panel's window, as a hosted window
   draws into its pane. **The unit is the installable, not the `dt_app` name**: a part
   shares its owner's state (Audio Player's pane reads the app's names), and
   one file registers several apps (Files: `rn`, `gi`, `ow`).
4. **A declined key is not an answer to cache.** `_key` returning 1 is what
   lets Escape reach Menu Bar after vi declines it. The protocol: a key goes
   down with a number, the app answers took or declined by number, the
   desktop holds that key's own fallback until the answer or `DT_HOLDMS`,
   and **holds the input that comes after it too** -- keys and clicks --
   sending each only once the
   one before is answered -- sending them on would hand the letter typed
   after Escape to the app instead of the menu it opened. **Which keys need
   the round trip, read from `wm/input.hibr`:** Menu Bar's chords and the
   Start key are taken before any window, and need none; then the focused
   window is offered the key; a key it declines falls through to the plain
   Menu Bar key (Escape) and then to **every** desktop action checked after
   the window -- close, quit, cycle, move, resize, the workspaces, the
   screenshot and the rest of `DT_KEYS`. So a key matching any of those
   (what `dt_keyassigned` already answers) waits for the app's answer; every
   other key goes down at once, since declining it does nothing. **A key
   whose answer does not come within `DT_HOLDMS`** is taken as declined --
   its desktop action runs, so a hung app cannot keep the person from
   closing or leaving it -- and the app's late answer is discarded. The app
   still holds that key and acts on it when it wakes, so a timed-out key
   would be **done twice**; the desktop therefore sends a revoke by the
   key's number, which the runtime honours before it dispatches the key. A
   loaded gate will miss 150 ms now and then, so the suites drive this with
   a stand-in that is slow on purpose, never with timing luck. The key's number is also the latency measurement the
   sensitivity requirement asks for: one mechanism, not two. The table of
   asked, pushed and told calls is `tools/seamcalls.py`'s, taken from the
   source rather than written by hand, its kinds the callbacks in
   ARCHITECTURE.md's table (see "How each call into an app is met" above).
   A key's fallback acts on the focused window as it stands, so **every
   input behind a key awaiting its answer is held, clicks as well as keys**:
   nothing can move the focus between a key and what it does.
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
6a. **Cross-cutting is not a shared process (2026-11-18).** Installables
   call into each other in **283** places counted by reading (the logic of
   `tests/deps.py`, over the shipped tree and the catalogue): **66** calls
   in 44 places stay within one entry and its parts, which share a process
   and are measured and blamed as one; **157** calls in 79 places are
   between the members of one catalogue bundle -- the Sound pane driving
   the Sound bar item, and Bluetooth, Network, Power, Longwire and Wiretap
   the same; **25** in 16 places are a library calling the app that sources
   it, which is one process already; and only **35** calls in **28** places
   link separate features -- Mail drawing Calendar's invitation card and
   adding to Contacts, Calendar and Contacts reading Mail's accounts, the
   Browser and Boxer, Archives asking Files to list again, the cursor
   module and the Terminal, the theme module and Appearance. Every one of
   these sits behind a `dt_has` guard (ADR 0046), which in a process of its
   own is false: **the feature goes quiet with no error**, so each is
   converted, never left. Shared code moves into a library each process
   sources (Calendar's card for Mail); an action becomes a request the
   desktop routes to the other app's process (add a contact, list a folder
   again), as the clipboard and open-with already are. Two separate
   features are never put in one process: from outside they could not be
   told apart, which the sensitivity requirement forbids. **A bundle's
   members are one process (owner, 2026-11-18)**: one feature split by
   kind, measured and blamed under the bundle's name, which is the name the
   person sees; a member installed alone runs alone. The 157 calls stay as
   they are. **What says so on an installed machine is `# needs:`**, the
   only link an installed file carries: the members of a process are the
   desktop installables joined by `needs`, leaving out command-line entries
   and libraries (or Calendar, Contacts and Mail would merge through
   `lib/pim`). Over the catalogue that rule gives exactly the six bundles
   -- Wiretap's four entries, Longwire's three, Bluetooth, Network, Power
   and Sound's two each -- and merges nothing else. The Power strip module
   needs nothing, as the owner made each Power piece installable on its
   own, so it is a process of its own and its two calls into the bar item
   are converted. Library state does fork: each process has its
   own job table, so Calendar and Contacts could each start a sync of one
   server; both sync scripts already take a lock folder on disk and the
   second answers "Another sync is running", so the store is safe. More
   changes than queueing, though: who is told a sync finished (`pe_onsync`)
   and the status shown (`PES`) are per process, so Calendar would not hear
   that Contacts' process synced the store they share. Each process watches
   the store folder itself (`dirwatch`) and reads the status from the store,
   not from a sibling's memory.
6b. **The owner's desktop, counted (Blit, 2026-11-18, read only):** 87
   installed entries -- 28 panes, 20 apps, 8 bar items, 7 system apps, 5
   libraries, 2 savers, 2 strip modules, and data -- and the desktop's own
   process at 104 MB PSS, about 15% of a core while in use. A stall on that
   machine the same day, `held: pass 7068 ms ... pactl set-sink-volume`, was
   the sound bar item waiting on its server in the frame (fixed in Sound
   1.5.2 and 1.5.3): exactly what isolation ends for good. **Projected:** a
   bar item or strip module runs for the session (10 processes here), an
   app while it has a window (about 5 on a working day), a pane only while
   the Control Panel shows it, a saver only while it runs; a library runs
   in the process of the app that sources it, and themes, colours and
   languages are data with none. Counted by process group rather than by
   entry: the four bar items the package ships are platform and have no
   process, and a bundle's bar item, strip module and pane are one -- so
   the owner's always-running set is about four (Wiretap, Longwire, the
   keyboard, the tray), and with apps open **about 9 to 10 processes**, at
   the prototype's measured cost (14.6 MB for the zygote, 1.1 MB each
   beside it) **about 26 MB** beside the desktop's 104 MB, an app at rest
   waking nothing. Measured again on the owner's set, with Blit, before the flip.
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
   App processes survive a Restart Desktop, as a terminal's programs do:
   their ptys are carried across its exec, and each connects again. A crash
   is another matter, and the owner decided it may lose programs (below,
   "Why not a desktop service").
9. **Not a goal: containment.** An app's process has the person's rights;
   this buys stability, not a sandbox.

## Cells, measured (2026-11-19)

How an app's drawing reaches its window when the app is a process of its
own was measured before it was designed: the real Clock and Calculator,
every draw call unchanged, and a 24x80 window changing every cell, each
carried several ways between two processes in lockstep, compared first for
correctness (every carrier's window, cell by cell, the same as in-process)
and then by instructions with callgrind, one side at a time, since the box
was at load 6. About 5,000 instructions are a microsecond here, so the
desktop's 0.7 ms frame is about 3.5 M.

Desktop's cost per frame, thousands of instructions (F a whole frame, O one
cell changed):

| carried as | Clock 7x26 F / O | Calculator 16x24 F / O | 24x80 F / O |
|---|---|---|---|
| in-process today | 216 / 216 | 1679 / 1679 | 2107 / 2107 |
| each draw call a socket line | 1728 / 1725 | 15890 / 15918 | 18320 / - |
| only the changed calls | 1893 / 1124 | 17637 / 1244 | 19987 / 1342 |
| changed cells, split in script | 699 / 453 | 1455 / 562 | 3067 / 581 |
| **changed cells, read and applied in C** | **176 / 81** | **385 / 103** | **1174 / 99** |
| the app's own terminal, through `term` | 321 / 294 | 537 / 553 | 2774 / 1903 |

So carrying cells as script work at the desktop's end is ruled out: the
calculator's frame cost the desktop nine times what running it in-process
does, four times the whole frame budget, and splitting a 3 kB line in
script is 1.4 M of it on its own. The way is **changed cells, as records the
console reads from the socket and applies in C**, which is cheaper at the
desktop than in-process for every app and frame measured, because the app's
drawing script no longer runs there; the one-cell figures include the
harness's own handshake, so they are an upper bound. A terminal per app
works and comes second: `term draw` repaints the whole window each frame,
so one cell on 24x80 costs what in-process does, and each app needs a pty
and two grids. Shared memory saves only the encode, the system call and the
parse, which are already small, and would let a misbehaving app's memory
reach the desktop; it is not built.

What the console needs, in C, for that way:

1. **A headless grid in the app's process.** `console put`, `pen` and `fill`
   with `-p` write into it with no terminal; `cn_flush`'s diff against what
   was last sent makes a record of runs (row, column, colours, attributes,
   text, wide and combining characters, underline colour, links). The
   runtime clears it before each `_draw`, or a shorter string leaves a
   stale cell (measured: `Novemberr`).
2. **A verb at the desktop** that reads a connection's cell records without
   waiting, applies them to the pane, and keeps them in the pane's own kept
   grid -- the hosted pane of #421 -- with the socket's guards rebuilt in C:
   a bounded amount per pass, a longest line, the connection dropped past
   it, never a wait.
3. **A repaint from that kept grid** for a move, an uncovering, a workspace
   and a restart, straight into the back grid (1.05 M through `dp->put` at
   24x80).
4. **Cell records on a channel of their own**, or the console taking them
   and handing everything else to `rtserve`; pictures stay on `frames`.

Memory, from the structures: the kept grid is 40 bytes a cell (77 kB for
24x80), the app's grid and its last-sent copy twice that, beside the +1.1 MB
an app costs as a fork of one host. Still to measure: wall time for the C
path beside the others under one load, its handshake alone, and a repaint
after many frames rather than one. The harness and every result are kept
with the work (`cellprobe`).

### The cell record (2026-11-20)

What an app's console sends for its windows, and the desktop's console
applies: lines on a stream socket of the app's own, each one letter and its
fields, separated by single spaces and ended by a newline. Positions and
counts are decimal; colours and attributes are hex. A line that carries
text says its length first, and the reader takes that many bytes whatever
they are, so the newline after them ends the line and nothing inside them
can. Nothing in it is for a terminal -- no cursor moves, no SGR, no erases,
no repeats, no synchronized-update pair -- because what reads it is a grid,
not a screen.

```text
w VIEW GEN                          the frame that follows is this view's, drawn at generation GEN
s ROWS COLS                         its size: the kept grid starts again, every cell none
l ID LEN URI                        link ID is the LEN bytes of URI, from here on
t ID PCT FB MATCH                   translucency ID is PCT percent over FB, MATCH counting as none
r ROW COL FG BG AT UL LK LEN TEXT   cells from ROW, COL in one pen
n ROW COL COUNT                     COUNT cells the app no longer draws
e                                   the frame is whole
```

- **VIEW** is a rectangle the desktop gave this connection to draw in, by
  a name both ends use: the app's console knows it as the pane its writes
  go through (`w12`), the desktop as a place -- a pane and a rectangle of
  it, or a rectangle of the screen -- bound to this connection alone (the
  table below). **GEN** is the binding's generation, which the desktop
  moves on every change of the view's size and hands the app with the new
  size; it is how a frame drawn before a resize is told from one drawn
  after.
- **ROW, COL** are the view's own rectangle from 0, 0 at its top left --
  after its origin (`console pane origin`), which is where `cn_pput` lands
  a write once it has taken the origin off, and so what the desktop adds
  the view's place to.
- **FG, BG, UL** are the console's own colour word: `0` the terminal's
  default, `10000NN` palette entry NN, `2RRGGBB` a colour. UL is the line
  colour framing and underline are drawn in (SGR 58), `0` the foreground.
- **AT** is the console's attribute word: bits 0-6 the `DP_` attributes,
  7-9 the framing kind (SGR 51, `DP_FRAME`), 10-14 the shadow (SGR 66),
  18-19 the icon colours (SGR 68), 17 *filled* (`CN_FILLED`: a fill's
  pattern, which a half-block shadow may replace), and 20 *kept*: the pen
  said `under` over a cell the app had not drawn, so BG is only its
  fallback and the desktop keeps the background its own drawing has there,
  taking BG where that is the default -- the `under:COLOUR` rule, resolved
  where the cell beneath is known. Bits 21-23 are the translucency the
  cell was written under: `0` the window's own, whatever the desktop has
  in force as it writes the cell (Control Panel's window translucency, as
  around an in-process `_draw` today), or an ID a `t` line named, for a
  cell written while the app's own `console translucent` was in force (the
  Terminal's). `CN_SHADOWED` is the desktop's own mark and is never sent.
- **LK** is `0` for none or an ID an `l` line on this connection named;
  the desktop takes the URI into its own link table at the `l`, so a run
  carries a number. An `l` may name an ID again, for the runs after it:
  the app's console gives a new URI an ID no cell of its grids still
  carries, so the IDs live at once are never more than its cells, and
  never more than `CN_CWLINKS` (1024), past which a further link is drawn
  as text. A URI
  longer than `CN_CWURI` (2 kB) is sent as no link, its text drawn as it
  is, and the URIs live on one connection at once are at most `CN_CWLKB`
  (64 kB) together, an `l` giving an ID again freeing what it held; past
  that the app's console sends the link as none.
- **t** names a translucency the app set, `PCT` 0 being the app's own
  `console translucent off` -- opaque, which is not the same as the
  window's. Seven at once, IDs 1-7, given again the way link IDs are; an
  eighth at once is drawn as the window's, said once in the app's log.
- **TEXT** is exactly LEN bytes of UTF-8. It is laid into cells as `cn_put`
  lays a string: a wide glyph takes two cells, a zero-width code point (a
  combining mark, or a control) joins the cell before it, and
  right-to-left text is already in display order, the app's console having
  done it. A run is cut at a glyph, never through one: it does not begin on
  a wide glyph's right half or with a zero-width code point. One cell's
  text is at most `CN_CWCELL` (32) bytes, its glyph and the marks that fit;
  marks past that are dropped by the app's console, the one thing a cell
  can hold in-process and not here.
- **n** is a cell the app's `_draw` did not write this frame: the desktop's
  own drawing shows there, as a cell an in-process app does not write
  shows its face or its frame today. The app's grid is cleared to none
  before each `_draw`, so a shorter string leaves no stale cell
  (`Novemberr`). A none touches only this connection's kept grid for this
  view, never a cell another process or the desktop drew. The desktop
  cannot put its own drawing back under a cell by itself -- the face is a
  fill `dt_win` makes, kept nowhere -- so a frame with a none in it is
  answered as *uncovered*, and whoever owns the rest of the view's place
  (the table below) draws it whole: the window manager a window's face,
  the kept grid over it and its frame, the Control Panel its own window.
  That is what an in-process window costs on every frame it is drawn; a
  frame that only changes cells costs the cells alone. How often a frame
  carries one, measured with the C (2026-11-20, the app's own console
  writing its records to a file, 24x80): Files moving its selection down
  /usr/bin a row at a time until the list scrolls, 0 of 39 frames; Write
  deleting a line's text a character at a time, 0 of 39. Both paint a row's
  whole width -- a list row, a line of text and the face after it -- so a
  shorter string leaves nothing undrawn. (The fixture had no `dt_locof` and
  no icon colour, so Files drew its icons with a refusal each; the rows are
  the same width either way.)
- **e** ends the frame. The desktop applies a frame only once its `e` has
  arrived, all of it at once, so no window is ever shown half drawn.

**`l` and `t` are the connection's, not a frame's.** They come before the
`w` of the frame that first uses them, outside any frame, and are applied
as they are read -- never kept back with a frame and never thrown away
with one -- so a frame discarded below takes nothing the app believes the
desktop has.

A view's first frame on a connection, and the first after its size
changes, begins with `s` and carries every cell the app drew; so does any
frame the desktop asks for again, since an `s` may open any frame. Restart
Desktop is not one of those: it ends every app's connection before its
exec (`dt_rtclose`), and an app's next connection is new at both ends --
its links, its translucencies and what it last sent start again, and so
does every view. After that a frame carries only the cells that changed
since the last one, as runs of one pen along a row -- a gap of unchanged
cells in the same pen shorter than a run's header may be carried inside
the run rather than cutting it -- and a view whose cells did not change
sends nothing, not even `w` and `e`. Translucency, the `under` rule and an opaque background over a
picture (`cn_bgfor`, `cn_opaque`) are applied at the desktop as each cell
is written, exactly as they are to an app's write today, because only the
desktop knows what is beneath; the record carries what the app asked for.

Every installable kind that draws cells draws them through a view, and
each view is one connection's alone, at one place; the rest of that place
is somebody else's, and is what an uncovered view asks to be drawn again.
The desktop refuses to bind a view over cells another connection's view
already holds.

| kind | draws today | its view is bound to | the rest of the place is |
|---|---|---|---|
| an app's window | `console put -p w$id`, the pane's own coordinates | pane `w$id`, the whole pane | the window manager's: the face before, the frame after |
| a Control Panel pane | `-p w$id` into the panel's window, `h` by `bw` from column `bx` | pane `w$id`, that rectangle of it, with the app's pane origin at its corner | the Control Panel's (platform, in the desktop): its list, divider and face |
| a Control Strip module | the screen at the row and column it is given, `CS_MODS[m]["w"]` wide | the screen, its slot inside the brackets | the strip's: brackets, arrows, face |
| a menu bar item that draws (the tray's `dt_traydraw`) | pane `bar` through `dt_barput` | pane `bar`, its slot | the bar's |
| a menu bar item of text (`dt_baritem`) | nothing: the bar draws the text it sends | no view -- a request, not cells | -- |
| a saver | the screen, all of it, from `console clear` | the screen, all of it | nothing: it is the whole screen |

None of these needed a change to the record. A view of the screen and a
view of a pane differ only in where the desktop puts its rectangle, and a
module that draws at screen coordinates in the app's process -- the
strip's, a saver, the Terminal's `term draw` through `dp->put` -- lands in
the app's headless screen at the place the runtime gave its view, which is
what the diff walks.

**A stale frame is a race, not a broken app.** A view resized while a
frame drawn at its old size is in flight, or closed while one is, is
ordinary, and the connection carries the app's other views. So a frame
whose `w` names a view the connection no longer has, or a generation that
is not the view's own, is skipped to its `e` -- its lines read and let go
as they come, never held, since there is no size to bound them by -- and
logged; for a closed view that is the end of it, and for a resized
one the app's next frame opens with `s` anyway, its grid having taken the
new size. A frame of the current generation that has no `s` where the
desktop holds no kept grid for it, or whose `s` differs from the size the
desktop itself gave the view, is discarded the same way, and the view is
answered as *again* -- as is one with no `s` where the kept grid is not
the view's size, the view having been bound again at another size, which
unguarded wrote past the kept grid in the desktop on an app's bytes
(measured under ASan before it was guarded) -- so the runtime has the app
send its next frame whole
(`console cells again`). `s` is checked, never trusted: every bound below
is computed from the size the desktop gave, not the one the app said. A
frame of a view the desktop holds is read into a buffer only while it is
inside its bound; one that passes it goes over to skipping, the same way.

At the desktop the guards are the socket's, in C: at most `CN_CWPASS`
(256 kB) read a pass with `MSG_DONTWAIT`, never a wait, the rest left in
the socket for the next pass; every pass drains, whether or not the view's
window is drawn, applying what it reads to the kept grids and marking what
changed, and the cells reach the screen when the view's owner draws it. A
run or a none past its view's edge is clipped and a frame whose runs and
nones pass `CN_CWRUN` (96) bytes for each cell of its view -- the most a
run of one cell can take, so a picture drawn in half blocks, a pen a cell,
is inside it -- is discarded and answered *again*. The connection is
dropped only for what no race can make: an unknown letter, a field that is
not a number, a line longer than `CN_CWLINE` (64 kB, its header and its
counted text together), a link or a translucency it never named, or live
URIs past `CN_CWLKB`. The app's console sends none of these -- it cuts a
run before `CN_CWLINE` and a cell at `CN_CWCELL` -- so each is a broken
app.

**Input is never held.** `dt_run` waits out a frame the output cap holds
with `console key -q`, which ignores every watched descriptor -- a
terminal's pty writing a hundred times a second must not wake it -- and so
an app's request, and the cells a key makes it draw, would each sit out the
hold (70 ms measured for an ask). So a watched descriptor has a class:
output, ignored by a quiet wait, which a pty and a cells channel are by
default; or input, which ends even a quiet wait as a key does and which
`console woke` names like any other -- `console watch -i FD`, which the
desktop gives each app's request socket. And the echo rule: when the
desktop tells an app something the person did (`console cells input
VIEW`), that app's cells channel is input until the next `e` it reads on
it, so the frame the key caused is drawn when it arrives, never held -- as
in-process, where the key's echo was drawn in the frame the key began.
None of this costs a wait anything while no descriptor is input: the quiet
wait asks a count before it looks at the list.

What the desktop's read answers, for each connection, is what it applied:
frames, cells and bytes, and the views it changed, uncovered or wants
again. The bytes and cells are what the desktop's budget (`dt_spent`)
charges the app with, so an app flooding its channel is a `held:` line
like any slow callback, its time spent in the desktop being the C that
read it.

Sizes. A run's header, its newline included, is 19 bytes at its smallest
(every field one digit) and 54 at its largest; a window's usual, an RGB pen
at a two-digit row and column, is 34. A cell is its glyph in UTF-8 -- 1 byte
for ASCII, 3 for a box-drawing or Blit glyph in the BMP, 4 for one in plane
16 -- and its combining marks; a wide glyph's right half is 0. A none run is
8 to 14 bytes, `w` about 8, `e` 2, `s` 8. So a whole 24x80 window of ASCII
in 48 runs is about 3.6 kB, and one cell changed is 45 bytes, `w` and `e`
included. Kept, a cell is a `cn_cell`, 40 bytes, at the desktop and twice at
the app (its grid and what it last sent); none is a code point no glyph has,
so nothing grows for it.

Built and measured (2026-11-20, callgrind, thousands of instructions a
frame, from 40 frames less 20 so start-up falls out; each side alone, the
other running plain; the desktop's figure is its whole frame -- the face,
the drawing or the records, and the flush, which is 419 of every row and
966 of a 24x80 window changed whole):

| | in-process today | records, desktop | of which read and drawn | records, app |
|---|---|---|---|---|
| Clock 7x26, whole | 719 | 602 | 40 | 287 |
| Clock, one cell | 702 | 492 | 9 | 236 |
| Calculator 16x24, whole | 2220 | 537 | 13 | 1828 |
| Calculator, one cell | 2220 | 526 | 11 | 1828 |
| 24x80, every cell | 3935 | 2343 | 1277 | 3060 |
| 24x80, one cell | 3386 | 522 | 11 | 2647 |

The desktop pays less than in-process on every row, and the window's own
script is gone from it, which is the point. A repaint from the kept grid
(`console cells show -a`, all 1716 cells of the 24x80 window) is 732,
against 1.05 M through `dp->put`. The write the records land through is the
one every in-process write uses (`CN_PUTG`), and sharing it made in-process
cheaper too: an 80x24 window drawn whole, 3,530 a frame against 3,660 at
0.99.553. `tests/cellwire.py` holds the records to in-process cell for
cell.

## The spine (2026-11-21, protocol 7)

The first path an app's own process draws a window through, end to end:
started, bound, drawn, given input, closed. Protocol 7, once, for all of it.

- **A second connection, the cells channel.** The desktop's `ok` after a
  hello carries a one-time token, good only on that request connection and
  void when it drops. An app whose file defines `_draw` opens a second
  connection to the same socket with `cells TOKEN`; the desktop answers `ok`
  and from then only reads it -- cell records (`console cells`), never a
  request. Either connection ending ends both.
- **`has PREFIX KIND...`** -- what the process's file registers, sent once
  the cells channel is up; a window of that app made from then on is hosted.
- **A hosted window is never drawn in-process.** `DT_RVC[id]` is set when
  it is made (`wm/seam.hibr`); `dt_win` binds its view (`view NAME GEN H W
  win ID DH DW OR OC`, the body's size and origin worked out by the desktop
  as `dt_win` does, so the app never re-derives them) and shows its kept
  cells where `_draw` would have run (`wm/rtview.hibr`). Until it binds, its
  face and frame are drawn and nothing else. It is dirty only when its cells
  changed or it was bound again (`DT_RVDIRTY`), never asked (`_dirty`).
- **One verb runs an app's code: `call SEQ ID KIND WORDS...`** -- SEQ 0 a
  tell, above 0 an ask answered `took SEQ STATUS T VALUE WHY`. Desktop to
  app `tell` is retired into it. `again NAME` repeats a view whose `take`
  failed, `forget NAME` ends one, `revoke SEQ` withdraws an ask whose answer
  came too late (the input track's).
- **Close is an ask that holds the window**, never a wait: the window goes
  when the app's `took` says so, or its process ends.
- **Only a test session hosts** (`dt_rthost`, through `dt_rtlisten`), so in
  a real session each new branch is one empty-map lookup: no app is moved
  out by this, and no switch chooses between two paths. `tests/hosted.py`
  drives Clock, a closer, a hanger and the calculator through it.

The later tracks -- a Control Panel pane, a strip module, a bar item, a
saver, menus and the words a list shows -- need no new verb: each is a
`view` of another KIND (`pane`, `strip`, `bar`, `saver`) or a `call` of
another callback, so they add code and not protocol.

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
