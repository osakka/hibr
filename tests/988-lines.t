# lines: show, grep, count and edit, the module the agent working on hibr
# uses instead of sed -n, grep -rn and a python replace (Gitea #67).
# Recorded: the module is hibr's own.

mod load ./build/mods/lines.so || exit 1
d=$(mktemp -d)
cd "$d" || exit 1
printf 'one\ntwo\nthree\nfour\nfive\n' > a.txt
printf 'no newline at the end' > b.txt
mkdir -p sub/.hidden
printf 'alpha\nTWO beta\n' > sub/c.txt
printf 'two\n' > sub/.hidden/skip.txt
printf 'x\0two\n' > bin.dat

echo "== show"
lines show a.txt 2 3
lines show -n a.txt 4 '$'
lines show b.txt 1; echo
lines show a.txt 3 2; echo "backwards: $?"

echo "== grep"
lines grep two a.txt sub bin.dat
lines grep -i two a.txt sub
lines grep -C 1 three a.txt
lines grep -A 1 -B 1 'o$' a.txt
lines grep -l -i two .
lines grep -g '*.txt' -F beta .
lines grep nothing a.txt; echo "no match: $?"
lines grep '(' a.txt; echo "bad pattern: $?"

echo "== count"
lines count a.txt o
lines count a.txt zzz

echo "== edit"
lines edit a.txt << 'X'
<<<<
two
three
====
2 and 3
>>>>

<<<<
five
====
5
>>>>
X
cat a.txt
lines edit a.txt << 'X'
<<<<
5
====
five
>>>>
<<<<
o
====
0
>>>>
X
echo "ambiguous, and the first block not written: $?"
cat a.txt
lines edit -n a.txt << 'X'
<<<<
four
====
4
>>>>
X
lines edit a.txt << 'X'
<<<<
four

====
>>>>
X
cat a.txt
lines edit a.txt << 'X'
stray
X
echo "outside a block: $?"
lines edit a.txt << 'X'
<<<<
one
X
echo "unended: $?"
ls

cd / && rm -rf "$d"
