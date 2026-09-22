# Builtins

Every builtin the shell carries, seventy of them. `help` lists the same set at
runtime, and `type name` says what a name resolves to.

Builtins marked **[hibr]** have no bash equivalent, or differ from it on
purpose. Everything else behaves as bash does unless the entry says otherwise.

- [Running things](#running-things) · [Variables](#variables) ·
  [Text and data](#text-and-data) · [Conditions and flow](#conditions-and-flow)
- [Jobs and processes](#jobs-and-processes) · [Files and directories](#files-and-directories)
- [Networking](#networking) · [Command-line arguments](#command-line-arguments)
- [Modules](#modules) · [Interactive](#interactive)

## Running things

| builtin | synopsis |
|---|---|
| `:` | succeed, doing nothing |
| `true` / `false` | succeed / fail |
| `exit [n]` | leave the shell with status `n`, or the last status |
| `eval word…` | join the words and run the result as a command |
| `exec [cmd]` | replace the shell with `cmd`; with none, apply the redirections permanently |
| `source f [args…]` / `. f [args…]` | run `f` in this shell; extra arguments become its positional parameters |
| `command [-p] name args…` | run `name` ignoring functions and aliases |
| `command -v name` | print how `name` would resolve; status 1 if it would not |
| `command -V name` | the same, in a sentence |
| `builtin name args…` | run the builtin `name`, ignoring a function of that name |
| `type name…` | say what each name resolves to |
| `help` | list every builtin with a one-line description |
| `time cmd` | run `cmd` and report how long it took |
| `let expr…` | evaluate arithmetic; the status is 0 when the last value is non-zero |
| `title name…` | **[hibr]** rename the running process as `ps` shows it |

`source` restores the caller's positional parameters afterwards. With no extra
arguments the sourced file inherits them, and a `shift` inside leaks, as in bash.

## Variables

| builtin | synopsis |
|---|---|
| `declare [-aAgilnprux] [type] [name[=v]…]` | declare variables and attributes |
| `typeset …` | the same builtin under its other name |
| `readonly [-p] [name[=v]…]` | make variables readonly, or list the ones that are |
| `local name[=v]…` | declare function-local variables |
| `export [-p] [name[=v]…]` | mark variables for export; `-p` lists them |
| `unset [-f] name…` | remove variables, array elements or functions |
| | `unset a[1]`, `a[-1]` and `h["a-b"]` all reach what they name |
| `set [-/+flags] [--] [args…]` | set options, or replace the positional parameters |
| `shopt [-s\|-u\|-q] [name…]` | read or set shell options |
| `shift [n]` | drop the first `n` positional parameters |
| `read [-rs] [-p s] [-n k] [-d c] [-t s] [-u fd] [-a arr] [name…]` | read one line; a name may be subscripted, as in `read h[k]` |
| `mapfile` / `readarray [-t] [-n k] [-s k] [-O k] [-d c] [-u fd] [arr]` | read lines into an array |
| `getopts optstring name [args…]` | parse option letters, one call at a time |

`declare` takes bash's flags — `-i` integer, `-l` and `-u` lower and upper
cased on assignment, `-r` readonly, `-x` export, `-a` and `-A` map (they are
the same thing here), `-n` nameref, `-p` print, `-g` global — and **[hibr]** accepts one of the shell's own type names in their place:

```sh
declare -i n;   n=abc    # bash's: coerces, n becomes 0
declare int n;  n=abc    # ours: refuses, and stops
declare num f=1.5        # also path, str, arr, map, any
```

A flagged `-i` coerces; a declared type *validates*, with the same check that
guards typed function parameters. Inside a function `declare` is local unless
`-g` is given.

**[hibr]** `shopt` and `set -o` are **one namespace**: `set -o nullglob` and
`shopt -s errexit` both work, where bash rejects each. `shopt` alone lists
everything. Options that cannot move — `extglob`, `globstar`,
`expand_aliases` always on, `pipefail` always off — say so rather than
appearing to succeed. See [0017](adr/0017-one-namespace-for-options.md).

## Text and data

All four work in the shell's own process. No pipeline, no fork.

| builtin | synopsis |
|---|---|
| `str op args…` | **[hibr]** `len upper lower trim slice index replace split join pad starts ends contains repeat` |
| `arr op var args…` | **[hibr]** `len push pop sort uniq reverse contains map filter` |
| `json op var args…` | **[hibr]** `parse get set emit keys len type` |
| `match [-i] [-a] subject pattern [var]` | **[hibr]** match a regex, groups land in `M` |
| `rsub [-i] [-g] subject pattern replacement [var]` | **[hibr]** substitute regex matches |
| `printf fmt [args…]` | formatted output, including `%q` and `%(fmt)T` |
| `echo [-n] [-e] args…` | write arguments |

```sh
str upper hello                      # HELLO
json parse cfg "$body"               # a document becomes a nested map
echo "${cfg[items][0][name]}"        # read it with ordinary subscripts
match "$line" '^([a-z]+)=([0-9]+)$'  # ${M[1]} and ${M[2]}
```

Every operation of each, with examples, is in
[Text, regex and JSON](data.md); whole tasks built out of them are in the
[cookbook](cookbook.md).

## Conditions and flow

| builtin | synopsis |
|---|---|
| `test expr` / `[ expr ]` | evaluate a conditional expression |
| `break [n]` | leave `n` enclosing loops |
| `continue [n]` | restart the `n`th enclosing loop |
| `return [n]` | return from a function with status `n` |
| `ret [value…]` | **[hibr]** produce a value and return |
| `try cmd args…` | **[hibr]** run `cmd`, catching failure instead of propagating it |
| `fail msg…` | **[hibr]** report a failure with a message |

**[hibr]** `ret` does not print. It fills the result slot, which `:=` binds:

```sh
fn add(int a, int b) -> int { ret $((a + b)); }
sum := add 2 3          # no subshell, no fork
echo "$sum"             # 5
```

`try` catches a failure and leaves the details in `$ERR`, `$ERRMSG` and
`$ERRSTATUS`. See [0005](adr/0005-results-travel-in-a-slot.md) and
[The language](language.md).

## Jobs and processes

| builtin | synopsis |
|---|---|
| `jobs` | list active jobs |
| `fg [%job]` / `bg [%job]` | resume a job in the foreground or background |
| `wait [%job\|pid]` | wait for jobs to finish |
| `wait -n [-p var]` | wait for the next one, and name which it was |
| `kill [-sig] %job\|pid` | signal a job or process |
| `disown [%job]` | forget a job without signalling it |
| `coproc [name] cmd args…` | **[hibr]** run a command as a coprocess |
| `trap [cmd] sig…` | run `cmd` on a signal, on `EXIT`, `ERR`, `DEBUG` or `RETURN` |
| `ulimit [-HSa] [-cdfilnstuv] [limit]` | read or set a resource limit |
| `umask [mask]` | show or set the file creation mask |
| `hash [-r] [-d name] [name…]` | show or forget where commands were found |

**[hibr]** A coprocess is an endpoint, spoken to with the same verbs as a
socket:

```sh
worker() { while recv 0 line; do send 1 "got:$line"; done; }
coproc cp worker
send ${cp[out]} hello
recv ${cp[in]} answer          # ${cp[0]} and ${cp[1]} read the same
```

As many as you like, at once. See
[0018](adr/0018-a-coprocess-is-an-endpoint.md).

`trap … DEBUG` runs before each command, with the command text in `$CMD`.
`trap … RETURN` runs when the function that set it returns, and is forgotten
afterwards, so it does not leak into the next call — set it inside the function
you mean, as in bash.
`hash` is a real cache: `findx` consults it before walking `PATH`, it is
forgotten when `PATH` changes, and a remembered path that stops working is
looked up again.

## Files and directories

| builtin | synopsis |
|---|---|
| `cd [dir]` | change directory; `cd -` returns to the previous one |
| `pwd` | print the working directory |
| `pushd dir` / `popd` | push a directory and change to it / pop the stack |
| `dirs` | show the directory stack |

The stack is reachable from a word as well: `~0` is the working directory, `~1`
the next entry, `~-1` the one before last, and `~+` and `~-` are `$PWD` and
`$OLDPWD`.

## Networking

| builtin | synopsis |
|---|---|
| `connect [-u\|-s] host port [var]` | **[hibr]** open a client connection; `-u` UDP, `-s` TLS |
| `listen [-f] [-n count] port handler` | **[hibr]** serve connections with a handler |
| `listen -b port [var]` | **[hibr]** bind only, and hand back the descriptor |
| `accept listenfd [var]` | **[hibr]** wait for one connection |
| `send [-n\|-r] fd text…` | **[hibr]** write to a descriptor; `-r` ends CRLF |
| `recv [-a\|-n bytes] fd [var]` | **[hibr]** read one line, all of it, or `n` bytes |

Descriptors come back above 9, so a redirection cannot tread on one. `listen -b`
is what lets a daemon bind a privileged port as root and then stop being root:

```sh
mod load sys
listen -b 80 LFD
drop www-data
while accept $LFD C; do serve <&$C >&$C; exec {C}<&-; done
```

See [Networking](networking.md) and
[0016](adr/0016-privileges-are-dropped-never-gained.md).

## Command-line arguments

| builtin | synopsis |
|---|---|
| `opt -s --long name [type[!+][=default]] [help…]` | **[hibr]** declare one option |
| `args "$@"` | **[hibr]** parse the arguments against what was declared |

Options are declared, not hand-parsed. `!` marks one required, `+` repeatable,
`=v` gives a default, and the type is checked:

```sh
opt .  "Summarise a source tree"
opt -d --dir  dir  path=.  "Directory to inspect"
opt -n --top  top  int=3   "How many to show"
args "$@"
```

`--help` is generated. **[hibr]** `args` ends the script only at the top level;
inside a function or a `try` it returns 2 —
[0011](adr/0011-args-exits-only-at-top-level.md).

## Modules

| builtin | synopsis |
|---|---|
| `mod load path\|name` | load a module |
| `mod drop name` | unload one |
| `mod list` | list what is loaded, with its ABI and builtins |
| `mod avail`, `mod list -a` | list every module that could be loaded, and its state |
| `need name…` | make an interface or module available, or fail saying which |
| `app name [text]` | name this script as an app, for whatever is running it |
| `cat [-benstuvAETfp] [file…]` | **[module]** `cat` in a pipe; gutter and colour on a terminal |

A module adds builtins, and it can add a *protocol*: register a scheme and
`/dev/<name>/…` works anywhere a filename does. When the effective uid is 0 the
search skips `.` and `HIBR_MODPATH` and consults only the module directory. See
[Modules](modules.md).

## Interactive

| builtin | synopsis |
|---|---|
| `history [-c] [-s text]` | print the command history, clear it, or add to it |
| `alias [name[=value]…]` | define or list aliases |
| `unalias [-a] name…` | remove aliases |

Aliases expand in scripts as well as interactively, with no option to set —
bash needs `shopt -s expand_aliases`. `alias` re-quotes an argument by escaping
it rather than wrapping it, so a subscript passed through an alias is still
evaluated.

---

[← documentation index](README.md)
