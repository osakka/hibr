# Text, regex and JSON

Four builtins that do in the shell's own process what a shell usually forks for.
No `sed`, no `awk`, no `jq`, no pipeline, no subshell. Every example that
shows its output is run again on every build by `tests/531-doc-examples.t`,
and the build fails if it stops printing what the page says.

- [How results come back](#how-results-come-back) · [`str`](#str) · [`arr`](#arr)
- [`match` and `rsub`](#match-and-rsub) · [`json`](#json) · [Worked examples](#worked-examples)

## How results come back

Most of these take an **optional variable name** as their last argument. Given
one, the answer goes there; given none, it is printed. Either way it also lands
in `$RET`, so the result slot binds it without forking:

```sh
str upper hello U; echo "$U"
str upper hello
U := str upper hello; echo "$U"
```

```output
HELLO
HELLO
HELLO
```

`str starts`, `str ends`, `str contains`, `arr contains` and `match` answer with
their **exit status** instead, so they read naturally in `if` and `&&`.

## `str`

| form | gives |
|---|---|
| `str len text [var]` | the length in characters |
| `str width text [var]` | the width in terminal columns |
| `str upper text [var]` | upper case |
| `str lower text [var]` | lower case |
| `str trim text [var]` | spaces, tabs, newlines and returns off both ends |
| `str slice text start [len] [var]` | a substring; a negative start counts from the end, `-` for len means "to the end" |
| `str index text needle [var]` | the character offset, or `-1` |
| `str replace text old new [var]` | every occurrence, literal not pattern |
| `str split text sep arrayvar` | split on a literal separator into an array |
| `str join arrayvar [sep] [var]` | join an array, default separator a space |
| `str pad text width [char] [var]` | pad to width **in columns**; a negative width pads on the left |
| `str repeat text n [var]` | the text `n` times |
| `str starts text prefix` | status: does it start with it |
| `str ends text suffix` | status: does it end with it |
| `str contains text needle` | status: does it contain it |

```sh
str len "hello world" n;        echo "$n"
str trim "  padded  " t;        echo "[$t]"
str slice "hello world" 6 5 s;  echo "$s"
str slice "hello" -3 - s;       echo "$s"
str index "hello world" world i; echo "$i"
str index "hello" zzz i;        echo "$i"
str replace "a-b-c" - + r;      echo "$r"
str split "a:b:c" : parts;      echo "${parts[*]}"
str join parts , j;             echo "$j"
str pad 7 4 0 p;                echo "$p"
str pad 7 -4 0 p;               echo "$p"
str repeat ab 3 r;              echo "$r"
f=/etc/hosts; line="an ERROR here"
str starts "$f" / && echo absolute
str contains "$line" ERROR && echo found
str len "─é漢ab" n;            echo "$n characters"
str width "─é漢ab" w;          echo "$w columns"
str pad "漢字" 8 . p;          echo "[$p]"
```

```output
11
[padded]
world
llo
6
-1
a+b+c
a b c
a,b,c
7000
0007
ababab
absolute
found
5 characters
6 columns
[漢字....]
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
arr len a n;        echo "$n"
arr push a date;    echo "${a[*]}"
arr pop a last;     echo "$last, leaving ${a[*]}"
b=(3 20 100)
arr sort b;         echo "${b[*]}"
arr sort b -n;      echo "${b[*]}"
arr sort b -n -r;   echo "${b[*]}"
e=(a b a c b); arr uniq e;    echo "${e[*]}"
f=(1 2 3);     arr reverse f; echo "${f[*]}"
arr contains a Apple && echo yes
```

```output
3
banana Apple cherry date
date, leaving banana Apple cherry
100 20 3
3 20 100
100 20 3
a b c
3 2 1
yes
```

**`map` and `filter` take a command**, run once per element, in this shell. A
function is the natural thing to give them:

```sh
fn shout(str x) { ret "<$x>"; }
g=(p q); arr map g shout;               echo "${g[*]}"
fn long(str x) { [ ${#x} -gt 3 ]; }
h=(ab abcd xy wxyz); arr filter h long; echo "${h[*]}"
```

```output
<p> <q>
abcd wxyz
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
echo "${M[0]}"
echo "${M[1]}/${M[2]}/${M[3]}"
match -i HELLO hello && echo matches
match -a "a1 b2 c3" '[a-z][0-9]' hits; echo "${hits[*]}"
rsub    "a1b2" '[0-9]' '#' one;   echo "$one"
rsub -g "a1b2" '[0-9]' '#' all;   echo "$all"
rsub -g "john smith" '([a-z]+) ([a-z]+)' '\2, \1' sw; echo "$sw"
```

```output
2024-06-01
2024/06/01
matches
a1 b2 c3
a#b2
a#b#
smith, john
```

`match` fails when there is no match, so it reads as a condition. Captures go to
`M` — [0004](adr/0004-regex-captures-go-to-M.md). The same engine backs `=~`
inside `[[ ]]`, which fills bash's `BASH_REMATCH` as well.

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
echo "${cfg[name]}"
echo "${cfg[tags][0]}"
echo "${cfg[meta][stars]}"
json get cfg .meta.stars n;  echo "$n"
json get cfg .tags all;      echo "$all"
n := json get cfg .meta.stars; echo "$n"
json keys cfg . top;         echo "$top"
json keys cfg .meta mk;      echo "$mk"
json len  cfg .tags
json type cfg .meta.ok
json type cfg .tags
json set cfg .meta.stars 43
json set cfg .meta.note 007 -s
json emit cfg
```

```output
hibr
shell
42
42
["shell","c"]
42
name tags meta
stars ok
2
boolean
array
{"name":"hibr","tags":["shell","c"],"meta":{"stars":43,"ok":true,"note":"007"}}
```

Types survive the round trip: a number stays a number, `true` stays a boolean,
`null` stays null. That is what `-s` is for — without it `007` would come back
out as `7`.

**One sharp edge.** A quoted subscript is a literal key, so a field like
`content-type` is reachable:

```sh
json parse h '{"content-type":"application/json"}'
echo "[${h["content-type"]}]"
echo "[${h[content-type]}]"
```

```output
[application/json]
[]
```

The second is empty: without quotes the key is read as arithmetic, `content`
minus `type`, and lands on key 0.

See [0006](adr/0006-arrays-are-sparse-maps.md).

## Worked examples

### Parse a log line into fields

<!-- setup
printf '%s\n' '2024-06-01T10:00:00Z INFO  started' '2024-06-01T10:00:05Z ERROR disk full' '2024-06-01T10:00:30Z WARN  slow request' '2024-06-01T10:01:00Z ERROR timeout' > app.log
-->
```sh
while read -r line; do
  match "$line" '^([0-9-]+)T([0-9:]+)Z +([A-Z]+) +(.*)$' || continue
  [ "${M[3]}" = ERROR ] || continue
  echo "${M[1]} ${M[2]}: ${M[4]}"
done < app.log
```

```output
2024-06-01 10:00:05: disk full
2024-06-01 10:01:00: timeout
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
echo "?$qs"
```

```output
?user=omar&limit=10
```

### Turn a CSV column into a sorted, unique list

<!-- setup
printf '%s\n' ada,1815,london omar,1990,cairo lin,1970,london sam,1985,berlin > people.csv
-->
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

```output
berlin
cairo
london
```

---

[← documentation index](README.md)
