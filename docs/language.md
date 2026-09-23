# The language

A guided tour. For the formal definition — lexical structure, an EBNF grammar,
precedence tables and the order expansions happen in — see
[the grammar](grammar.md); for what each builtin takes, see
[builtins](builtins.md).

## Everything you expect from a shell

Pipelines, `&&`, `||`, `!`, `;`, background `&`, subshells `( )`, groups `{ }`,
`if`/`elif`/`else`, `while`, `until`, `for`, `case` (with `|`, a leading `(`,
and the `;&` / `;;&` fallthroughs), functions with recursion, and `local`.

**Expansion.** Single, double and `$'…'` quoting (`\n \t \e \xHH \0NNN`).
`${x:-d} ${x:=d} ${x:?msg} ${x:+alt}`, `${#x}`, `${x#p} ${x##p} ${x%p} ${x%%p}`,
`${x/p/r} ${x//p/r}`, anchored `${x/#p/r}` and `${x/%p/r}`, `${x:off:len}` with
negative offsets and negative lengths, over a scalar's characters or a list's
elements — `${*:2}`, `${@:2:1}` and `${a[*]:1}` all select, they do not slice
the joined text, `${x^^} ${x,,} ${x^} ${x,}`, the transforms `${x@Q} ${x@E}
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

**Special variables.** `$@ $* $# $? $$ $! $0–$9 $RANDOM $SECONDS $EPOCHSECONDS
$EPOCHREALTIME $PPID $UID $EUID $HOSTNAME $HIBR_VERSION $HIBR_ABI`, plus `$RET`, `$ERRMSG`, `$ERR`, `$ERRSTATUS`,
`$REMOTE` and `$M`. Assigning to `$RANDOM`, `$SECONDS`, `$EPOCHSECONDS` or
`$EPOCHREALTIME` makes an ordinary variable that hides the live one until it
is unset: `RANDOM=5` reads 5 from then on, where bash would seed with it, and
bash ignores an assignment to the two clocks. `$$` is fixed at startup, so it is the same inside every
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

## Control flow

Everything POSIX has, plus what bash added, plus extended patterns with nothing
to switch on.

```sh
for f in a b c; do printf '%s ' "$f"; done           # a b c
for ((i = 0; i < 3; i++)); do printf '%s ' "$i"; done # 0 1 2

i=0; while [ $i -lt 3 ]; do printf '%s ' "$i"; i=$((i+1)); done  # 0 1 2
i=0; until [ $i -ge 3 ]; do printf '%s ' "$i"; i=$((i+1)); done  # 0 1 2

m=([a]=1 [b]=2)
for k in "${!m[@]}"; do printf '%s=%s ' "$k" "${m[$k]}"; done    # a=1 b=2
```

`break n` and `continue n` reach outward through `n` loops:

```sh
for x in 1 2 3 4; do
  [ $x = 2 ] && continue
  [ $x = 4 ] && break
  printf '%s ' "$x"
done                                                  # 1 3
```

`case` matches patterns, `|` separates alternatives, `;&` falls through to the
next body and `;;&` re-tests from the next arm. The last arm may omit its `;;`.

```sh
case foo.log in
  !(*.txt))  echo "not a text file" ;;      # extended groups need no shopt
  *.log|*.txt) echo "a log or a text file" ;;
  *)         echo "something else"
esac
```

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

### What a subscript means

One syntax serves two purposes, so there is a rule:

| subscript | read as |
|---|---|
| all digits — `a[2]` | the literal key `2` |
| contains an operator — `a[i+1]` | arithmetic |
| a bare name — `cfg[host]` | its value when that is a number, the literal key otherwise |
| **quoted** — `h["content-type"]` | **always the literal key** |
| negative — `a[-1]` | counted back from the highest key |

The quoted form is the escape hatch, and it is the one quoting already implies
everywhere else. Without it a hyphen is an operator, so an unquoted
`head[content-type]` reads as `content - type` and lands on the key `0`,
silently and in both directions:

```sh
h["content-type"]=applicaton/json
echo "${h["content-type"]}"   # applicaton/json
echo "${!h[@]}"               # content-type

