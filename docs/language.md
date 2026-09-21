# The language

## Everything you expect from a shell

Pipelines, `&&`, `||`, `!`, `;`, background `&`, subshells `( )`, groups `{ }`,
`if`/`elif`/`else`, `while`, `until`, `for`, `case` (with `|`, a leading `(`,
and the `;&` / `;;&` fallthroughs), functions with recursion, and `local`.

**Expansion.** Single, double and `$'…'` quoting (`\n \t \e \xHH \0NNN`).
`${x:-d} ${x:=d} ${x:?msg} ${x:+alt}`, `${#x}`, `${x#p} ${x##p} ${x%p} ${x%%p}`,
`${x/p/r} ${x//p/r}`, anchored `${x/#p/r}` and `${x/%p/r}`, `${x:off:len}` with
negative offsets, `${x^^} ${x,,} ${x^} ${x,}`, the transforms `${x@Q} ${x@E}
${x@U} ${x@L} ${x@u}`, indirection `${!ref}` (which keeps any modifier after
it, so `${!ref:-d}` works), the names starting with a prefix `${!pre*}` and
`${!pre@}`, and negative subscripts `${a[-1]}`, counted back from the highest
key so a sparse array answers the same as bash. Command substitution `$( )` and backquotes, arithmetic `$(( ))`, tilde,
field splitting on `IFS`, globbing with `*`, `?`, `[…]` and `**` across
directories, and brace expansion — `{a,b}`, `{1..9}`, `{01..12}`, `{a..e}`,
`{1..9..2}`, nested and multiplied.

