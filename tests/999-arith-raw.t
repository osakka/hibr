# Arithmetic with nothing to expand is evaluated as it stands, without
# being lexed and expanded again on every evaluation (Gitea #443): the
# answers must be the ones the slow path gave. Compared against bash,
# including the empty expression, which is 0 there and was an error here.
echo "[$(( ))] [$((  ))]"
(( ))
echo "empty test: $?"
a=5 b=3
echo $((a<=b)) $((a>=b)) $((a==b)) $((a!=b))
((x = a == 5)); echo "x=$x"
((a <<= 1)); echo "a=$a"
((a >>= 2)); echo "a=$a"
((c = b <= 3 ? 7 : 8)); echo "c=$c"
((d += 2, d *= 3)); echo "d=$d"
i=0; echo $((i++ + ++i)) $i
m[2]=4; k=2; ((m[k] += 1)); echo "${m[2]}"
echo $(( 2#101 + 0x1f + 010 ))
for ((j = 0; j < 3; j++)); do printf %s "$j"; done; echo
n=0; while ((n < 5)); do ((n++)); done; echo "n=$n"
