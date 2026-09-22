# mods/cat

`cat`, with one rule above all the others: **in a pipe it is `cat`, byte for
byte.** Everything it adds is conditional on standard output being a terminal.

A `cat` that is clever in a pipe is a broken `cat`. Scripts pipe it, `diff` it,
and feed it binaries, and all of that has to keep working exactly as it always
has — including `cat -n` and `cat -A`, which produce GNU's bytes and not a
prettier version of them.

| file | role |
|---|---|
| `ct.h` | the options and the entry points |
| `cat.c` | argument handling, the raw copy, the line-at-a-time copy |
| `hl.c` | terminal rendering: the gutter, visible control bytes, colour |

## The two paths

`ct_plainish` decides. When nothing about the run needs the content looked at —
no `-n`, no `-A`, not a terminal — `ct_raw` copies descriptor to descriptor in
64 kB blocks and never examines a byte. A hundred megabytes takes 15 ms against
`/bin/cat`'s 14.

Anything else goes through `ct_cook`, which buffers to line boundaries and
hands each line to `ct_line`. That path costs more, and only runs when
something asked for it.

## On a terminal

A dim gutter with line numbers, control bytes shown as `^A` rather than sent,
and lexical colour: comments, strings, numbers and a keyword list, for C,
shell, Python, JSON, Markdown and Makefiles. Binary files are refused with a
line saying how large they are rather than spewed; `-f` overrides that.

Block comments and triple-quoted strings do carry across lines -- `/* … */`,
`"""…"""`, and Markdown fences -- because `ct_opt` holds the state between
lines. Tabs are expanded to the stop the *file* means rather than the one the
terminal would pick, since the gutter has already moved the terminal's columns
along. A byte that is not valid UTF-8 is shown as `<ff>` instead of being sent
for the terminal to turn into a replacement character.

**Lexical, not syntactic.** It does not parse. A `#` inside a string in a
language whose comments start with `#` will still end the line, and a `/*`
inside a string opens a comment. That is the trade for one pass with no forks,
and it is stated in the guide rather than hidden.

`-p` turns every addition off, for when the output is a terminal but you want
the bytes.

## What it deliberately does not do

**No paging.** A `cat` that pages is half a `most`, and building a pager twice
is exactly what the screen layer exists to prevent. When the `most` module
exists, this can hand off to it.

**No git gutter**, although the backlog promised one. hibr does read git's
object store natively — but that code is inside `prompt.so`, and modules are
opened `RTLD_LOCAL`, so `cat.so` cannot see it. Getting there means one of:
moving several thousand lines of git into the shell, opening modules
`RTLD_GLOBAL` and accepting collisions between every pair of modules, or adding
a module-to-module export mechanism to the ABI. The third is the right answer
and it is a design decision, not something to improvise inside a `cat`. See
`docs/backlog.md`.

## Testing

`tests/650-cat.t` is the contract: 37 comparisons against `/bin/cat` itself,
covering the flags, the error cases, the dash forms and a binary file. It is
recorded rather than compared against bash, because bash's `cat` *is*
`/bin/cat` and the point is the bytes.

`tests/cat.py` is the other half, through a pseudo terminal, because everything
this module adds is invisible to `run.sh` by design.

    python3 tests/cat.py
