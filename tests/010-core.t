echo hello world
x=5
y="quoted $x"
echo "$y" and $y
echo ${x:-def} ${nope:-fallback} ${nope:=set} ${nope}
echo len=${#y}
f=/tmp/a/b/c.txt
echo ${f##*/} ${f%.txt} ${f#/tmp/}
echo sum=$((x * 3 + 2))
echo sub=$(echo nested $(echo deep))
if [ "$x" -gt 3 ]; then echo big; else echo small; fi
i=0
while [ $i -lt 3 ]; do echo "i=$i"; i=$((i+1)); done
for w in a b c; do echo w:$w; done
greet() { echo "hi $1 from $0"; return 7; }
greet omar
echo status=$?
echo one two three | tr ' ' '\n' | sort -r
echo pipeline=$?
{ echo grouped; echo again; } > /tmp/out.txt
cat /tmp/out.txt
( cd /tmp && pwd )
pwd
true && echo and-ok || echo never
! false && echo not-ok
echo 'single $x' "double $x"
