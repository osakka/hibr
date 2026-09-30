# hibr, for a language model

hibr is a small shell that runs the bash you already write, and adds a few
things bash does not have. This page is the whole of what you need to write
it well: the rules where it differs from bash, then each addition with an
example. Every example below was run by hibr and its output pasted back
under it; `tests/531-doc-examples.t` runs them again on every build and fails when
one stops matching.

Run a script with `hibr script` or a `#!/usr/bin/env hibr` line;
`hibr -c 'cmd'` and `hibr -n script` (parse only) work as in bash.
`hibr --explain script` checks a script for the mistakes below without
running it.

## Write bash, and mind these differences

Ordinary bash -- variables, quoting, `if`/`case`/`for`/`while`, functions,
pipelines, redirections, here-documents, `$(…)`, `$((…))`, `[[ … ]]`,
arrays, parameter expansion, `trap`, job control -- behaves as in bash.
Where hibr is deliberately different:

| bash | hibr |
|---|---|
| `set -e` reaches into functions called from any context | only the tested pipeline is exempt; a failure inside a function it calls still stops |
| a failing `((expr))` trips `set -e` | `((expr))` never trips `set -e`; its status is a value |
| `pipefail` is an option | any failing stage fails the pipeline for `set -e`; there is no `pipefail` |
| `=~` captures into `BASH_REMATCH` | captures go into `M`: `${M[0]}`, `${M[1]}`, … |
| functions return data by printing it | `ret value` sets the result slot; `x := f args` binds it without a fork |
| an indexed array is a list | arrays are sparse ordered maps; subscripts chain: `${m[a][b]}` |
| an indexed array's subscript is arithmetic, an associative one's a key | the same for `declare -A`; an array made without it reads `h[content-type]` as `content - type`, so quote a literal key: `h["content-type"]` |
| `{$a,$b}` brace-expands | brace expansion is literal-only |
| `**` needs `shopt -s globstar` | `**` is always on, and never follows symlinked directories |
| `extglob` is an option | extended patterns always work |
| `${#s}` counts bytes in some locales | `${#s}` and `${s:i:n}` count characters |
| unquoted expansions split and glob | the same by default; `set -S` or `strict expansion` stops it |
| `"${x:-the machine's zone}"` does not parse | an apostrophe inside `${…}`'s own text is just a character |
| `echo a=~` expands the tilde | only a real assignment does: `x=~`, `PATH=~/bin:~/x` |

A script runs a command at a time, as it is read, as in bash: a syntax
error late in a file stops it there, after the lines before it have run.
`hibr --checkfirst` (or `set -o checkfirst`, and agent mode) parses the
whole script first, so a script that does not parse runs none of it:

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

Other things that are the same as bash but catch people out: a quoted
`"$x"` never splits; leading zeros are octal in arithmetic (`$((08))` is an
error); `local a=$1 b=${m[$a]}` cannot see the `a` it is declaring.

## Functions with real signatures

```sh
fn greet(str name, greeting = "hello") { echo "$greeting, $name"; }
greet world
greet world hi
fn add(int a, int b) -> int { ret $((a + b)); }
sum := add 2 3
echo "sum=$sum"
add 2 x 2> /dev/null || echo "add refused a word: status $?"
```

```output
hello, world
hi, world
sum=5
add refused a word: status 2
```

Parameters become locals, in order. A missing argument without a default,
a surplus one, or one of the wrong type fails the call with status 2 and
the body never runs; `...rest` collects the remainder into an array. Types:
`int num str path arr map any`; a type ending in `?` also accepts empty.

```sh
fn at(int? col = "", int row = 1) { echo "col=[$col] row=$row"; }
at
at 7 2
at "" 3
fn tally(label, ...rest) { echo "$label: ${#rest[@]} -> ${rest[*]}"; }
tally fruit apple pear fig
```

```output
col=[] row=1
col=[7] row=2
col=[] row=3
fruit: 3 -> apple pear fig
```

## The result slot: `ret` and `:=`

`ret` ends the function, like `return`, and puts a value in the slot;
`x := cmd` runs a builtin or function and binds that value, with no
subshell and no fork. It binds what a builtin or function hands back -- a
program on the PATH prints as usual, so capture one with `$(…)`.

```sh
fn half(int n) { ret $((n / 2)); }
h := half 42
echo "$h"
up := str upper "shout"
echo "$up"
m[k] := half 10
echo "${m[k]}"
```

```output
21
SHOUT
5
```

