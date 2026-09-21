i=1
echo "assign: $((i+=5)) i=$i"
echo "post/pre: $((i++)) $i $((++i)) $i $((i--)) $((--i)) $i"
echo "ops: $((7%3)) $((2**10)) $((2**3**2)) $((1<<4)) $((~0)) $((!5))"
echo "ternary: $((1?10:20)) $((0?10:20)) $((i>3 ? 1 : 0))"
echo "comma: $((a=3, b=4, a*b)) a=$a b=$b"
x=0; echo "short and: $(( 0 && (x=9) )) x=$x"
echo "short or: $(( 1 || (x=9) )) x=$x"
echo "no div error when skipped: $(( 0 && 1/0 ))"
echo "compound: $((c=10, c*=3, c-=5, c/=5, c)) $((d=7, d%=4, d)) $((e=1, e<<=3, e))"
((i=10)); ((i++)); echo "statement: i=$i"
((i > 5)) && echo "true status"
((0)); echo "zero status=$?"
((1)); echo "nonzero status=$?"
let j=6*7 k=j+1; echo "let: j=$j k=$k"
let 0; echo "let zero status=$?"
for ((n=0; n<4; n++)); do printf "%s " $n; done; echo
for ((n=10; n>0; n-=3)) { printf "%s " $n; }; echo
for ((;;)); do m=$((m+1)); ((m>=3)) && break; done; echo "infinite with break: m=$m"
for ((p=0; p<5; p++)); do ((p==2)) && continue; printf "%s" $p; done; echo
set -e
z=0; ((z++)); echo "((0++)) does not trip set -e"
set +e
( echo "never printed $(( 1/0 ))" ) 2>/dev/null; echo "div by zero aborts the command, status=$?"
((2/0)) 2>/dev/null; echo "statement error status=$?"
v=$(( 7 ))x; echo "expansion still fine after an error: $v"
s=ab; s+=cd; s+=ef; echo "string +=: $s"
arr=(1); arr+=(2 3); arr+=(4); echo "array +=: ${arr[*]} n=${#arr[@]}"
m[k]=x; m[k]+=y; echo "element +=: ${m[k]}"
sp=(a b); sp[7]=h; sp+=(z); echo "append after sparse: ${!sp[*]}"
fn f() { local l=(1 2); l+=(3); ret ${#l[@]}; }; r := f; echo "local +=: $r"
echo "wraps like bash: $(( (-9223372036854775807 - 1) / -1 )) $((2**63)) $((2**64)) $((9223372036854775807 + 1))"
echo "shift masked: $((1 << 70))"
