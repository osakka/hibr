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

```
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
