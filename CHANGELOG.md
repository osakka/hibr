# Changelog

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
