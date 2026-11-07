# A second operand gives up the rest of its line with status 1, a number too
# big for a long wraps, and a value holding a dollar is no expression.
f() { shift 1 2; echo in; }
f a b c; echo same
echo "next $?"
exit 3 4; echo same
echo "next $?"
for i in 1; do break 1 2; echo in; done; echo same
echo "next $?"
for i in 1; do continue 1 2; echo in; done; echo same
echo "next $?"
g() { return 3 4; echo in; }
g; echo same
echo "next $?"
shift 1 2; echo same
echo "next $?"
read -t 1e3 x < /dev/null 2> /dev/null; echo "read $?"
read -t 0x10 x < /dev/null 2> /dev/null; echo "read $?"
read -t 0.5 x < /dev/null; echo "read $?"
ulimit -f 9223372036854775807 2> /dev/null; echo "ulimit $?"
echo $((9223372036854775808)) $((18446744073709551616)) $((0xffffffffffffffff))
n=9223372036854775808; echo $((n))
[[ 9223372036854775808 -gt 0 ]]; echo "big $?"
y=5
x='$y'; [[ $x -eq 5 ]] 2> /dev/null; echo "dollar $?"
x='$((1))'; [[ $x -eq 1 ]] 2> /dev/null; echo "dollar $?"
(echo $((x))) 2> /dev/null; echo "sub $?"
i=2; a=(1 2 3); echo $((a[$i] + $i)) ${a[$i - 1]}
(exit 4) & p=$!; (exit 5) & q=$!; wait -p v $q $p; echo "$? $([ "$v" = "$p" ] && echo same)"
sleep 0.05 & p=$!; wait -p v $p; echo "$? $([ "$v" = "$p" ] && echo same)"
cat <(exit 4); wait $!; echo "psub $?"
exit 3 4
exit 7
