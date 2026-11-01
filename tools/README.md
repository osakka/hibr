# tools

## screenshot.py

Renders a real hibr session to a PNG — not a mockup, a pixel-for-pixel
reconstruction of what the terminal actually drew. It runs hibr on a real
pty, the same way `tests/screen.py` does for the test suite, and replays the
exact escape sequences the console module sent: text, position, foreground,
background, bold, dim, **and the pictures**. Every screenshot this produces
is provably a real render of real output, which is the point — a picture in
the docs cannot drift from what hibr actually does once it is made this way,
and cannot show something hibr does not actually do.

A bitmap is drawn from the same evidence as the text: the kitty
transmission is kept whole, base64-decoded, inflated when the control says
`o=z`, scaled to the cells its own placement names, and pasted under the
text, where a cell with no background colour of its own lets it through.
`--cell 9x18` is what makes pixels possible at all — it tells the pty what a
cell measures, which is what a terminal that can place pixels reports — and
without it hibr draws half blocks, exactly as on a terminal that cannot.

```text
tools/screenshot.py docs/img/wall.png 'DT_WALLIMG=/path/to.png; DT_IMGMODE=pixels' --cell 9x18
```

Until 0.99.148 it could not show a picture, and did something worse than
nothing: its escape skip advanced two bytes whatever the escape was, so an
APC's base64 was fed to the grid as text and the harness's own idle marker
(`ESC ] 7777;idle;N;M BEL`) was written across whatever window the cursor
was in — shifting every cell written after it in that run. `tests/shots.py`
is the guard. The images under `programs/desktop/img/` were made before it
and stayed shipped for sixty-nine releases: `desktop-strip.png` had lost
**File**, **View** and **Special** from its menu bar, the workspace
switcher and the notification flag — a visitor's first picture of the
desktop showed a menu bar with three of its five menus missing.

They were never regenerated because **no recipe for them existed
anywhere**: each was made by hand, once, so when the tool turned out to be
corrupting its own output there was nothing to re-run. `tools/docshots.py`
is that recipe now — one place that makes every picture the documentation
shows, next to the tool that renders them, so a corrupted or stale image
is one command away from being correct:

```text
python3 tools/docshots.py                 # all of them
python3 tools/docshots.py desktop-strip   # one
```

Writing it found two more faults in this tool, both of which made a
picture quietly wrong rather than failing. `--apps` resolved
`programs/desktop/apps/<name>.hibr` and nothing else, where the desktop
globs `**`, so an application in a submenu — the calculator, in
`Accessories/` — sourced nothing and its window photographed as an empty
box with a correct title. And `--wait` is a *settle timeout*, not a
dwell: it returns the moment the desktop first goes idle, which is well
before a program inside a window has started, so a terminal rendered
blank however large `--wait` was made. `--until TEXT` waits for the
thing, which is the rule the suites already follow.

```text
tools/screenshot.py docs/img/hello.png 'dt_new "Hello" 8 30 6 10'
tools/screenshot.py docs/img/calc.png 'dt_new "Calc" 16 24 2 2 calc' --apps calc
tools/screenshot.py docs/img/menu.png 'dt_new "Hello" 8 30 6 10' --keys f10
```

Put the inline session before any `--flag`, not after — see the script's
own `--help` for why.

Run `tools/screenshot.py --help` for the rest — `--session-file` to run a
session file directly instead of an inline snippet, `--keys` to send input
before the screenshot is taken, `--rows`/`--cols`/`--wait`/`--tick` for the
terminal size and timing, `--until TEXT` with `--untilmax` to bound the
wait, `--apps` and `--modules` for what the session loads, `--env` for
variables it starts with, and `--cell` for pixels.

Regenerate a doc's screenshots whenever the thing they show changes; a stale
screenshot is worse than none, since it reads as current when it is not.

The tour in `docs/desktop.md` shows the desktop as the package installs
it -- the seven applications `packaging/ships` names and nothing else --
so its pictures cannot read the tree's `apps/` the way the others do.
`docshots.py` builds that desktop from `tools/ships.hibr`'s answer, serves
a copy of the real catalogue (`~/hibr-apps`, or `CATALOGUE_DIR`) with the
suites' own `tests/davserve.py`, installs Mail from it with the real
`catalogue` command into a data folder of its own, and hands that folder
to the shots that come after the install with `--env`. Nothing is fetched
from anywhere and nothing published changes. Every shot is given a home
folder that is nobody's, `/tmp/visitor`, made fresh and removed after, so
no picture of Files shows the home of whoever ran it. A glyph DejaVu Sans
Mono lacks is drawn from Noto Sans Symbols, as a terminal would fall back,
where it used to be drawn as the missing-glyph box.

## measure.py

The README's Measurements table, measured: hibr against dash and bash,
printed as Markdown with the version, the host's CPU and the load average
it ran under, so a reading carries what it needs to be judged. The table
had been re-measured by hand three times, a different subset of rows each
time; a script takes every row the same way every time.

```text
tools/measure.py [path-to-hibr]        # build/hibr by default
```

The binary is the stripped size of a copy, the file itself left alone.
Resident memory is the shell's own `VmHWM`, read from `/proc/self/status`
with the shell's own `read` so no fork is counted, and is the median of 25
runs, since one reading spreads by about 180 kB. Times are the best of 11,
each process pinned to one core. The loop's instruction count comes from
callgrind, because wall time between two builds moves several percent on
code layout alone.

## mdanchors.py

Every `](page.md#anchor)` in a tracked page names a heading that page
has. `tests/530-docs.t` already checked the page exists; this checks the
part after the `#`, and 530 runs it.

```text
python3 tools/mdanchors.py
```

## llm-measure/