h[content-type]=elsewhere     # unquoted: still arithmetic
echo "${!h[@]}"               # content-type 0
```

That is why `json parse` can hand back a document with any field name at all and
every field stays reachable. See
[decision 0006](adr/0006-arrays-are-sparse-maps.md).

`"${a[@]}"` gives one field per entry; `"${a[*]}"` joins them with the first
character of `IFS`. Unquoted, both split.

### Reaching a name held in a variable

```sh
w=w1
to="$w[out]"
send "${!to}" hello           # ${!ref} follows subscripts, and nests
```

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

```
$ prog -vn3 --tag a --tag b x y
verbose=1 count=3 tags=[a b] rest=[x y] dollar1=x
$ prog --count=5
verbose=0 count=5 tags=[] rest=[] dollar1=
$ prog -t one -t two -- -notanopt
verbose=0 count=1 tags=[one two] rest=[-notanopt] dollar1=-notanopt
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
inside it neither `set -e` nor the `ERR` trap fires.

```sh
fn risky(str what) { fail "cannot reach $what"; }
try risky example.com
echo "ERR=$ERR ERRSTATUS=$ERRSTATUS ERRMSG=$ERRMSG"
try true
echo "after success: ERR=$ERR"
```

```
ERR=1 ERRSTATUS=1 ERRMSG=cannot reach example.com
after success: ERR=0
```

`trap 'handler' ERR` runs on any failure outside a tested context, and
`trap 'handler' EXIT` runs on the way out — including out of a subshell, a
command substitution, a background job or a pipeline element, while an
*inherited* trap still fires only once, in the parent.

Three more things that stop a script rather than letting it limp on: an unset
variable under `set -u`, a `${x:?message}` guard, and an assignment to a
readonly or to a declared type that refuses the value. Each prints the reason
and, in a non-interactive shell, leaves.

**[hibr]** `set -e` is scoped to the tested pipeline and does not reach into the
bodies of functions it calls, and there is no `pipefail` —
[0002](adr/0002-errexit-is-scoped.md).

## Strict expansion

`set -S` changes one rule: the result of an expansion is never split and never
globbed. What you write splits; what expands does not.

```sh
count() { echo "$#"; }
f="my file.txt"; empty=""
echo "default: $(count $f) $(count $empty)"
set -S
echo "strict:  $(count $f) $(count $empty)"
```

```
default: 2 0
strict:  1 0
```

An empty expansion still disappears under `-S`, the same as it does by default
and the same as in zsh; only a quoted `"$empty"` gives an empty argument.

A glob behaves the same way: `count $g` with `g="*.c"` counts every `.c` file by
default and exactly one argument — the literal `*.c` — under `set -S`.

It guards the value that arrives in a word, not the word you wrote: `$dir/*`
still globs under `-S`, because that `*` is not the result of an expansion.
Whether it should become the default is settled, with the measurements, in
[0009](adr/0009-strict-expansion-is-opt-in.md).

The last line is the deliberate cost, and the point: `rm -rf $dir/*` can no
longer become `rm -rf /*` because `$dir` was empty. It is off by default.

## Where to go next

| | |
|---|---|
| [The grammar](grammar.md) | The formal definition: EBNF, precedence, expansion order |
| [Builtins](builtins.md) | Every builtin, what it takes and what it gives back |
| [Text, regex and JSON](data.md) | `str`, `arr`, `json`, `match`, `rsub`, operation by operation |
| [Cookbook](cookbook.md) | Whole tasks solved end to end |
| [Decisions](adr/README.md) | Why any of this differs from bash |

---

[← documentation index](README.md) · [← project README](../README.md)