## Maps, nested

```sh
h[users][omar][role]=admin
h[users][omar][shell]=hibr
h[users][ana][role]=dev
echo "${h[users][omar][role]}"
echo "${!h[users][@]}"
echo "${#h[users][omar][@]}"
cfg=([host]=localhost [port]=8080)
echo "${cfg[host]}:${cfg[port]}"
h["content-type"]=text/plain
echo "${h["content-type"]}"
```

```output
admin
omar ana
2
localhost:8080
text/plain
```

```sh
declare -A seen
for w in a-1 b+2 a-1 "c 3"; do
  [ -n "${seen[$w]}" ] && echo "again: $w"
  seen[$w]=1
done
echo "${#seen[@]} distinct: ${!seen[*]}"
```

```output
again: a-1
3 distinct: a-1 b+2 c 3
```

An array made with `declare -A` takes every subscript as a literal key, as
in bash. Without `-A`, quote every literal key. An unquoted subscript that is a bare name reads
that variable's value when it is a number, so `${m[row]}` inside a function
with a numeric `row` reaches the wrong entry without an error.
`unset m["$k"]` removes the key held in `$k`; bash's `unset 'm[$k]'`
removes a key literally named `$k` here.

## Text and arrays, in-process

```sh
s := str replace "a-b-c" "-" "+"
echo "$s"
str split "x,y,z" "," parts
echo "${#parts[@]} ${parts[1]}"
p := str pad "ab" 5
echo "[$p]"
n := str len "naïve"
echo "$n"
a=(pear apple fig apple)
arr sort a
arr uniq a
echo "${a[*]}"
```

```output
a+b+c
3 y
[ab   ]
5
apple fig pear
```

`str`: `len upper lower trim slice index replace split join pad starts ends
contains repeat`. `arr`: `len push pop sort uniq reverse contains map
filter`. `str pad` counts display columns; lengths and slices count
characters.

## Regular expressions

```sh
if match "2026-09-30 London" '^([0-9]+)-([0-9]+)-([0-9]+) (.*)$'; then
  echo "year=${M[1]} place=${M[4]}"
fi
[[ "v12" =~ ^v([0-9]+)$ ]] && echo "major=${M[1]}"
match -a "a1 b22 c333" "[0-9]+" nums
echo "${nums[*]}"
t := rsub -g "hello world" "o" "0"
echo "$t"
```

```output
year=2026 place=London
major=12
1 22 333
hell0 w0rld
```

`rsub` replaces the first match unless given `-g`.

## JSON, with its types kept

```sh
json parse doc '{"name":"hibr","tags":["shell","c"],"meta":{"stars":42,"ok":true}}'
json get doc .meta.stars n
echo "$n"
json type doc .meta.ok
json keys doc . top
echo "$top"
json set doc .meta.stars 43
json set doc .meta.note 007 -s
json emit doc
```

```output
42
boolean
name tags meta
{"name":"hibr","tags":["shell","c"],"meta":{"stars":43,"ok":true,"note":"007"}}
```

A parsed document is an ordinary nested map: `${doc[meta][stars]}` works
too. `-s` keeps a numeric-looking value a string.

## Errors: `try` and `fail`

```sh
fn fetch(str what) { fail "cannot reach $what"; }
try fetch example.com
echo "ERR=$ERR ERRMSG=$ERRMSG"
try true
echo "ERR=$ERR"
```

```output
ERR=1 ERRMSG=cannot reach example.com
ERR=0
```

`fail` sets `$ERRMSG` and leaves the function with status 1 (`-s n` for
another). `try` runs a command with failure caught and always succeeds
itself; inside it neither `set -e` nor an `ERR` trap fires.

## Declared command-line arguments

```sh
fn prog(...argv) {
  opt .  "Frobnicate widgets"
  opt -v --verbose verbose "Chatty"
  opt -n --count count int=1 "How many"
  opt -t --tag tags str+ "A tag, repeatable"
  args "${argv[@]}" || return
  echo "verbose=$verbose count=$count tags=[${tags[*]}] rest=[${ARGS[*]}]"
}
prog -vn3 --tag a --tag b x y
prog --count=5
prog --count=many 2> /dev/null || echo "refused: status $?"
```

```output
verbose=1 count=3 tags=[a b] rest=[x y]
verbose=0 count=5 tags=[] rest=[]
refused: status 2
```

