# += ( ... ) takes [key]=value elements as = ( ... ) does: hibr appended
# the text `[5]=x` as a value. A keyed element sets its key and the next
# plain element goes after it; on a map declared -A the key is the key; and
# a scalar appended to becomes element 0. Compared against bash.

a=(1)
a+=(2 [5]=x y)
declare -p a
a+=([9]=nine)
declare -p a
a+=(z)
declare -p a

declare -A m
m+=([k]=v)
m+=([j]="w x" [k]=again)
declare -p m
echo "${m[k]} ${m[j]} ${#m[@]}"

unset b
b+=([3]=z q)
declare -p b
unset b
b+=()
declare -p b

s=one
s+=(two)
declare -p s
e=
e+=(x)
declare -p e

declare -a c=(1)
declare c+=([4]=f g)
declare -p c

f() { local l=(a); l+=([3]=d e); declare -p l; }
f

k=7
v="has space"
g=()
g+=([$k]=$v "$v")
declare -p g

( readonly r=(1); r+=(2 3); echo "not reached" ) 2>/dev/null
echo "readonly: $?"
echo "readonly lines: $( (readonly r=(1); r+=(2 3)) 2>&1 | wc -l)"
