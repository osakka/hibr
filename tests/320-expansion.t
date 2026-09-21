echo {a,b,c}
echo pre{1,2}post
echo {1..5}
echo {01..04}
echo {a..e}
echo {1..9..3}
echo {5..1}
echo file.{c,h}
echo {a,b}{x,y}
echo "{quoted,nope}"
echo {single}
echo {}
printf '%s|' $'tab\there' $'\x41\x42' $'oct\101' $'esc\e[0m' ; echo
d=/tmp/hibr-glob-test
rm -rf $d; mkdir -p $d/a/b/c $d/x
touch $d/top.c $d/a/one.c $d/a/b/two.c $d/a/b/c/three.c $d/x/four.txt
cd $d
echo *.c
echo **/*.c
for f in **/*.c; do echo "each $f"; done
echo a/**/*.c
cd /
rm -rf $d