What `docs/llm.md` is worth to a model writing hibr. Thirteen tasks in three
groups -- `plain` shell work, `trap` (a bash idiom that means something else
here) and `feature` (something only hibr has) -- each with fixed arguments,
input and expected output.

```text
tools/llm-measure/measure.py prompt > bare.txt          # the tasks, no page
tools/llm-measure/measure.py prompt --page > page.txt   # the tasks and docs/llm.md
tools/llm-measure/measure.py score -v reply.txt ...     # run each reply's scripts
```

Hand a model each prompt, keep its reply, and score them. A reply is scripts
between `=== ID` lines ending in `=== END`. Scoring runs each script under
`build/hibr` in a scratch directory with its own `HOME`, five seconds at
most, and refuses -- counts as failed, does not run -- any script that names
`sudo`, `rm`, `curl`, a write outside that directory, or anything else that
could touch the machine. `runs/` keeps what the models actually wrote, with
the date and model in each name, so a later run is compared against the
scripts and not only against a number.

A dozen tasks on a couple of models is a signal about the page, not a
benchmark.

## next-version.sh

Suggests the next `HIBR_VER` and `DT_VER` from what changed since the last
release tag: a change to `HIBR_ABI`, or a commit carrying a `Breaking:`
trailer, means more than a patch. `make next-version` runs it; `release.sh`
still does the bump.

## homebrew-sync.sh

Keeps the published Homebrew tap's formula on the newest tag the public
mirror can serve: it fetches that tag's tarball, takes its checksum and pushes
the formula when it is behind. Run on a timer, not by hand.

## desktop-launcher.in, vw-launcher.in, catalogue-launcher.in

The templates for the three commands `make install` places in
`$(PREFIX)/bin`: `desktop`, `vw` and `catalogue`. Each is a short hibr
script that runs the installed copy, with the place it went (`@DESKDIR@`
or `@SHAREDIR@`) filled in by `sed` at install time. The catalogue's also
points `HIBR_APPS_KEYRING` at the installed signing key unless it is set
already (ADR 0044).

## strings.py

Every string the desktop shows a person, for translation (ADR 0032). It
reads `programs/desktop/**/*.hibr` the way the shell would -- quote state
carried across lines, commands split at newlines, `;`, `&&`, `||`, `|`
and braces -- and takes the text arguments of every call that draws
something a person reads.

```text
python3 tools/strings.py            # a census: counts per kind, per file
python3 tools/strings.py --list     # every string, file:line kind text
python3 tools/strings.py --keys     # the sorted catalogue keys
python3 tools/strings.py --check    # what tests/992-strings.t runs
python3 tools/strings.py --write    # lang/strings.txt and the two pseudo-languages
```

`--check` fails on a string drawn and not in `programs/desktop/lang/strings.txt`,
on one listed and no longer drawn, and on a string with an expansion in it,
which cannot be a catalogue key and so would never be translated.
`--write` brings `strings.txt` up to date and rewrites `xx.json` and
`xy.json`, the pseudo-translations that mark every string, left to right
and right to left.

## htmlents.py

Writes `mods/html/ent.c` from WHATWG's `entities.json`: each name once,
without `&` and `;`, sorted for a binary search, with whether it is also
recognised without its semicolon.

```text
curl -sO https://html.spec.whatwg.org/entities.json
python3 tools/htmlents.py entities.json > mods/html/ent.c
```

## hcalgen.py

Writes `mods/hcal/calendars/umalqura.json` from ICU's own Umm al-Qura
table, which covers 1300-1600 AH: a 12-bit mask of month lengths a year,
and the Julian day of the table's first day. It needs PyICU
(`python3-icu`); run it only to regenerate, and check the result with
`tests/hcal_icu.py`.

```text
python3 tools/hcalgen.py
```

## mvoffsets.sh

Where each libav field the media module reads sits, per FFmpeg release --
the numbers in `mods/media/av.c`'s `mv_tabs` -- taken with `offsetof` from
that release's own headers, cloned from FFmpeg's repository, rather than
written by hand. Run it when FFmpeg makes a new major release and add a
line for it; the two zeros it prints are libswscale's and libswresample's
majors, filled in by hand.

```text
tools/mvoffsets.sh n8.0 [n9.0 ...]
```


## unigen.py

Writes `mods/uni/tab.c` -- bidi classes, mirrored and bracket pairs, joining
types, Arabic presentation forms and display widths -- and the core's own
width tables, `src/wtab.c`, from one version of
the Unicode Character Database. `tools/unigen.py fetch 15.1.0 build/ucd`
downloads what it reads; `python3 tools/unigen.py gen build/ucd >
mods/uni/tab.c` writes the tables, naming the version, and `python3
tools/unigen.py width build/ucd > src/wtab.c` the core's. Replace the gzipped
conformance suites in `tests/uni` from the same download: the tables and the
tests must never be from different versions (ADR 0030).

## ships.hibr

Which of the desktop's files the package installs ([ADR
0047](../docs/adr/0047-what-the-package-ships.md)). `packaging/ships` lists
the installables that ship, one path a line; this reads it with every
owner's `# part:` lines and a theme's `image`, so a part goes where its
owner goes, and prints the files that ship, or with `-n` the ones that do
not. `-c` checks the list and the parts: a name that is not there, a part
listed on its own, a shipped theme whose colours do not ship, and a pane
whose `cp_pane` says `app` that no application brings.

```text
hibr tools/ships.hibr          # what make install places
hibr tools/ships.hibr -n       # what make install-extras places
hibr tools/ships.hibr -c       # the check tests/541-ships.t runs
```

It is the one place that decides. The Makefile runs it with the shell it
has just built, and `tests/scopes.py` and `docshots.py` build their
desktop "as shipped" from its answer rather than a rule of their own.
