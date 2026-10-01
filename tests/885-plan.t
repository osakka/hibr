# hibr --plan follows a script's own logic but refuses every write outside a
# scratch TMPDIR of its own, every connection, and every program not known to
# only read, and records each one. The records are half the test; the other
# half is that nothing the script names was created.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
d=$(mktemp -d)
cd "$d"
norm() { sed -E 's/tmp\.[A-Za-z0-9]+/tmp.X/g'; }

printf 'one line\n' > in.txt
cat > p.sh <<'EOF'
echo hello > out1
echo more >> out2
echo both &> out3
exec {fd}> out4
>| out6
while read -r l; do echo "$l"; done < ../in.txt > out7
exec 3<>/dev/tcp/127.0.0.1/9
tmp=$(mktemp)
echo kept > "$tmp"
echo "a temp file is real: $(cat "$tmp")"
mkdir "$tmp.d" && touch "$tmp.d/f" && echo "file tools inside it run"
echo x > "$TMPDIR/../outside"
n=$(grep -c . ../in.txt); echo "a reading program runs: $n line"
rm -rf never
curl -s https://example.com > page || echo "a refused program fails: $?"
sed -i s/a/b/ out1
sed s/a/b/ ../in.txt > /dev/null && echo "sed without -i runs"
awk '{print}' ../in.txt
timeout 5 grep x ../in.txt
env rm -rf never
env > /dev/null && echo "env with no program runs"
date -s now
cat < /dev/tcp/127.0.0.1/9
find . -name never -delete
git commit -m x
kill -0 $$ && echo "kill -0 runs"
kill 1
listen 9999 f
mod load sys
need json
exec rm -rf never
exec 2>/dev/null
echo "the record survives exec 2>/dev/null" >&2
exec 250>&-
rm -rf also-never
trap 'rm -rf "$tmp" stays' EXIT
EOF
mkdir w
cd w
"$h" --plan ../p.sh > ../out.txt 2>&1
st=$?
cd ..
norm < out.txt
echo "status $st"
echo "--- nothing was made:"
ls -A w
echo "--- agent mode:"
"$h" --plan --agent -c 'x=$(curl -s a.b); (rm -f z); touch y | cat; echo done' 2>&1
echo "--- a plan stops where it needed a result it could not have:"
"$h" --plan -c 'set -e; curl -sO https://x/y.tgz; echo never' 2>&1
echo "status $?"
echo "--- the scratch TMPDIR is gone:"
"$h" --plan -c 'echo "$TMPDIR"' > scratch.txt
[ -e "$(cat scratch.txt)" ] && echo "still there" || echo "removed"

cd /
rm -rf "$d"