**Declarations.** `declare` / `typeset` take bash's flags — `-i -r -x -a -A -n
-p -g` — and, instead of them, one of the shell's own type names: `declare int
n`, `declare num f`, `declare path p`. A flagged `-i` coerces the way bash does,
turning anything unreadable into `0`; a declared type *validates*, with the same
check that guards typed function parameters, and a bad value fails loudly and
stops. `readonly` and `export -p` round out the set.

**Options.** One namespace, reached either way: `set -o nullglob` and
`shopt -s errexit` both work, and `shopt` alone lists everything. Extended
patterns `?(…) *(…) +(…) @(…) !(…)` work in `case`, `[[ ]]` and globbing with
nothing to switch on, which is what bash's parse-time `extglob` cannot manage —
see [0017](adr/0017-one-namespace-for-options.md).

**Coprocesses and limits.** `coproc [name] cmd` starts a coprocess whose ends
are `${name[in]}` and `${name[out]}` — and `${name[0]}`/`${name[1]}` for bash —
spoken to with the same `send` and `recv` as a socket. `mapfile` / `readarray`,
`wait -n [-p var]`, `trap … DEBUG` (the command is in `$CMD`), `hash`, `ulimit`,
`|&` and `printf '%(fmt)T'` are all there too.

**Special variables.** `$@ $* $# $? $$ $! $0–$9 $RANDOM $SECONDS $PPID $UID
$EUID $HOSTNAME $HIBR_VERSION $HIBR_ABI`, plus `$RET`, `$ERRMSG`, `$ERR`, `$ERRSTATUS`,
`$REMOTE` and `$M`. `$$` is fixed at startup, so it is the same inside every
subshell. `$HIBR_VERSION` holds the version and nothing else does, so it is
the way to ask which shell is running. It is set at startup over anything
inherited, so a planted `HIBR_VERSION` in the environment cannot claim a shell
is hibr when it is not, and it is not exported, so a child shell does not
inherit a stale answer either. `$SHELL` cannot answer that question at all —
it is the login shell out of `/etc/passwd`, and no shell sets it.
`$HIBR_ABI` is the module ABI the shell was built with, so a script can tell
whether a module it carries will be accepted before it tries to load it; both
are set the same way and carry the same guarantees.

**Redirection.** `<  >  >>  <>  n>&m  n<&m  &>  &>>  >|` (with `set -C`
noclobber), here-documents `<<` and `<<-`, here-strings `<<<`, named
descriptors `{fd}>file` and `{fd}>&-`, and process substitution `<( )` and
`>( )`. Redirections work on commands, groups, conditionals and loops alike.

## Arithmetic

```
((i++)); ((n > 3)) && echo big
let "total = a * b"
for ((i = 0; i < 10; i += 2)); do …; done       # or { …; } as the body
echo $(( x > 0 ? x : -x )) $(( 2 ** 10 )) $(( a = 3, b = 4, a * b ))
```

The full C operator set: `= += -= *= /= %= <<= >>= &= |= ^= **=`, prefix and
postfix `++`/`--`, `?:`, the comma operator, and `**`. `&&`, `||` and `?:`
short-circuit for real, so `(( 0 && (x=9) ))` leaves `x` alone and
`0 && 1/0` is not an error. Overflow wraps as two's complement exactly as in
bash, `INT64_MIN / -1` is defined rather than trapping, and the evaluator is
clean under UndefinedBehaviorSanitizer. An arithmetic error aborts the command
it appears in, with status 1.

`+=` appends: `s+=tail`, `list+=(more items)`, `map[key]+=tail`.

## Conditionals

`[[ … ]]` never splits or globs its operands. It has `&& || ! ( )`, pattern
`==`/`!=` (quoted text is literal), string `< >`, numeric `-eq -ne -lt -le -gt
-ge`, file tests including `-nt -ot -ef`, `-v name` for "is set", and `=~`.
The right side of `&&` or `||` is not even expanded when the left side decides
the answer.

```
[[ $name == prefix* && -n $other ]]
[[ $line =~ ^([a-z]+)=(.*)$ ]] && echo "${M[1]} is ${M[2]}"
```

`test` and `[` are there as well.

## Maps, arrays and nesting

One value model: a variable is a scalar or an ordered map, and an array is just
a map with numeric keys. Subscripts chain, so nesting needs no new syntax.

```
h[users][omar][role]=admin          # intermediate levels are created as needed
echo ${h[users][omar][role]}
echo ${!h[users][@]}                # keys at that level
echo ${h[users][omar][@]}           # values at that level
echo ${#h[users][@]}                # entry count
cfg=([host]=localhost [port]=8080)
a=(one two three); a[7]=eight       # sparse: keys 0 1 2 7, count 4
unset a[1]
echo "${a[@]:1:2}"                  # slices
```

A subscript made only of digits is a literal key; one containing an operator is
evaluated arithmetically, so `${a[i+1]}` works; a bare name is used for its
value when that value is a number and taken literally otherwise, so
`${cfg[host]}` means the key `host`.

One trap follows from that rule: a hyphen is an operator, so
`head[content-type]` is read as `content - type` and lands on the key `0`,
silently and in both directions. Fold hyphens to underscores before using
text as a key, or reach the value with `json get`. See
[decision 0006](adr/0006-arrays-are-sparse-maps.md). `"${a[@]}"` gives one field per entry;
`"${a[*]}"` joins them.

## Functions with real signatures

```
fn greet(name, greeting = "hello") { echo "$greeting, $name"; }
fn add(int a, int b) -> int { ret $((a + b)); }
fn tally(label, ...rest) { echo "$label: ${#rest[@]} items"; }
fn conf(map m, str key) { echo "${m[$key]}"; }
```

Parameters become locals, in order; `$1` and `$@` still work. Types are `int
num str path arr map any`, checked on every call. A missing argument without a
default, a surplus argument without `...rest`, or a wrong type fails the call
with status 2 and the body does not run. `arr` and `map` parameters take a
variable's name and bind a copy. The old `name() { … }` form still works,
unchecked.

**Results without a subshell.** `ret` puts a value in the result slot; `:=` runs
a command and binds that slot to a variable. Nothing forks.

```
add 2 3              # $RET is 5
sum := add 10 32     # sum is 42
fn pair() { ret left right; }
p := pair            # several values become an array
files := ls -q src   # works with builtins and modules too
```

`:=` clears the slot first, so a command that never sets it binds an empty
value, and it tells builtins they are being bound so they fill the slot without
printing. A function that should print uses `echo`; `$( )` still works and still
forks.

`local` gives dynamic scoping as in bash: a called function sees its caller's
locals, and every binding is restored on return, including its export flag.

## Declared command-line arguments

No `getopts` loop, no `shift`, no `case`:

```
opt .  "Frobnicate the widgets"
opt -v --verbose  verbose         "Chatty output"
opt -n --count    count  int=1    "How many times"
opt -o --output   out    path!    "Where to write"     # ! = required
opt -t --tag      tags   str+     "A tag, repeatable"  # + = collects an array
args "$@"
```

After `args`, `$verbose` is 1 or 0, `$count` has been type-checked, `${tags[@]}`
holds every `--tag`, and positional arguments are in `${ARGS[@]}` and in `$1
$2 …`. It accepts `--count=3`, `--count 3`, `-n3`, bundled `-vn3`,
`--no-verbose`, and `--`. `-h`/`--help` prints a usage table generated from the
declarations. On an error it prints the reason and the usage and returns 2; at
the top level of a script that also ends it, and inside a function or `try` it
just returns.

## Errors

```
fn fetch(str url) {
  try something "$url"
  [ "$ERR" -eq 0 ] || fail "fetch failed: $ERRMSG"
  ret "$RET"
}
```

`fail [-s status] message` sets `$ERRMSG` and returns from the function, as the
error-shaped twin of `ret`. `try command…` runs a command with failure caught:
it sets `$ERR` (0 or 1), `$ERRSTATUS` and `$ERRMSG` and always succeeds itself;
inside it neither `set -e` nor the `ERR` trap fires. `trap 'handler' ERR` runs
on any failure outside a tested context. `set -e` and `set -u` are supported.

## Strict expansion

`set -S` changes one rule: the result of an expansion is never split and never
globbed. What you write splits; what expands does not.

```
f="my file.txt"; g="*.c"
count $f      # default: 2 arguments      strict: 1
count $g      # default: every .c file    strict: the literal *.c
count $empty  # default: no argument      strict: one empty argument
```

The last line is the deliberate cost, and the point: `rm -rf $dir/*` can no
longer become `rm -rf /*` because `$dir` was empty. It is off by default.

---

[← documentation index](README.md) · [← project README](../README.md)
