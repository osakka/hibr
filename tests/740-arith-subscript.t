# Arithmetic reaches a subscript, and a nested one, for reading and writing.
# Compared against bash: all of this is bash behaviour that hibr lacked.

a=(5 6 7)
i=1
echo "read $(( a[i] + a[2] ))"
echo "expr $(( a[i + 1] ))"
(( a[0] = 42 )); echo "assign ${a[0]}"
(( a[1] += 5 )); echo "plus ${a[1]}"
(( a[2]++ )); echo "post ${a[2]}"
(( ++a[0] )); echo "pre ${a[0]}"
(( a[i] = a[0] * 2 )); echo "both ${a[1]}"
echo "missing $(( a[9] + 1 ))"
echo "tern $(( a[0] > a[1] ? a[0] : a[1] ))"

n=3
while [ $n -gt 0 ]; do
  (( a[n] = n * n ))
  n=$((n - 1))
done
echo "loop ${a[1]} ${a[2]} ${a[3]}"

declare -A m
m[x]=2
(( m[x] *= 3 ))
echo "named ${m[x]}"
