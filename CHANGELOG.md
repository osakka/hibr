# Changelog

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
11/11, tests/term_diff.py 62/62 -- each count now the checks actually made.
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
