# 0029 — mkdir, rm and mv are builtins

Status: accepted

## Context

Blit, a terminal server that draws hibr's desktop straight to the screens,
runs it in a guest whose `/usr/bin` holds busybox and nothing linked to
it. The desktop's control socket never appeared there (Gitea #98): its
private folder was made with `mkdir -p -m 700`, and there was no `mkdir`.
The folder for the desktop's log was made the same way, so the log that
would have said so was never opened either. The hold module, which makes
its folder in C, worked, which is what made the socket look like the
problem.

The project's rule is that the core grows only for what makes it a better
shell, and that something user-facing wanting to be in the core is a sign
it should be a module. A module does not help here: root, which Blit runs
as, loads modules only from the compiled-in folder (ADR 0016), and a
guest without coreutils has no such folder either.

## Decision

`mkdir` and `rm` are builtins, in `src/fs.c`. They shadow `/bin/mkdir` and
`/bin/rm` for every script, so they answer as GNU's do for what they
implement -- the same output, statuses and messages, checked against them
by `tests/986-mkdir-rm.t` -- and hand everything else to the program on
`PATH`:

- `mkdir`: `-p`, `-m` with an octal mode, `-v`, `--parents`, `--mode=`,
  `--verbose`, `--`;
- `rm`: `-f`, `-r`/`-R`, `-d`, `-v`, their long forms, `--`. A link is
  removed, never entered; `.`, `..` and `/` are refused as GNU refuses
  them.
- `mv` (0.99.44, Gitea #99): `-f`, `-n`, `-v`, `-t dir`, `-T`, their long
  forms, `--`; a rename, or a move into a directory. A move across
  filesystems is a copy, and goes to the program, one source at a time.
  `tests/987-mv.t` checks it against GNU's.

A symbolic mode, `-i`, any other option, and any case where GNU's `rm`
would stop to ask (a file it cannot write, with a terminal on standard
input) runs the program, so a script asking for more gets all of it. If
there is no program to run, an option that needs one fails and says so;
a prompt that cannot be asked is not, and the removal goes ahead as GNU's
would with no terminal. Under `--plan` both are refused and recorded, as
the programs were.

## Consequences

- A desktop, or any script, can make and clear folders where coreutils is
  missing, and does so without a fork: 87 places in the desktop alone.
- `type rm` says builtin; `command -v rm` prints `rm`, not a path.
  `command rm` reaches the builtin, `/bin/rm` the program.
- The core is about 560 lines larger. The desktop's snapshot and settings,
  written beside and renamed into place, work with no coreutils. What it
  still forks -- `sort` with keys, `cp -R`, `touch -r`, `ps`, `readlink`,
  `uname` -- is a system tool doing a system tool's job, and a guest that
  wants all of it can link busybox's applets (`busybox --install -s`).
