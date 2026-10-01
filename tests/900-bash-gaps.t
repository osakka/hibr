# Bash differences found while checking the documentation for 0.69.
x=(z); printf 'a\nb\n' | { mapfile -t -O 1 x; echo "mapfile -O 1: ${!x[*]} / ${x[*]}"; }
x=(z); printf 'a\nb\n' | { mapfile -t -O 5 x; echo "mapfile -O 5: ${!x[*]} / ${x[*]}"; }
x=(z y w); printf 'a\n' | { mapfile -t -O 0 x; echo "mapfile -O 0 keeps the rest: ${x[*]}"; }
x=(z y w); printf 'a\n' | { mapfile -t x; echo "mapfile alone clears: ${x[*]}"; }
exec 3< <(printf 'q\nr\n'); read -u 3 a; read -u 3 b; echo "exec on a process substitution: [$a][$b]"
exec 3<&-
cat <(echo one) <(echo two)
( echo $(( 1 / 0 )); echo never ) 2>/dev/null; echo "an arithmetic error ends the script: $?"
( f() { echo $(( 1 / 0 )); echo never; }; f; echo never ) 2>/dev/null; echo "inside a function too: $?"
( a=(1 2); echo ${a[1/0]}; echo never ) 2>/dev/null; echo "in a subscript too: $?"
( (( 1 / 0 )); echo "(( )) only fails: $?" ) 2>/dev/null
( let "1/0"; echo "let only fails: $?" ) 2>/dev/null
kill -l | head -6
kill -l 9 15 SIGTERM hup 137
kill -L KILL
kill -l 99 2>/dev/null; echo "unknown: $?"
sleep 5 & p=$!; kill -s TERM $p; wait $p; echo "kill -s: $?"
sleep 5 & p=$!; kill -n 9 $p 2>/dev/null; wait $p 2>/dev/null; echo "kill -n: $?"
