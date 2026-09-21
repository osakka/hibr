c=0
for i in 1 2 3 4 5; do
  if [ $i -eq 3 ]; then continue; fi
  if [ $i -eq 5 ]; then break; fi
  c=$((c+i))
done
echo c=$c
fact() { if [ $1 -le 1 ]; then echo 1; else echo $(( $1 * $(fact $(($1-1))) )); fi; }
echo "fact 6 = $(fact 6)"
v=abc
case_test=${v:+present}
echo $case_test ${missing:+never}
echo "nested: ${v#a}${v%c}"
u=UNSET
unset u
echo "[${u-gone}]"
x=1
x=2 true
echo x=$x
a() { echo "in a"; b; }
b() { echo "in b, depth ok"; }
a
echo "exit code of false: "; false; echo $?
