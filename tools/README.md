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
terminal size and timing.

Regenerate a doc's screenshots whenever the thing they show changes; a stale
screenshot is worse than none, since it reads as current when it is not.

The tour in `docs/desktop.md` shows the desktop as the package installs
it, which has no applications, so its pictures cannot read the tree's
`apps/` the way the others do. `docshots.py` serves a copy of the real
catalogue (`~/hibr-apps`, or `CATALOGUE_DIR`) with the suites' own
`tests/davserve.py`, installs Calculator and Files from it with the real
`catalogue` command into a data folder of its own, and hands that folder
to the shots that come after the install with `--env`. Nothing is fetched
from anywhere and nothing published changes. Every shot is given a home
folder that is nobody's, `/tmp/visitor`, made fresh and removed after, so
no picture of Files shows the home of whoever ran it. A glyph DejaVu Sans
Mono lacks is drawn from Noto Sans Symbols, as a terminal would fall back,
where it used to be drawn as the missing-glyph box.

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

## desktop-launcher.in

The template for the installed `desktop` command: `make install` fills in
where the desktop went and writes it to `$(PREFIX)/bin/desktop`.


## unigen.py

Writes `mods/uni/tab.c` -- bidi classes, mirrored and bracket pairs, joining
types, Arabic presentation forms and display widths -- from one version of
the Unicode Character Database. `tools/unigen.py fetch 15.1.0 build/ucd`
downloads what it reads; `python3 tools/unigen.py gen build/ucd >
mods/uni/tab.c` writes the tables, naming the version. Replace the gzipped
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
