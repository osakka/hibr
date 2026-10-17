# What declare -p, set, set +o and shopt -p print reads back in, for the
# shapes bash has no syntax for (Gitea #269). Recorded: nested maps, the
# one-namespace option listing and the bound slot are hibr's own.
#
# A map holding maps is the declaration and then one assignment a line; an
# empty map inside one has no assignment and is left out, which the json
# case shows. Every shape goes through `eval` in this shell and through a
# fresh one, since a listing that only reads back into the shell that
# printed it could be leaning on what that shell already holds.
declare -A m=([k]=v ["a b"]='q"$z' [c-d]=$'l1\nl2' [e]=)
declare -a s=()
s["word"]=x
declare -A n=()
n["a"]["b"]="v"
n["a"]["c"]["d"]="deep spaced"
n["z"]=1
json parse j <<< '{"a":{"b":"v"},"e":{},"z":"1"}'
declare -x int t=3
declare -p m s n j t
echo "== a fresh shell reads it back"
dump=$(declare -p m s n j t)
"$HIBR" -c "$dump
declare -p m s n j t" > /tmp/hibr-213-$$.out 2>&1
[ "$dump" = "$(< /tmp/hibr-213-$$.out)" ] && echo same || cat /tmp/hibr-213-$$.out
rm -f /tmp/hibr-213-$$.out
echo "== so does this one"
unset m n
eval "$dump"
echo "${m["a b"]} ${n["a"]["c"]["d"]} ${#m[@]}"
echo "== sorted by name"
vqz=1 vab=1 vmm=1 vzz=1 vaa=1 vkk=1
declare -p | grep -E '^declare -- v(qz|ab|mm|zz|aa|kk)='
export vexb=1 vexa=1
export -p | grep -E '^declare -x vex'
echo "== -- ends the options"
declare -- dd=1
readonly -- rr=2
echo "$dd $rr"
readonly -p | grep ' rr='
echo "== shopt -p and set +o restore the options"
shopt -s nullglob
shopt -p nullglob extglob dotglob
saved=$(shopt -p)
shopt -u nullglob
eval "$saved"
shopt nullglob
shopt -p nosuch; echo "st=$?"
set -o noclobber
saved=$(set +o)
set +o noclobber
eval "$saved"
set -o | grep noclobber
set +o noclobber
"$HIBR" +o < /dev/null | grep -E '^set [-+]o noclobber$'
echo "== a bound slot takes the listing and prints nothing"
x := declare -p t
echo "[$x]"
x := export -p
case $x in *"vexa"*) echo "export bound" ;; esac
x := set
case $x in *"vzz=1"*) echo "set bound" ;; esac
x := shopt -p nullglob
echo "[$x]"
x := set +o
case $x in *"set +o xtrace"*) echo "set +o bound" ;; esac
f1() { :; }
x := declare -f f1
echo "[$x]"
