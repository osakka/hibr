# A quoted subscript is a literal key inside $(( )), (( )) and for (( )) as
# well, as it is everywhere else (ADR 0006): the quotes survive expansion as
# far as the evaluator, and only inside a subscript.
m["content-type"]=7
echo "read:     $(( m["content-type"] + 1 ))"
(( m["x-y"] = 5 ))
echo "assigned: ${m["x-y"]}"
(( m["x-y"]++, m["x-y"] *= 2 ))
echo "updated:  ${m["x-y"]}"
k="a b"; m["a b"]=9
echo "a quoted variable as the key: $(( m["$k"] * 2 ))"
q["it's"]=6
echo "a key with an apostrophe: $(( q["it's"] ))"
n["o"]["p"]=4
echo "nested:   $(( n["o"]["p"] ** 2 ))"
for (( m["c-d"] = 0; m["c-d"] < 3; m["c-d"]++ )); do :; done
echo "for loop: ${m["c-d"]}"
a=(10 20 30); i=2
echo "unquoted stays arithmetic: $(( a[i] + a[i-1] ))"
echo "quotes outside a subscript are removed, as in bash: $(( "1" + 2 ))"
let 'm["content-type"] += 1'
echo "let:      ${m["content-type"]}"
declare -A h; h[a-b]=3
echo "declare -A needs no quotes: $(( h[a-b] + 1 ))"
