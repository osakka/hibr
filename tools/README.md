# tools

## screenshot.py

Renders a real hibr session to a PNG — not a mockup, a pixel-for-pixel
reconstruction of what the terminal actually drew. It runs hibr on a real
pty, the same way `tests/screen.py` does for the test suite, and replays the
exact escape sequences the console module sent: text, position, foreground,
background, bold, dim. Every screenshot this produces is provably a real
render of real output, which is the point — a picture in the docs cannot
drift from what hibr actually does once it is made this way, and cannot show
something hibr does not actually do.

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

