x=1

case $LINENO in *) echo "case $LINENO";; esac
for i in $LINENO; do echo "for $i"; done
[[ $LINENO == 5 ]] && echo "cond 5" || echo "cond not 5"
while [ $LINENO ]; do echo "while $LINENO"; break; done
if [ $LINENO = 7 ]; then echo "if 7"; fi
f() {
	echo "in f $LINENO"
	for j in $LINENO; do echo "for in f $j"; done
}
f
echo "after $LINENO"
