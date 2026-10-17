# declare -p and set print what reads back in, compared against bash
# (Gitea #269): a value quoted as bash quotes it, in both styles, an
# indexed array's contents, and a one-key -A map, whose trailing space
# before the parenthesis is bash's own. hibr printed `x="q"$z"`, `(...)`
# for every array, and nothing at all for a bare `set`.
a=(1 "two words" '$x' 'q"z' $'t\tab')
a[7]=seven
declare -A m=([k]=v)
x=$'a\nb\tc'
y="q\"\$\`\\z"
z='it'"'"'s'
e=
w=plain
k=a=b
declare -i n=3
declare -rx R=1
declare -n r=x
declare -p a m x y z e w k n R r
set | grep -E '^(a|m|x|y|z|e|w|k|n|R|r)='
myfn() { echo hi; }
set | grep -c '^myfn'
f=$(mktemp)
set -o errexit
set +o > "$f"
set +o errexit
for o in errexit nounset xtrace noclobber; do
	grep -E "^set [-+]o $o\$" "$f"
done
rm -f "$f"
declare -- q2=1
echo "q2=$q2"
eval "$(declare -p a y z)"
declare -p a y z
