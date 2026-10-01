# The language

A guided tour. For the formal definition — lexical structure, an EBNF grammar,
precedence tables and the order expansions happen in — see
[the grammar](grammar.md); for what each builtin takes, see
[builtins](builtins.md).

## Everything you expect from a shell

Pipelines, `&&`, `||`, `!`, `;`, background `&`, subshells `( )`, groups `{ }`,
`if`/`elif`/`else`, `while`, `until`, `for`, `select`, `case` (with `|`, a
leading `(`, and the `;&` / `;;&` fallthroughs), functions with recursion --
written `name() { … }`, bash's `function name { … }`, or hibr's `fn` -- and
`local`. An empty `then`, `do` or `{ }` is a syntax error, as in bash, and a
syntax error is status 2.

**Expansion.** Single, double and `$'…'` quoting (`\n \t \e \xHH \0NNN \uHHHH \UHHHHHHHH`, the last two a character by its Unicode code point).
`${x:-d} ${x:=d} ${x:?msg} ${x:+alt}`, `${#x}`, `${x#p} ${x##p} ${x%p} ${x%%p}`,
`${x/p/r} ${x//p/r}`, anchored `${x/#p/r}` and `${x/%p/r}`, `${x:off:len}` with
negative offsets and negative lengths, over a scalar's characters or a list's
elements — `${*:2}`, `${@:2:1}` and `${a[*]:1}` all select, they do not slice
the joined text, `${x^^} ${x,,} ${x^} ${x,}`, the transforms `${x@Q} ${x@E}
${x@U} ${x@L} ${x@u}`, indirection `${!ref}` (which keeps any modifier after
it, so `${!ref:-d}` works), the names starting with a prefix `${!pre*}` and
`${!pre@}`, and negative subscripts `${a[-1]}`, counted back from the highest
key so a sparse array answers the same as bash. Command substitution `$( )` and
backquotes, arithmetic `$(( ))` (and bash's older `$[ ]`), tilde,
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
$EPOCHREALTIME $LINENO $PPID $UID $EUID $HOSTNAME $HIBR_VERSION $HIBR_ABI $HIBR`, plus `$RET`, `$ERRMSG`, `$ERR`, `$ERRSTATUS`,
`$REMOTE` and `$M`, and `$BASH_SOURCE`: the file the running code came from --
the script, a file being sourced, or, inside a function, the file the
function was defined in, which is how a sourced file finds its own directory
(`${BASH_SOURCE%/*}`). As in bash, `RANDOM=5` seeds the generator, `SECONDS=5`
restarts the count from 5, and an assignment to `$EPOCHSECONDS` or
`$EPOCHREALTIME` is ignored; `${RANDOM[0]}` and the like read the value, as
`[0]` of any scalar does. Only `${BASH_SOURCE[0]}` is kept, not the stack of
callers' files beneath it. `$$` is fixed at startup, so it is the same inside every
subshell. `$HIBR_VERSION` holds the version and nothing else does, so it is
the way to ask which shell is running. It is set at startup over anything
inherited, so a planted `HIBR_VERSION` in the environment cannot claim a shell
is hibr when it is not, and it is not exported, so a child shell does not
inherit a stale answer either. `$SHELL` cannot answer that question at all —
it is the login shell out of `/etc/passwd`, and no shell sets it.
`$HIBR_ABI` is the module ABI the shell was built with, so a script can tell
whether a module it carries will be accepted before it tries to load it; both
are set the same way and carry the same guarantees. `$HIBR` is this shell's
own absolute path, asked of the system (`/proc/self/exe`, or
`_NSGetExecutablePath` on macOS) rather than guessed from the name it was
started under, which is what bash's `$BASH` does and which a caller can set
to anything; only where the system cannot say is the name looked up on
`PATH`. So a script that starts
another copy of itself runs `"$HIBR" "$0"` and gets the shell it is running
under, not whichever `hibr` comes first on `PATH`. It is not called `$BASH`
because `[ -n "$BASH" ]` is how scripts ask whether they are in bash.

**Redirection.** `<  >  >>  <>  n>&m  n<&m  &>  &>>  >|` (with `set -C`
noclobber), here-documents `<<` and `<<-`, here-strings `<<<`, named
descriptors `{fd}>file` and `{fd}>&-`, and process substitution `<( )` and
`>( )`. Redirections work on commands, groups, conditionals and loops alike.

## Control flow

Everything POSIX has, plus what bash added, plus extended patterns with nothing
to switch on.

```sh
for f in a b c; do printf '[%s]' "$f"; done; echo
for ((i = 0; i < 3; i++)); do printf '[%s]' "$i"; done; echo

i=0; while [ $i -lt 3 ]; do printf '[%s]' "$i"; i=$((i+1)); done; echo
i=0; until [ $i -ge 3 ]; do printf '[%s]' "$i"; i=$((i+1)); done; echo

m=([a]=1 [b]=2)
for k in "${!m[@]}"; do printf '[%s=%s]' "$k" "${m[$k]}"; done; echo
```

```output
[a][b][c]
[0][1][2]
[0][1][2]
[0][1][2]
[a=1][b=2]
```

`break n` and `continue n` reach outward through `n` loops:

```sh
for x in 1 2 3 4; do
  [ $x = 2 ] && continue
  [ $x = 4 ] && break
  printf '[%s]' "$x"
done; echo
```

```output
[1][3]
```

The loop a `break` leaves is one in the same function: a function cannot
break its caller's loop, as in bash. `break` or `continue` with no loop
around it, and `return` or `ret` outside a function or a sourced file, are
reported and do nothing -- the script carries on, with status 0 for `break`
and 2 for `return`. And an `exit` or `return` inside an `if`, `while` or `!`
condition keeps its own status:

```sh
f() { break; }
for i in 1 2; do f 2>/dev/null; echo "loop $i"; done
return 3 2>/dev/null; echo "return outside a function: status $?"
(if exit 3; then :; fi); echo "an exit inside a condition keeps its status: $?"
```

```output
loop 1
loop 2
return outside a function: status 2
an exit inside a condition keeps its status: 3
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

```output
not a text file
```

## Arithmetic

```sh
n=5; ((n++)); ((n > 3)) && echo "big: $n"
a=2 b=3; let "total = a * b"; echo "total=$total"
for ((i = 0; i < 10; i += 2)); do printf '[%s]' "$i"; done; echo
x=-7
echo $(( x > 0 ? x : -x )) $(( 2 ** 10 )) $(( a = 3, b = 4, a * b ))
```

```output
big: 6
total=6
[0][2][4][6][8]
7 1024 12
```

The full C operator set: `= += -= *= /= %= <<= >>= &= |= ^= **=`, prefix and
postfix `++`/`--`, `?:`, the comma operator, and `**`. `&&`, `||` and `?:`
short-circuit for real, so `(( 0 && (x=9) ))` leaves `x` alone and
`0 && 1/0` is not an error. Overflow wraps as two's complement exactly as in
bash, `INT64_MIN / -1` is defined rather than trapping, and the evaluator is
clean under UndefinedBehaviorSanitizer. An arithmetic error in `(( ))` or
`let` fails that command with status 1; one in an expansion -- `$(( ))`, an
array subscript -- ends a non-interactive script with status 1, as in bash,
and abandons the line at the prompt. Inside `try` it fails only the command,
so `(( v = expr, 1 ))` or `try` is how a script evaluates something that may
not be an expression.

`+=` appends: `s+=tail`, `list+=(more items)`, `map[key]+=tail`.

A quoted subscript is a literal key inside arithmetic as everywhere else,
and `$[ ]` is bash's older spelling of `$(( ))`:

```sh
m["x-y"]=4
echo $(( m["x-y"] * 2 )) $[ 1 + 1 ]
```

```output
8 2
```

## Conditionals

`[[ … ]]` never splits or globs its operands. It has `&& || ! ( )`, pattern
`==`/`!=` (quoted text is literal), string `< >`, numeric `-eq -ne -lt -le -gt
-ge`, file tests including `-nt -ot -ef`, `-v name` for "is set", and `=~`.
The right side of `&&` or `||` is not even expanded when the left side decides
the answer.

```sh
name=prefix-1 other=y
[[ $name == prefix* && -n $other ]] && echo "both hold"
line="port=8080"
[[ $line =~ ^([a-z]+)=(.*)$ ]] && echo "${M[1]} is ${M[2]}, and ${BASH_REMATCH[1]} in bash's name"
```

```output
both hold
port is 8080, and port in bash's name
```

**[hibr]** `=~` puts its captures in `M`, and fills bash's `BASH_REMATCH`
with the same values; the `match` builtin fills `M` only --
[0004](adr/0004-regex-captures-go-to-M.md).

`test` and `[` are there as well.

## Maps, arrays and nesting

One value model: a variable is a scalar or an ordered map, and an array is just
a map with numeric keys. Subscripts chain, so nesting needs no new syntax.

```sh
h[users][omar][role]=admin          # intermediate levels are created as needed
h[users][ana][role]=dev
echo ${h[users][omar][role]}
echo ${!h[users][@]}                # keys at that level
echo ${h[users][omar][@]}           # values at that level
echo ${#h[users][@]}                # entry count
cfg=([host]=localhost [port]=8080)
echo "${cfg[host]}:${cfg[port]}"
a=(one two three); a[7]=eight       # sparse
echo "keys ${!a[*]}, count ${#a[@]}"
unset a[1]
echo "${a[@]:1:2}"                  # slices
```

```output
admin
omar ana
admin
2
localhost:8080
keys 0 1 2 7, count 4
three eight
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
| any subscript of an array made with `declare -A` | the literal key, after expansion, as in bash |

The quoted form is the escape hatch, and it is the one quoting already implies
everywhere else. Without it a hyphen is an operator, so an unquoted
`head[content-type]` reads as `content - type` and lands on the key `0`,
silently and in both directions:

```sh
h["content-type"]=applicaton/json
echo "${h["content-type"]}"
echo "${!h[@]}"

h[content-type]=elsewhere     # unquoted: still arithmetic
echo "${!h[@]}"
```

```output
applicaton/json
content-type
content-type 0
```

That is why `json parse` can hand back a document with any field name at all and
every field stays reachable. See
[decision 0006](adr/0006-arrays-are-sparse-maps.md).

`"${a[@]}"` gives one field per entry; `"${a[*]}"` joins them with the first
character of `IFS`. Unquoted, both split.

### Reaching a name held in a variable

```sh
coproc w1 cat
w=w1
to="$w[out]"
from="$w[in]"
send "${!to}" hello           # ${!ref} follows subscripts, and nests
recv "${!from}" reply
echo "$reply"
```

```output
hello
```

## Functions with real signatures

```sh
fn greet(name, greeting = "hello") { echo "$greeting, $name"; }
fn add(int a, int b) -> int { ret $((a + b)); }
fn tally(label, ...rest) { echo "$label: ${#rest[@]} items"; }
fn conf(map m, str key) { echo "${m[$key]}"; }
fn at(int? col = "", int row = 1) { echo "col=[$col] row=$row"; }
greet world; greet world hi
tally fruit apple pear fig
c=([host]=localhost); conf c host
at; at "" 3
add 2 x 2> /dev/null || echo "add refused a word: status $?"
```

```output
hello, world
hi, world
fruit: 3 items
localhost
col=[] row=1
col=[] row=3
add refused a word: status 2
```

Parameters become locals, in order; `$1` and `$@` still work. Types are `int
num str path arr map any`, checked on every call. A type ending in `?` also
accepts the empty string, so `int? col = ""` is an optional integer, and
`at "" 3` passes an empty one on purpose -- see
[0024](adr/0024-a-type-can-allow-empty.md). A missing argument without a
default, a surplus argument without `...rest`, or a wrong type fails the call
with status 2 and the body does not run. `arr` and `map` parameters take a
variable's name and bind a copy. The old `name() { … }` form still works,
unchecked.

**Results without a subshell.** `ret` puts a value in the result slot; `:=` runs
a command and binds that slot to a variable. Nothing forks.

<!-- setup
fn add(int a, int b) -> int { ret $((a + b)); }
mkdir src; touch src/a.c src/b.h
-->
```sh
add 2 3; echo "RET=$RET"
sum := add 10 32; echo "sum=$sum"
fn pair() { ret left right; }
p := pair            # several values become an array
echo "${#p[@]}: ${p[*]}"
need ls              # a module's builtins bind too
files := ls -q src
echo "${#files[@]}: ${files[*]}"
```

```output
RET=5
sum=42
2: left right
2: a.c b.h
```

`:=` clears the slot first, so a command that never sets it binds an empty
value, and it tells builtins they are being bound so they fill the slot without
printing. A function that should print uses `echo`; `$( )` still works and still
forks.

`local` gives dynamic scoping as in bash: a called function sees its caller's
locals, and every binding is restored on return, including its export flag.

## Declared command-line arguments

No `getopts` loop, no `shift`, no `case`:

```sh
prog() {
  opt .  "Frobnicate the widgets"
  opt -v --verbose  verbose         "Chatty output"
  opt -n --count    count  int=1    "How many times"
  opt -o --output   out    path!    "Where to write"     # ! = required
  opt -t --tag      tags   str+     "A tag, repeatable"  # + = collects an array
  args "$@" || return
  echo "verbose=$verbose count=$count out=$out tags=[${tags[*]}] rest=[${ARGS[*]}] \$1=$1"
}
prog -vn3 -o log.txt --tag a --tag b x y
prog --count=5 --output=log.txt
prog -o log.txt -t one -t two -- -notanopt
prog -v 2> /dev/null || echo "no --output: status $?"
```

```output
verbose=1 count=3 out=log.txt tags=[a b] rest=[x y] $1=x
verbose=0 count=5 out=log.txt tags=[] rest=[] $1=
verbose=0 count=1 out=log.txt tags=[one two] rest=[-notanopt] $1=-notanopt
no --output: status 2
```

After `args`, `$verbose` is 1 or 0, `$count` has been type-checked, `${tags[@]}`
holds every `--tag`, and positional arguments are in `${ARGS[@]}` and in `$1
$2 …`. It accepts `--count=3`, `--count 3`, `-n3`, bundled `-vn3`,
`--no-verbose`, and `--`. `-h`/`--help` prints a usage table generated from the
declarations. On an error it prints the reason and the usage and returns 2; at
the top level of a script that also ends it, and inside a function or `try` it
just returns. `args` uses its declarations up: the next `args` starts from the
`opt` lines before it, so a function can declare and parse on every call, and
two functions never see each other's options.

## Errors

<!-- setup
something() { [ "$1" = up ] && ret "fetched $1" || fail "no route to $1"; }
-->
```sh
fn fetch(str url) {
  try something "$url"
  [ "$ERR" -eq 0 ] || fail "fetch failed: $ERRMSG"
  ret "$RET"
}
r := fetch up; echo "$r"
try fetch down; echo "$ERRMSG"
```

```output
fetched up
fetch failed: no route to down
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

```output
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
and, in a non-interactive shell, leaves; an interactive one abandons the line.
An assignment in front of a command, `r=2 cmd`, only warns, as in bash:

```sh
( readonly r=1; r=2; echo never ) 2>&1; echo "status $?"
```

```output
hibr: r: readonly variable
status 1
```

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

```output
default: 2 0
strict:  1 0
```

An empty expansion still disappears under `-S`, the same as it does by default
and the same as in zsh; only a quoted `"$empty"` gives an empty argument.

A glob behaves the same way: `count $g` with `g="*.c"` counts every `.c` file by
default and exactly one argument — the literal `*.c` — under `set -S`.

A pattern is a glob too. Under `-S` a pattern that comes from a variable --
in `case`, on the right of `[[ == ]]`, in `${x#$p}` and its kin -- matches
only itself, exactly as if it had been quoted:

```sh
p="a*"
case abc in $p) echo "default: $p matches abc" ;; esac
set -S
case abc in $p) echo "strict: matches" ;; *) echo "strict: $p is only itself" ;; esac
case "a*" in $p) echo "strict: $p matches a*" ;; esac
```

```output
default: a* matches abc
strict: a* is only itself
strict: a* matches a*
```

Text written inside `${x:+…}`, `${x:-…}` or `${x:=…}` is written, not
expanded, so under `-S` it still splits and globs; an expansion inside it
still does neither:

<!-- setup
mkdir .h
-->
```sh
# in a directory whose one hidden directory is .h
c() { echo "$# [$*]"; }
g="./.*/"
on=1
c ${on:+a b} ${on:+./.*/} ${on:+$g}
set -S
c ${on:+a b} ${on:+./.*/} ${on:+$g}
```

```output
4 [a b ./.h/ ./.h/]
4 [a b ./.h/ ./.*/]
```

It guards the value that arrives in a word, not the word you wrote: `$dir/*`
still globs under `-S`, because that `*` is not the result of an expansion.
Whether it should become the default is settled, with the measurements, in
[0009](adr/0009-strict-expansion-is-opt-in.md).

The last line is the deliberate cost, and the point: `rm -rf $dir/*` can no
longer become `rm -rf /*` because `$dir` was empty. It is off by default.

## Strict checks

`strict` refuses, in the file that says it, the two things a shell script most
often gets wrong without anything failing: a function defined twice, and a
function creating a global because a `local` was forgotten.

```sh
cat > demo.hibr <<'EOF'
strict
greet() { echo hello; }
greet() { echo hi; }
tally() { total=1; }
tally
echo "total=${total-unset}"
keep() { local n; n=1; declare -g seen=yes; }
keep
echo "seen=$seen"
EOF
"$HIBR" demo.hibr 2>&1
```

```output
hibr: demo.hibr:3: greet is already defined at line 2 (strict functions)
hibr: demo.hibr:4: total is not declared -- local total, or declare -g total (strict vars)
total=unset
seen=yes
```

The first definition stays, the undeclared assignment is not made, and each
refused command fails -- status 2 for the definition, 1 for the assignment --
so `set -e` stops there. A function may still assign anything that already
exists, a `local` of its own or of a function that called it, and anything it
creates with `local`, `declare` or `declare -g`. Name the checks to take only
some -- `strict functions vars` -- and `strict off vars` to put one back;
`strict expansion` is `set -S` for this file alone, and `strict -p` lists what
is on. "This file" is `$BASH_SOURCE`, so a function is checked by the file it
was defined in, wherever it is called from. The desktop's own window manager
and widgets, and every app, pane, desk accessory and strip module, run under
all three -- plain `strict` -- with every list they split held in an array,
`read -ra list <<< "$words"`. Why it is per file, and what
it deliberately does not catch, is [0023](adr/0023-strict-is-per-file.md).

`$LINENO` is the line of the command running, counted in its own file -- for a
function, the file it was defined in -- as bash counts it.

## Reading a script, and checking it first

A script runs a command at a time, as it is read, the way bash runs one: a
syntax error late in a file stops it there, after the lines before it have
run, with status 2. Piped in, each command runs as soon as its last line has
arrived, and a `read` in the script takes the script's own next line, as it
does in bash.

`hibr --checkfirst` (or `set -o checkfirst`, or `shopt -s checkfirst`)
parses the whole text first instead -- a script file, `-c` text, standard
input, or a file `source`d while it is on -- and runs none of it when it
does not parse:

```sh
printf 'echo ran\nif\n' > late.sh
"$HIBR" late.sh 2>&1; echo "status $?"
"$HIBR" --checkfirst late.sh 2>&1; echo "status $?"
```

```output
ran
hibr: unexpected end of input
status 2
hibr: unexpected end of input
status 2
```

Standard input is read whole under `checkfirst`, so nothing streams. `eval`,
traps and `$(…)` run as read either way. Why both, and the one case piped
input cannot match bash, is [0026](adr/0026-a-script-runs-as-it-is-read.md).

## A dry run

`hibr --plan script` runs the script's own logic -- variables, functions,
loops, reading files and standard input -- but refuses everything that would
change the machine, and says so, one line each:

```sh
printf 'echo built > out.txt\ncurl -sO https://example.com/app.tgz\nn=$(grep -c . job.sh)\necho "job.sh has $n lines"\nrm -rf build\n' > job.sh
"$HIBR" --plan job.sh 2>&1; echo "status $?"; ls
```

```output
hibr: plan: job.sh:1: would write out.txt
hibr: plan: job.sh:2: would run curl -sO https://example.com/app.tgz
job.sh has 5 lines
hibr: plan: job.sh:5: would run rm -rf build
status 1
job.sh
```

What it refuses, and what it lets through:

| | in a plan |
|---|---|
| `>`, `>>`, `&>`, `<>`, `>\|`, `{fd}>` | open `/dev/null` instead, recorded -- unless inside the plan's own `$TMPDIR` |
| `/dev/tcp`, `/dev/udp`, `/dev/tls`, `/dev/unix` | open `/dev/null`, recorded as a connection |
| a program known only to read | runs: `cat grep head tail wc ls stat find` (no `-delete`, `-exec`) `sed` (no `-i`) `sort` (no `-o`) `jq diff date` and the like, and `git status log diff show` and other reading subcommands |
| `mktemp`, and `rm mkdir touch cp mv ln chmod rmdir` | run when every path they are given is inside the plan's own `$TMPDIR` |
| any other program | refused and recorded, status 1, no output |
| `exec`, `kill` (but `kill -0`), `listen`, `mod load`, `need` | refused and recorded |

A plan gets a scratch `$TMPDIR` of its own, removed when it ends, so a
script that makes a temporary file and reads it back follows its own logic.
A terminal on standard input is replaced by `/dev/null`. The records go to a
copy of standard error taken at the start, so a script's own `2>/dev/null`
cannot hide them; with `--agent` each is a line of JSON keyed `plan`.

What a plan cannot do, stated plainly: a refused program fails, so a script
that needed its result -- under `set -e`, at once -- stops there, and the
plan is partial; a branch that depends on a write having happened takes the
other way. `awk` is refused, because whether an awk program writes cannot
be told from its text, and a `sed` script's own `w` command is not caught
for the same reason; every program that runs another is refused --
`env` with arguments, `timeout`, `xargs`, `nice`, `sudo`, `sh -c` -- since
letting one through would let anything through. A plan reads whatever the
script reads. Why each choice is [0027](adr/0027-a-dry-run-refuses-what-it-cannot-show-is-harmless.md).

## Agent mode

For a script a program runs rather than a person -- a language model's, a
build's -- `hibr --agent` (or `set -o agent`) makes errors one line of JSON
each, turns on `set -u`, strict expansion and `checkfirst`, and swaps a terminal on
standard input for `/dev/null` so nothing waits on a person who is not
there. `HIBR_TIMEOUT=seconds` bounds each foreground process: TERM, KILL two
seconds later, status 124.

```sh
printf 'echo start\necho "$missing"\n' > run.sh
"$HIBR" --agent run.sh 2>&1; echo "status $?"
HIBR_TIMEOUT=1 "$HIBR" --agent -c 'sleep 30; echo "status $?"' 2>&1
```

```output
start
{"error":"missing: unbound variable","file":"run.sh","line":2,"source":"echo \"$missing\""}
status 1
{"error":"timed out after 1 seconds (HIBR_TIMEOUT)","file":"command line","line":1}
status 124
```

A syntax error adds its column. The timeout bounds processes, not the shell's
own work: a builtin, a function or a loop is not a process to end. Why each
choice, and what it leaves out, is [0025](adr/0025-agent-mode.md).

## Explaining a script

`hibr --explain script` parses a script, runs none of it, and names each
common mistake it finds, with the line and what to write instead:

```sh
printf 'cd build\nrm $out\nres := uname\n' > go.sh
"$HIBR" --explain go.sh 2>&1; echo "status $?"
```

```output
hibr: go.sh:1: cd-unchecked: cd can fail, and then everything after it runs in the wrong directory; write cd ... || exit
hibr: go.sh:2: unquoted-path: rm is handed $out unquoted: a blank or a * in its value makes more paths than meant; write "$out"
hibr: go.sh:3: bind-program: := binds what a builtin or a function returns, and uname is a program: it prints and the variable stays empty; write x=$(uname ...)
status 1
```

Status is 0 for a clean script, 1 when anything was found and 2 for a
syntax error. `-c 'text'` and standard input work as they do for running;
with `--agent` each finding is a line of JSON keyed `warning`. The rules:

| rule | what it catches |
|---|---|
| `unquoted-path` | an unquoted expansion handed to `rm`, `mv`, `cp`, `rmdir`, `ln`, `chmod`, `chown`, `chgrp`, `touch` or `mkdir` |
| `cd-unchecked` | a `cd` outside a condition, in a script that never says `set -e` (`cd /` is let through) |
| `for-ls` | `for f in $(ls)` |
| `test-unquoted` | `[ $x = y ]`, where an empty `$x` leaves the test a word short; not `$#`, `$?`, `${#x}`, nor a variable the script only ever gives a number |
| `bind-program` | `x := program` for a program on the PATH, which prints and binds nothing |
| `unset-quoted-key` | `unset 'm[$k]'`, which here removes a key literally named `$k` |
| `local-self-ref` | `local a=$1 b=${m[$a]}`, where `b` reads `a` before it is assigned |
| `local-masks-status` | `$?` read right after `local x=$(cmd)`, which is `local`'s status |
| `dead-return` | a failing `return` straight after `ret`, which has already ended the function |
| `bare-key` | `${m[row]}` with `row` never assigned, on an array this file makes without `-A` |

The rules live in a module, `lint`, loaded only by `--explain`, so running a
script never pays for them. bash's `set -e`-inside-a-condition trap is not a
rule, because hibr's errexit already reaches into those functions -- see
[0002](adr/0002-errexit-is-scoped.md).

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
