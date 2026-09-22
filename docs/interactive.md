# Using it every day

What the shell is like to sit in front of. Everything below was run before it
was written down; the terminal transcripts are what actually came back.

- [Starting up](#starting-up) · [Prompts](#prompts) · [Line editing](#line-editing)
- [History](#history) · [Completion](#completion)
- [Getting around](#getting-around) · [Jobs](#jobs) · [Reading input](#reading-input)

## Starting up

`~/.hibrc` is read by interactive shells only — a script pays nothing for it.
`$HIBR_RC` names a different file, which is how the test harness starts a shell
with a prompt of its own.

```sh
mod load sys
mod load prompt
PS1='\u@\h \w\$ '
alias ll='ls -lh'
export EDITOR=vim
```

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

```sh
PS1='[\u|\h|\w|\s|\v|\$]'
```

```
[osakka|claude-code|~/hibr|hibr|0.21|$]
```

Colour goes inside `\[ \]` so the editor does not count it as width, and the
cursor lands where it should on a line that wraps.

### The right-hand prompt

`RPS1` is drawn against the right edge of the first row. It appears only when
the line leaves room for it, so a long command simply takes the space back, and
it never moves the cursor.

```sh
RPS1='[\t]'                          # the time, on the right
RPS1='$(git branch --show-current)'
```

### The transient prompt

When a line is accepted it is redrawn with `TPS1` before the command runs, so
scrollback keeps the commands and not the decoration. The right-hand prompt is
dropped from that line too.

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
| `^U` | kill the whole line | `^W` | kill the word before the cursor |
| `^L` | redraw | `^R` | search history backwards |
| `^D` | delete forward, or end the shell on an empty line | `^C` | abandon the line |
| `Tab` | complete | `^Z` | stop the foreground job |
| arrows, `Home`, `End`, `Delete` | as expected | | |

## History

Saved to `$HIBR_HISTFILE`, or `~/.hibr_history`. `^R` searches it incrementally.

`history` prints it, `history -c` clears it, `history -s text` adds an entry
without running it, and `history -p` shows what an expansion would become:

```sh
set -H
history -s "echo first"
history -s "echo second"
history -p '!!'        # echo second
history -p '!-2'       # echo first
history -p '!echo'     # echo second
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

```
> unal<Tab>                → unalias
> echo $HIBR_VERS<Tab>     → echo $HIBR_VERSION
> ls some/dir/uniq<Tab>    → ls some/dir/unique-name.txt
```

A single candidate is completed and a space added; several are listed and the
line left alone.

**[hibr]** It is programmable. `COMPLETE[cmd]` names a function, which is
called with the command and the partial word, and whatever it `ret`s becomes
the candidates:

```sh
fn gitc(str cmd, str word) { ret commit checkout cherry-pick; }
COMPLETE[git]=gitc
```

```
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
pushd /tmp; pushd /usr; dirs      # /usr /tmp ~/hibr
popd;                    dirs     # /tmp ~/hibr
echo ~1                           # the next entry down
echo ~+ ~-                        # $PWD and $OLDPWD
```

## Jobs

Every interactive foreground job gets its own process group and the terminal.
Scripts skip all of it and pay nothing.

```sh
sleep 30 &
sleep 60 &
jobs
```

```
[1]-  Running                sleep 30
[2]+  Running                sleep 60
```

`^Z` stops the foreground job, `fg` and `bg` resume one, `kill` takes a signal
name or a `%job`, `disown` forgets one without signalling it, and `wait` waits —
for a job, a pid, or with `-n` for whichever finishes first, naming it with
`-p`.

## Reading input

`read` takes `-r` (no backslash escapes), `-p` (prompt), `-s` (no echo), `-n`
(that many characters), `-d` (a different delimiter), `-t` (seconds), `-u` (a
descriptor) and `-a` (into an array). Attached values work too, so `-n3` is
`-n 3`.

```sh
printf abc | { read -r -n2 x; echo "$x"; }     # ab
printf a:b | { read -r -d: y; echo "$y"; }     # a
read -r -t 1 z < /dev/null; echo "$?"          # 1, it timed out
```

`select` builds a menu and loops until something breaks it. A long list is laid
out in columns, as many as the terminal takes, filled down each column in turn —
the same shape bash chooses, padded with spaces rather than tabs so the columns
line up whatever the terminal's tab stops are:

```sh
select x in alpha beta; do echo "picked $x"; break; done
```

```
1) alpha
2) beta
#? 2
picked beta
```

---

[← documentation index](README.md) · [← project README](../README.md)
