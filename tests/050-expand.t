x=hello
e=
u=
unset u
echo "${x}-${x:-d}-${u:-d}-${e:-d}-${e-d}"
echo "${#x} ${x#he} ${x%lo} ${x##*l} ${x%%l*}"
p=/a/b/c.tar.gz
echo "${p##*/} ${p%%.*} ${p#/} ${p%/*}"
echo "${u:+set} ${x:+set}"
a="one two  three"
for w in $a; do echo "[$w]"; done
for w in "$a"; do echo "q[$w]"; done
set -- p q r
echo "$# $* $@ $1"
n=0
echo $((1+2*3)) $(( (1+2)*3 )) $((10%4)) $((-5+2)) $((2>1)) $((n||1))
echo $(echo nested $(echo deeper))