`-h`/`--help` prints a usage table built from the declarations. At the top
level of a script a bad argument ends it with status 2; inside a function
or `try`, `args` returns 2 instead. `args` uses its declarations up, so a
function declares its options and parses them on every call.

## Sockets without nc

```sh
port=$(( 20000 + $$ % 20000 ))
listen -b "$port" LFD
{ exec 3<>/dev/tcp/127.0.0.1/$port; echo ping >&3; read -r r <&3; echo "client got: $r"; } &
accept "$LFD" C
read -r line <&$C
echo "server got: $line"
echo pong >&$C
wait
```

```output
server got: ping
client got: pong
```

`/dev/tcp/host/port`, `/dev/udp/…`, `/dev/tls/…` and `/dev/unix/path` all
open with ordinary redirections; `listen port handler` serves, calling the
handler per connection with the socket as its standard input and output.
TLS verifies certificates unless `HIBR_TLS_INSECURE=1`.

## Strict checks, per file

```sh
strict
f() { made=1; }
f 2> /dev/null || echo "creating a global in a function: refused, status $?"
g() { local kept=1; echo "local is fine: $kept"; }
g
list="a b"
for w in $list; do echo "[$w]"; done
```

```output
creating a global in a function: refused, status 1
local is fine: 1
[a b]
```

`strict` turns on three checks for the file that says it: a function
defined twice in it is refused, a function creating a global without
`local` is refused, and expansions stop splitting and globbing. Name some
to take only those (`strict functions vars`). To split on purpose, say so:
`read -ra words <<< "$list"`.

## Agent mode

Run a script with `hibr --agent` when a program, not a person, will read
what it says:

```sh
printf 'echo start\necho "$missing"\n' > run.sh
"$HIBR" --agent run.sh 2>&1
echo "status $?"
HIBR_TIMEOUT=1 "$HIBR" --agent -c 'sleep 30; echo "status $?"' 2>&1
```

```output
start
{"error":"missing: unbound variable","file":"run.sh","line":2,"source":"echo \"$missing\""}
status 1
{"error":"timed out after 1 seconds (HIBR_TIMEOUT)","file":"command line","line":1}
status 124
```

Each error is one line of JSON on stderr: the message keyed by its level,
the file, the line, the source line, and a column for a syntax error. The
mode also turns on `set -u`, strict expansion and `checkfirst`, and swaps
a terminal on standard input for `/dev/null`. `HIBR_TIMEOUT` bounds each foreground
process -- TERM, KILL two seconds later, status 124 -- but not the shell's
own work: a builtin, a function or a loop is not a process to end.

## Checking a script before running it

```sh
printf 'cd build\nrm $out\nres := uname\n' > go.sh
"$HIBR" --explain go.sh 2>&1
echo "status $?"
"$HIBR" --agent --explain go.sh 2>&1 | head -1
```

```output
hibr: go.sh:1: cd-unchecked: cd can fail, and then everything after it runs in the wrong directory; write cd ... || exit
hibr: go.sh:2: unquoted-path: rm is handed $out unquoted: a blank or a * in its value makes more paths than meant; write "$out"
hibr: go.sh:3: bind-program: := binds what a builtin or a function returns, and uname is a program: it prints and the variable stays empty; write x=$(uname ...)
status 1
{"warning":"cd-unchecked: cd can fail, and then everything after it runs in the wrong directory; write cd ... || exit","file":"go.sh","line":1,"source":"cd build"}
```

Nothing runs. Status is 0 when clean, 1 when something was found, 2 for a
syntax error. It also catches `for f in $(ls)`, `[ $x = y ]`, `unset
'm[$k]'`, `local a=$1 b=${m[$a]}`, `$?` after `local x=$(cmd)`, a failing
`return` after `ret`, and `${m[row]}` with `row` never assigned.

## Modules

`need name` loads a module that provides a name, or says it cannot;
`mod load path.so` loads one by path; `mod list` shows what is loaded.
Modules add builtins and protocols, and a script that uses none pays
nothing for them.

## Mistakes to avoid

- `x := program` binds nothing for a program on the PATH; use `x=$(program)`.
- `ret "$v"; return 1` never reaches the `return`: `ret` has already ended
  the function with status 0.
- An unquoted literal key is looked up as a variable first: write
  `${m["row"]}`, not `${m[row]}`.
- `local id=$1 b=${m[$id]}` cannot see `id`; put `local id=$1` on its own
  line.
- A string of tokens is not a list: `for k in $keys` splits and globs;
  hold the list in an array and write `"${keys[@]}"`.
