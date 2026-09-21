# Text, regex and JSON

Four builtins that do in the shell's own process what a shell usually forks for.
No `sed`, no `awk`, no `jq`, no pipeline, no subshell. Every example below was
run and its output pasted back.

- [How results come back](#how-results-come-back) · [`str`](#str) · [`arr`](#arr)
- [`match` and `rsub`](#match-and-rsub) · [`json`](#json) · [Worked examples](#worked-examples)

## How results come back

Most of these take an **optional variable name** as their last argument. Given
one, the answer goes there; given none, it is printed. Either way it also lands
in `$RET`, so the result slot binds it without forking:

```sh
str upper hello U          # $U is HELLO
str upper hello            # prints HELLO
U := str upper hello       # $U is HELLO, no subshell
```

`str starts`, `str ends`, `str contains`, `arr contains` and `match` answer with
their **exit status** instead, so they read naturally in `if` and `&&`.

## `str`

| form | gives |
|---|---|
| `str len text [var]` | the length in bytes |
| `str upper text [var]` | upper case |
| `str lower text [var]` | lower case |
| `str trim text [var]` | spaces, tabs, newlines and returns off both ends |
| `str slice text start [len] [var]` | a substring; a negative start counts from the end, `-` for len means "to the end" |
| `str index text needle [var]` | the byte offset, or `-1` |
| `str replace text old new [var]` | every occurrence, literal not pattern |
| `str split text sep arrayvar` | split on a literal separator into an array |
| `str join arrayvar [sep] [var]` | join an array, default separator a space |
| `str pad text width [char] [var]` | pad to width; a negative width pads on the left |
| `str repeat text n [var]` | the text `n` times |
| `str starts text prefix` | status: does it start with it |
| `str ends text suffix` | status: does it end with it |
| `str contains text needle` | status: does it contain it |

```sh
str len "hello world" n        # 11
str trim "  padded  " t        # "padded"
str slice "hello world" 6 5 s  # "world"
str slice "hello" -3 - s       # "llo"
str index "hello world" world i  # 6
str index "hello" zzz i        # -1
str replace "a-b-c" - + r      # "a+b+c"
str split "a:b:c" : parts      # parts is (a b c)
str join parts , j             # "a,b,c"
str pad 7 4 0 p                # "7000"
str pad 7 -4 0 p               # "0007"
str repeat ab 3 r              # "ababab"

str starts "$f" / && echo absolute
str contains "$line" ERROR && echo found
```

## `arr`

Arrays are maps with numeric keys, so these work on anything an ordinary
subscript reaches.

| form | does |
|---|---|
| `arr len var [out]` | the number of elements |
| `arr push var v…` | append |
| `arr pop var [out]` | remove and give back the last |
| `arr sort var [-n] [-r]` | sort in place; `-n` numeric, `-r` reversed |
| `arr reverse var` | reverse in place |
| `arr uniq var` | drop later duplicates, keeping order |
| `arr contains var value` | status: is the value in it |
| `arr map var command` | replace each element with what `command` returns |
| `arr filter var command` | keep the elements for which `command` succeeds |

```sh
a=(banana Apple cherry)
arr len a n              # 3
arr push a date          # banana Apple cherry date
arr pop a last           # last=date

b=(3 20 100)
arr sort b               # 100 20 3     (lexical)
arr sort b -n            # 3 20 100     (numeric)
arr sort b -n -r         # 100 20 3

e=(a b a c b); arr uniq e     # a b c
f=(1 2 3);     arr reverse f  # 3 2 1
arr contains a Apple && echo yes
```

**`map` and `filter` take a command**, run once per element, in this shell. A
function is the natural thing to give them:

```sh
fn shout(str x) { ret "<$x>"; }
g=(p q); arr map g shout          # <p> <q>

fn long(str x) { [ ${#x} -gt 3 ]; }
h=(ab abcd xy wxyz); arr filter h long   # abcd wxyz
```

`map` collects each call's `$RET`; `filter` keeps the element when the call
succeeds. Neither forks.

## `match` and `rsub`

POSIX extended regular expressions, with our own engine — `tcc` cannot parse
glibc's `regex.h`, so `include/re.h` is ours.

| form | does |
|---|---|
| `match [-i] [-a] subject pattern [var]` | match; groups land in `M` |
| `rsub [-i] [-g] subject pattern replacement [var]` | substitute; `\1`…`\9` are the groups |

`-i` ignores case, `-a` collects **every** match into an array, `-g` replaces
every occurrence rather than the first.

```sh
match "2024-06-01" '^([0-9]{4})-([0-9]{2})-([0-9]{2})$'
echo "${M[0]}"                 # 2024-06-01, the whole match
echo "${M[1]}/${M[2]}/${M[3]}" # 2024/06/01

match -i HELLO hello && echo matches
match -a "a1 b2 c3" '[a-z][0-9]' hits   # hits is (a1 b2 c3)

rsub    "a1b2" '[0-9]' '#' one   # a#b2
rsub -g "a1b2" '[0-9]' '#' all   # a#b#
rsub -g "john smith" '([a-z]+) ([a-z]+)' '\2, \1' sw   # smith, john
```

`match` fails when there is no match, so it reads as a condition. Captures go to
`M`, not `BASH_REMATCH` — [0004](adr/0004-regex-captures-go-to-M.md). The same
engine backs `=~` inside `[[ ]]`.

## `json`

A JSON document parses **into the map model**, so every ordinary subscript
reaches into it and no query language is needed to read a field.

| form | does |
|---|---|
| `json parse var [text]` | parse text, or standard input, into `var` |
| `json get var path [out]` | read a value or a whole subtree |
| `json set var path value [-s]` | write one; `-s` keeps a numeric-looking value a string |
| `json emit var [-p]` | print the document; `-p` indents |
| `json keys var path [out]` | the keys at that path |
| `json len var path` | how many entries |
| `json type var path` | `string number boolean null array object` |

A path is jq-flavoured: `.` the root, `.meta.stars` a field, `.tags[1]` an
element.

```sh
json parse cfg '{"name":"hibr","tags":["shell","c"],"meta":{"stars":42,"ok":true}}'

echo "${cfg[name]}"           # hibr        — an ordinary subscript
echo "${cfg[tags][0]}"        # shell
echo "${cfg[meta][stars]}"    # 42

json get cfg .meta.stars n    # 42
json get cfg .tags all        # ["shell","c"] — a subtree comes back as JSON
n := json get cfg .meta.stars # into the result slot, no fork

json keys cfg .     top       # name tags meta
json keys cfg .meta mk        # stars ok
json len  cfg .tags           # 2
json type cfg .meta.ok        # boolean
json type cfg .tags           # array

json set cfg .meta.stars 43
json set cfg .meta.note 007 -s   # stays the string "007", not 7
json emit cfg
```

Types survive the round trip: a number stays a number, `true` stays a boolean,
`null` stays null. That is what `-s` is for — without it `007` would come back
out as `7`.

**One sharp edge.** A quoted subscript is a literal key, so a field like
`content-type` is reachable:

```sh
json parse h '{"content-type":"application/json"}'
echo "${h["content-type"]}"   # application/json
echo "${h[content-type]}"     # empty: read as arithmetic, lands on key 0
```

See [0006](adr/0006-arrays-are-sparse-maps.md).

## Worked examples

### Parse a log line into fields

```sh
while read -r line; do
  match "$line" '^([0-9-]+)T([0-9:]+)Z +([A-Z]+) +(.*)$' || continue
  [ "${M[3]}" = ERROR ] || continue
  echo "${M[1]} ${M[2]}: ${M[4]}"
done < app.log
```

No `grep`, no `awk`, no `cut`, and no process started.

### Read an API and pick fields out

```sh
exec 3<>/dev/tls/api.example.com/443
send -r 3 "GET /v1/items HTTP/1.0"; send -r 3 "Host: api.example.com"; send -r 3 ""
recv 3 status
[[ $status =~ ^HTTP/1\.[01]\ ([0-9]{3}) ]] && echo "HTTP ${M[1]}"
while recv 3 h; do [ -z "$h" ] && break; done
recv -a 3 body

json parse api "$body"
for i in $(seq 0 $(( $(json len api .items) - 1 ))); do
  echo "${api[items][$i][name]}"
done
```

TLS, HTTP, and JSON without a single external command.

### Build a query string from a map

```sh
q=([user]=omar [limit]=10)
out=()
for k in "${!q[@]}"; do
  arr push out "$k=${q[$k]}"
done
str join out '&' qs
echo "?$qs"                    # ?user=omar&limit=10
```

### Turn a CSV column into a sorted, unique list

```sh
names=()
while read -r line; do
  str split "$line" , cols
  arr push names "${cols[2]}"
done < people.csv
arr sort names
arr uniq names
str join names $'\n'
```

---

[← documentation index](README.md)
