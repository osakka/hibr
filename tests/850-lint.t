# hibr --explain: parse a script, run nothing, and name each mistake a model
# (or a person) commonly makes in it. Every rule has a case that fires and a
# case beside it that must not.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
export HIBR_MODPATH=$PWD/build/mods
root=$PWD
d=$(mktemp -d)
cd "$d"

cat > bad.sh <<'EOF'
f="my file"
rm $f
rm "$f"
for x in $(ls); do echo "$x"; done
for x in *; do echo "$x"; done
cd /tmp
cd /tmp || exit
cd /
[ $x = y ] && echo yes
[ "$x" = y ] && echo yes
[ $# -eq 0 ] && [ ${#f} -gt 2 ]
n=0
[ $n -lt 3 ]
up := uname
up := str upper x
unset 'm[$k]'
unset m["$k"]
g() {
	local a=$1 b=${m[$a]}
	local c=$1
	local e=${m[$c]}
	local out=$(false)
	echo "$?"
	ret "$b"
	return 1
}
h() {
	ret "$1"
	return 0
}
m[0]=x
echo "${m[row]}" "${m["row"]}"
declare -A s
echo "${s[row]}" "${other[row]}"
EOF
"$h" --explain bad.sh 2>&1 | sed 's/^hibr: [^:]*:/hibr: bad.sh:/'
echo "status $?"
"$h" --explain bad.sh > /dev/null 2>&1
echo "findings status $?"

echo "--- agent mode"
"$h" --agent --explain bad.sh 2>&1 | head -2
echo "--- clean"
printf 'x=1\necho "$x"\n' > good.sh
"$h" --explain good.sh
echo "status $?"
echo "--- nothing runs"
"$h" --explain -c 'echo ran; rm $f'
echo "status $?"
echo "--- a syntax error is status 2"
"$h" --explain -c 'if true; then' 2>&1
echo "status $?"
echo "--- standard input"
echo 'rm $f' | "$h" --explain 2>&1

echo "--- hibr's own examples are clean"
n=0
for f in $(find "$root/examples" -name '*.hibr'); do
  n=$((n + 1))
  "$h" --explain "$f" || echo "findings in ${f#"$root"/}"
done
[ "$n" -gt 60 ] && echo "examples linted"

cd /
rm -rf "$d"
