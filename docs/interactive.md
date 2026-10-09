# Using it every day

What the shell is like to sit in front of. Every example below that shows its output is
run again on every build by `tests/531-doc-examples.t`; the ones that need a
person at a terminal say so, and their transcripts are what actually came
back.

- [Starting up](#starting-up) · [Prompts](#prompts) · [Line editing](#line-editing)
- [History](#history) · [Completion](#completion)
- [Getting around](#getting-around) · [Jobs](#jobs) · [When something fails](#when-something-fails)
- [Reading input](#reading-input)

## Starting up

`man hibr` lists every flag, environment variable and exit status.
`~/.hibrc` is read by interactive shells only — a script pays nothing for it.
`$HIBR_RC` names a different file, which is how the test harness starts a shell
with a prompt of its own.

A shell is interactive when standard input is a terminal, or when `-i` says
so: `hibr -i` prompts and reads `~/.hibrc` even on a pipe, and `hibr -i -c
'…'` runs the command as a shell somebody is talking to. Job control needs a
real terminal, so a `-i` shell on a pipe says it has none and carries on.
`$-` carries an `i` either way, which is how a script asks:

```sh
case $- in *i*) echo interactive ;; *) echo a script ;; esac
```

```output
a script
```

A **login shell** — `hibr -l`, `hibr --login`, or one whose `argv[0]` begins
with a dash, which is how `login`, `getty` and `sshd` invoke it — reads
`/etc/profile` first, then the first that exists of `~/.hibr_profile`,
`~/.hibr_login` and `~/.profile`, and `~/.hibr_logout` on the way out. It reads
those whether or not it is interactive, so `--login` works in a script; and an
interactive login shell reads `~/.hibrc` **as well**, after the profile, so an
alias written there works at a console and over ssh without `~/.profile` having
to source it by hand ([0039](adr/0039-an-interactive-login-shell-reads-both.md)).
`$HIBR_PROFILE` names a different system profile, the way `$HIBR_RC` names a
different rc file.

A command a module provides needs no `mod load` first: `$HIBR_MODULES` is
`after` unless set, which means a module is looked for only once no alias,
function, builtin or program on `PATH` has answered — so `sysinfo` works with
no startup file at all and nothing a module offers can shadow a real program.
`before` puts hibr's own `ls`, `cat` and `most` in front of `PATH`, `off` asks
nothing ([0040](adr/0040-a-module-may-answer-a-command.md)).

<!-- not run: a ~/.hibrc, read when an interactive shell starts -->
```sh
mod load sys
mod load prompt
PS1='\u@\h \w\$ '
alias ll='ls -lh'
export EDITOR=vim
```

A `command_not_found` function defined here — `deploy.sh` writes one into a
new install's starter `.hibrc` by default — is called with the missing
command and its arguments once ordinary lookup has already failed, the same
convention bash's `command_not_found_handle` uses:

```sh
command_not_found() {
	mod find "$1" > /dev/null 2>&1 && "$@" ||
		{ echo "hibr: $1: command not found" >&2; return 127; }
}
upper hello world
totally-bogus-command
```

```output
HELLO WORLD
hibr: totally-bogus-command: command not found
```

`upper` autoloaded the `sys` module that registers it — no `mod load`/`need`
line first — while a command nothing registers still fails cleanly. This is
interactive-only: `.hibrc` is never read by a script, so a script that wants
a module's builtin still needs its own explicit `need`. See [`mod
find`](builtins.md#modules).

## Prompts

| variable | is |
|---|---|
| `PS1` | the prompt |
| `PS2` | the continuation, when a line is unfinished |
| `RPS1` | **[hibr]** a right-hand prompt, against the right edge |
| `TPS1` | **[hibr]** a transient prompt, left behind after the line runs |

All four take the same escapes, and are then expanded like any other word, so
`$(…)` and `${…}` work in them.

| escape | gives | escape | gives |
|---|---|---|---|
| `\u` | user name | `\t` | time, `HH:MM:SS` |
| `\h` `\H` | host, short and full | `\T` `\@` | 12-hour time, and with am/pm |
| `\w` `\W` | working directory, full and basename | `\d` | the date |
| `\s` | the shell's name | `\n` | a newline |
| `\v` | its version | `\e` `\a` | escape and bell |
| `\j` | how many jobs | `\[` `\]` | around bytes that take no width |
| `\$` | `#` for root, `$` otherwise | | |

<!-- not run: a prompt is drawn only by an interactive shell -->
```sh
PS1='[\u|\h|\w|\s|\v|\$]'
```

```text
[osakka|claude-code|~/hibr|hibr|0.68|$]
```

Colour goes inside `\[ \]` so the editor does not count it as width, and the
cursor lands where it should on a line that wraps.

### The right-hand prompt

`RPS1` is drawn against the right edge of the first row. It appears only when
the line leaves room for it, so a long command simply takes the space back, and
it never moves the cursor.

<!-- not run: a prompt is drawn only by an interactive shell -->
```sh
RPS1='[\t]'                          # the time, on the right
RPS1='$(git branch --show-current)'
```

### The transient prompt

When a line is accepted it is redrawn with `TPS1` before the command runs, so
scrollback keeps the commands and not the decoration. The right-hand prompt is
dropped from that line too.

<!-- not run: a prompt is drawn only by an interactive shell -->
```sh
PS1='\u@\h \w\$ '     # what you type at
TPS1='\$ '            # what stays behind
```

Neither is on by default. Both are checked by `python3 tests/editor.py`, which
drives a real terminal — see [Testing](testing.md).

## Line editing

UTF-8 throughout: one backspace removes a letter together with its harakat,
wide CJK characters count as two columns, and a line that wraps stays correct
when the terminal is resized under it.

| key | does | key | does |
|---|---|---|---|
| `^A` `^E` | start, end of line | `^B` `^F` | back, forward one character |
| `^P` `^N` | previous, next history | `^K` | kill to end of line |
| `^U` | kill back to the start of the line | `^W` | kill the word before the cursor |
| `^L` | redraw | `^R` | search history backwards |
| `^D` | delete forward, or end the shell on an empty line | `^C` | abandon the line |
| `Tab` | complete | `^Z` | stop the foreground job |
| arrows, `Home`, `End`, `Delete` | as expected | | |

A line holding Arabic, Hebrew or another right-to-left script is drawn in
display order, with Arabic in its joined forms, one screen row at a time,
with the whole line as one paragraph. The buffer, the keys and the cursor stay
in logical order: left and right step through the text as it was typed, and
the cursor stands on the column its position landed in. The `uni` module does
this, loaded the first time such a line is drawn. For a terminal that reorders
right-to-left text itself, set `HIBR_BIDI=off`; it is also the desktop's
default for `console bidi`.

## History

Saved to `$HIBR_HISTFILE`, or `~/.hibr_history`. `^R` searches it incrementally.

`history` prints it, `history -c` clears it, `history -s text` adds an entry
without running it, and `history -p` shows what an expansion would become:

```sh
set -H
history -s "echo first"
history -s "echo second"
history -p '!!'
history -p '!-2'
history -p '!echo'
```

```output
echo second
echo first
echo second
```

Expansion — `!!`, `!$`, `!n`, `!-n`, `!prefix` — happens in interactive shells
under `set -H`, and `set +H` turns it off. A script never sees it.

## Completion

`Tab` offers what makes sense where the cursor is:

| position | candidates |
|---|---|
| command | builtins, functions and everything on `PATH` |
| after `$` | variable names |
| anywhere else | paths |

```text
> unal<Tab>                → unalias
> echo $HIBR_VERS<Tab>     → echo $HIBR_VERSION
> ls some/dir/uniq<Tab>    → ls some/dir/unique-name.txt
```

A single candidate is completed and a space added; several are listed and the
line left alone.

**[hibr]** It is programmable. `COMPLETE[cmd]` names a function, which is
called with the command and the partial word, and whatever it `ret`s becomes
the candidates:

<!-- not run: completion happens when Tab is pressed at the prompt -->
```sh
fn gitc(str cmd, str word) { ret commit checkout cherry-pick; }
COMPLETE[git]=gitc
```

```text
> git c<Tab>
checkout cherry-pick commit
> git c
```

Because `COMPLETE` is an ordinary map and the hook is an ordinary function,
there is no separate completion language to learn — it is the shell.

## Getting around

`cd` with no argument goes home and `cd -` goes back. `CDPATH` is searched for
a relative name, and `cd` prints where it landed when it used it, as bash does.

A directory stack, reachable from a word as well as from the builtins:

```sh
cd /
pushd /tmp; pushd /usr; dirs
popd;                    dirs
echo ~1                           # the next entry down
echo ~+ ~-                        # $PWD and $OLDPWD
```

```output
/tmp /
/usr /tmp /
/usr /tmp /
/tmp /
/tmp /
/
/tmp /usr
```

`pushd` and `popd` print the stack as they change it, as bash's do.

## Jobs

Every interactive foreground job gets its own process group and the terminal.
Scripts skip all of it and pay nothing.

```sh
sleep 30 &
sleep 60 &
jobs
kill %1 %2
```

```output
[1]-  Running                sleep 30
[2]+  Running                sleep 60
```

`^Z` stops the foreground job, `fg` and `bg` resume one, `kill` takes a signal
name or a `%job`, `disown` forgets one without signalling it, and `wait` waits —
for a job, a pid, or with `-n` for whichever finishes first, naming it with
`-p`.

## When something fails

At the prompt a failure costs at most the line it is on; the shell itself
goes on. Under `set -e` a failing command abandons the rest of the line
rather than ending the shell, and so does an assignment the variable refuses
-- a readonly variable, or a typed one given a value of the wrong kind --
which in a script ends the script, as in bash. `return` outside a function,
and `break` or `continue` outside a loop, say so and do nothing else:

```text
> readonly r=1; r=2; echo same-line
hibr: r: readonly variable
> return 3; echo after-return
hibr: return: can only `return' from a function or sourced script
after-return
```

## Reading input

`read` takes `-r` (no backslash escapes), `-p` (prompt), `-s` (no echo), `-n`
(that many characters), `-d` (a different delimiter), `-t` (seconds), `-u` (a
descriptor) and `-a` (into an array). Attached values work too, so `-n3` is
`-n 3`.

```sh
printf abc | { read -r -n2 x; echo "$x"; }
printf a:b | { read -r -d: y; echo "$y"; }
read -r -t 1 z < /dev/null; echo "$?"
```

```output
ab
a
1
```

The last is 1 because the read met the end of its input.

`select` builds a menu and loops until something breaks it. A long list is laid
out in columns, as many as the terminal takes, filled down each column in turn —
the same shape bash chooses, padded with spaces rather than tabs so the columns
line up whatever the terminal's tab stops are:

<!-- not run: the choice is typed at the terminal -->
```sh
select x in alpha beta; do echo "picked $x"; break; done
```

```text
1) alpha
2) beta
#? 2
picked beta
```

---

[← documentation index](README.md) · [← project README](../README.md)
