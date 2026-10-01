# From bash to hibr, in ten minutes

You already write bash. This page takes you from installing hibr to the
handful of things it does that bash cannot, one short example each. Every
example below is run on every build and its output compared with what is
printed here, so what you read is what hibr does.

## Install

On Debian 12, Ubuntu 22.04 or newer (amd64), from hibr's own apt repository:

```text
curl -fsSL https://osakka.github.io/hibr-apt/hibr.gpg \
  | sudo tee /usr/share/keyrings/hibr.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/hibr.gpg] https://osakka.github.io/hibr-apt stable main" \
  | sudo tee /etc/apt/sources.list.d/hibr.list
sudo apt update && sudo apt install hibr
```

On macOS or Linux with Homebrew:

```text
brew tap osakka/hibr
brew install hibr
```

From the source, with a C compiler (tcc by default, or `make CC=gcc`):

```text
git clone https://github.com/osakka/hibr
cd hibr
make
make install            # /usr/local; PREFIX=... for elsewhere
```

`hibr` with no arguments is an interactive shell with line editing, history
and completion; `hibr script` runs a script; `man hibr` lists every flag.

## Your bash scripts already run

hibr runs ordinary bash. Point it at a script you have:

```sh
cat > greet.sh <<'EOF'
#!/bin/bash
name=${1:-world}
for i in 1 2; do
  echo "hello, $name ($i)"
done
EOF
"$HIBR" greet.sh omar
```

```output
hello, omar (1)
hello, omar (2)
```

(`$HIBR` is the running shell's own path; at a prompt you would type
`hibr greet.sh omar`.)

## Three things that differ

hibr differs from bash in a few places, on purpose. Three of them are worth
knowing before anything else.

**A quoted subscript is a literal key.** An unquoted one is still arithmetic,
as in an ordinary bash array, so a key with a dash in it needs quotes:

```sh
h["content-type"]=text/plain
h[a-b]=oops
echo "${!h[@]}"
```

```output
content-type 0
```

`h[a-b]` was `a` minus `b`, which is key 0. Arrays made with `declare -A`
take every subscript literally, as in bash.

**`set -e` does not switch off inside a function you call from a
condition.** In bash, `if f; then` quietly stops `set -e` checking anything
inside `f`; in hibr a failure in there still stops the script. And a
pipeline fails if any part of it does, so there is no `pipefail` to remember.

**Regex captures land in `M`**, and `[[ =~ ]]` fills bash's `BASH_REMATCH`
too, so bash scripts find them where they look. Every deliberate difference
has a page of its own in [the decisions](adr/README.md).

## Maps that nest

An array is an ordered map, and maps nest without any new syntax:

```sh
users[omar][role]=admin
users[omar][shell]=hibr
users[ana][role]=dev
for u in "${!users[@]}"; do
  echo "$u is ${users[$u][role]}"
done
echo "omar has ${#users[omar][@]} fields"
```

```output
omar is admin
ana is dev
omar has 2 fields
```

Keys come back in the order they were added.

## Functions that return values

`fn` declares parameters, with types if you want them checked. `ret` hands
a value back and `:=` catches it -- no `$( )`, no subshell, no fork:

```sh
fn area(int w, int h) { ret $((w * h)); }
a := area 6 7
echo "area $a"
area 6 seven 2>&1 || echo "refused: status $?"
```

```output
area 42
hibr: area: h expects int, got 'seven'
refused: status 2
```

A call with a wrong type, a missing argument or one too many is refused
before the body runs.

## JSON without jq

A parsed document is an ordinary nested map, and its types survive the
trip back out:

```sh
json parse cfg '{"name":"hibr","ports":[80,443],"tls":true}'
echo "${cfg[name]} listens on ${cfg[ports][1]}"
json type cfg .tls
json set cfg .ports[2] 8080
json emit cfg
```

```output
hibr listens on 443
boolean
{"name":"hibr","ports":[80,443,8080],"tls":true}
```

## Regular expressions with captures

```sh
line="2026-10-01 ERROR disk full"
if match "$line" '^([0-9-]+) ([A-Z]+) (.*)$'; then
  echo "${M[2]} on ${M[1]}: ${M[3]}"
fi
```

```output
ERROR on 2026-10-01: disk full
```

## Command-line options without getopts

Declare the options; `args` parses them, checks their types, and builds
`--help` from the declarations:

```sh
fn main(...argv) {
  opt . "Copy some files somewhere"
  opt -n --dry-run dry "Only say what would happen"
  opt -t --to dest str=out "Where to copy them"
  args "${argv[@]}" || return
  echo "dry=$dry dest=$dest files=${ARGS[*]}"
}
main -n --to backup a.txt b.txt
```

```output
dry=1 dest=backup files=a.txt b.txt
```

## Failing, and catching it

`fail` stops a function with a message; `try` runs something with failure
caught, and leaves the message in `$ERRMSG`:

```sh
fn fetch(str url) {
  [[ $url == https://* ]] || fail "not https: $url"
  echo "fetching $url"
}
try fetch http://example.com
[ "$ERR" -ne 0 ] && echo "caught: $ERRMSG"
try fetch https://example.com
```

```output
caught: not https: http://example.com
fetching https://example.com
```

## Check a script before you run it

`--explain` reads a script and names the common mistakes in it; `--plan`
follows it without changing anything and lists what it would have done:

```sh
printf 'cd build\nrm $out\n' > job.sh
"$HIBR" --explain job.sh 2>&1
printf 'echo built > out.txt\nrm -rf build\n' > tidy.sh
"$HIBR" --plan tidy.sh 2>&1
```

```output
hibr: job.sh:1: cd-unchecked: cd can fail, and then everything after it runs in the wrong directory; write cd ... || exit
hibr: job.sh:2: unquoted-path: rm is handed $out unquoted: a blank or a * in its value makes more paths than meant; write "$out"
hibr: plan: tidy.sh:1: would write out.txt
hibr: plan: tidy.sh:2: would run rm -rf build
```

## Where next

| | |
|---|---|
| [The language](language.md) | everything above in full, and every place hibr differs from bash |
| [Builtins](builtins.md) | each builtin, what it takes and what it gives back |
| [Text, regex and JSON](data.md) | `str`, `arr`, `match`, `rsub` and `json`, operation by operation |
| [Cookbook](cookbook.md) | whole tasks, solved |
| [Networking](networking.md) | sockets, TLS and servers, with no `nc` or `curl` |
| [hibr for a language model](llm.md) | all of it on one page, for a model to read |

---

[← documentation index](README.md)
