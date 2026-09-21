# Cookbook

Whole tasks, solved. Every script here was run and its output pasted back
underneath it. Almost none of them start a process.

- [Text without the toolchain](#text-without-the-toolchain)
- [Structured data](#structured-data) · [Concurrency](#concurrency)
- [Network](#network) · [Command-line programs](#command-line-programs)
- [Discipline](#discipline)

## Text without the toolchain

### Count words, without `sort | uniq -c`

Arrays are maps, so the counter *is* the data structure.

```sh
count=()
for w in the quick brown fox the lazy dog the end; do
  count[$w]=$(( ${count[$w]:-0} + 1 ))
done
for w in "${!count[@]}"; do
  printf '%s %s\n' "$(str pad "$w" -6)" "${count[$w]}"
done
```

```
   the 3
 quick 1
 brown 1
   fox 1
  lazy 1
   dog 1
   end 1
```

Insertion order is kept, which `sort | uniq -c` cannot do at all.

### Pull fields out of a log

```sh
while read -r line; do
  match "$line" '^([0-9-]+)T([0-9:]+)Z +([A-Z]+) +(.*)$' || continue
  [ "${M[3]}" = ERROR ] || continue
  echo "${M[1]} ${M[2]}: ${M[4]}"
done < app.log
```

```
2024-06-01 10:00:05: disk full
2024-06-01 10:01:00: timeout
```

Four tools replaced — `grep`, `awk`, `cut`, `sed` — and no process started for a
file of any length.

### Lay out a table, measuring the columns first

```sh
rows=("name:role:years" "omar:founder:12" "ada:engineer:7")
widths=(0 0 0)
for r in "${rows[@]}"; do
  str split "$r" : c
  for i in 0 1 2; do
    str len "${c[$i]}" n
    [ "$n" -gt "${widths[$i]}" ] && widths[$i]=$n
  done
done
for r in "${rows[@]}"; do
  str split "$r" : c
  line=""
  for i in 0 1 2; do
    str pad "${c[$i]}" "${widths[$i]}" " " cell
    line="$line$cell  "
  done
  str trim "$line" line
  echo "$line"
done
```

```
name  role      years
omar  founder   12
ada   engineer  7
```

## Structured data

### Read an INI file into nested maps

One pass, no temporary files, and the result is addressable by section and key.

```sh
cfg=()
section=main
while read -r line; do
  str trim "$line" line
  [ -z "$line" ] && continue
  case "$line" in \#*) continue ;; esac
  if match "$line" '^\[(.+)\]$'; then section=${M[1]}; continue; fi
  match "$line" '^([^=]+)=(.*)$' || continue
  str trim "${M[1]}" k
  str trim "${M[2]}" v
  cfg[$section][$k]=$v
done < app.ini

echo "sections: ${!cfg[@]}"
echo "host: ${cfg[server][host]}  port: ${cfg[server][port]}"
```

```
sections: server auth
host: example.com  port: 8080
```

`cfg[$section][$k]=$v` builds the intermediate level as it goes. No other shell
has the container to hold this.

### Build JSON from shell data

```sh
json parse out '{}'
json set out .service hibr -s
json set out .port 8080
json set out .tls true
json set out .tags[0] shell -s
json set out .tags[1] fast -s
json emit out -p
```

```json
{
  "service": "hibr",
  "port": 8080,
  "tls": true,
  "tags": [
    "shell",
    "fast"
  ]
}
```

Types are kept: `8080` emits as a number, `true` as a boolean, and `-s` keeps
`hibr` a string. Without `-s` a value like `007` would come back out as `7`.

### Query a document you have parsed

```sh
json parse cfg "$body"
echo "${cfg[meta][stars]}"        # an ordinary subscript reaches in
json get cfg .tags all            # or a path, giving JSON back
n := json get cfg .meta.stars     # or straight into the result slot
```

## Concurrency

### Two worker processes, spoken to like sockets

```sh
square() { while recv 0 n; do send 1 $(( n * n )); done; }
coproc w1 square
coproc w2 square
for n in 3 4; do send ${w1[out]} $n; recv ${w1[in]} r; echo "w1: $n -> $r"; done
for n in 5 6; do send ${w2[out]} $n; recv ${w2[in]} r; echo "w2: $n -> $r"; done
```

```
w1: 3 -> 9
w1: 4 -> 16
w2: 5 -> 25
w2: 6 -> 36
```

bash cannot keep two coprocesses at once — it warns and loses the first. See
[0018](adr/0018-a-coprocess-is-an-endpoint.md).

### Wait for whichever finishes first

```sh
slow & fast &
wait -n -p who
echo "job $who finished first, status $?"
```

## Network

### A daemon that binds a privileged port and then stops being root

```sh
mod load sys
listen -b 80 LFD          # needs root, because the port is below 1024
drop www-data             # irreversible: real, effective and saved ids
while accept $LFD C; do
  serve <&$C >&$C
  exec {C}<&-
done
```

Only opening the port needed privilege, and the descriptor outlives it. See
[0016](adr/0016-privileges-are-dropped-never-gained.md).

### An HTTPS request, parsed, with nothing external

```sh
exec 3<>/dev/tls/api.example.com/443
send -r 3 "GET /v1/items HTTP/1.0"
send -r 3 "Host: api.example.com"
send -r 3 ""
recv 3 status
[[ $status =~ ^HTTP/1\.[01]\ ([0-9]{3}) ]] && echo "HTTP ${M[1]}"
while recv 3 h; do [ -z "$h" ] && break; done
recv -a 3 body
json parse api "$body"
```

No `curl`, no `jq`, no pipe. Certificates are verified by default —
[0010](adr/0010-tls-verifies-and-is-dlopened.md).

## Command-line programs

### Declare the options instead of parsing them

```sh
opt .  "Summarise a source tree"
opt -d --dir   dir  path=.   "Directory to inspect"
opt -n --top   top  int=3    "How many largest files to show"
opt -j --json  json          "Emit the summary as JSON"
args "$@"

echo "looking at $dir, showing $top"
```

`--help` is generated from the declarations, types are checked, and `!` marks an
option required, `+` repeatable. Compare the twenty lines of `case` this
replaces.

### Return a value from a function without forking

```sh
fn add(int a, int b) -> int { ret $((a + b)); }
sum := add 2 3
echo "$sum"          # 5
```

`$(add 2 3)` would fork. `:=` does not — [0005](adr/0005-results-travel-in-a-slot.md).

## Discipline

### Retry with a growing delay

```sh
delay=1
for try in 1 2 3 4 5; do
  if ok := attempt $try; [ "$ok" = 0 ]; then
    echo "succeeded on attempt $try"
    break
  fi
  echo "attempt $try failed, waiting ${delay}s"
  delay=$(( delay * 2 ))
done
```

```
attempt 1 failed, waiting 1s
attempt 2 failed, waiting 2s
succeeded on attempt 3
```

### Catch a failure instead of dying on it

```sh
try risky_thing --now
if [ "$ERR" -ne 0 ]; then
  echo "failed: $ERRMSG (status $ERRSTATUS)"
fi
```

`try` catches; `fail msg` raises. `set -e` is scoped to the tested pipeline and
does not reach into functions it calls —
[0002](adr/0002-errexit-is-scoped.md).

### Clean up whatever happens

```sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
```

The trap fires in a subshell, a command substitution, a background job and a
pipeline element too — and an *inherited* one does not fire twice.

---

[← documentation index](README.md)
