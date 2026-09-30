# An array declared with declare -A takes every subscript as a literal key,
# as bash does: a key holding a dash, a plus or a space is a key, not
# arithmetic. Compared against bash. Keys are printed sorted, because bash's
# own order is its hash's and hibr's is the order they were added.
declare -A h
k=a-b
h[$k]=1
k2="my key"
h[$k2]=2
k3=x+1
h[$k3]=3
h[content-type]=4
h[-1]=5
printf '%s\n' "${!h[@]}" | sort
echo "get: ${h[$k]} ${h[$k2]} ${h[x+1]} ${h[content-type]} ${h[-1]}"
unset "h[$k]"
printf '%s\n' "${!h[@]}" | sort
declare -A seen
for w in a-1 b+2 a-1 "c 3"; do
  if [ -n "${seen[$w]}" ]; then echo "again: $w"; fi
  seen[$w]=1
done
echo "${#seen[@]} distinct"
f() {
  local -A l
  l[p-q]=1
  echo "local: ${!l[*]}"
}
f
typeset -A t
t[u-v]=1
echo "typeset: ${!t[*]}"
a=(10 20 30)
i=1
echo "an indexed array still evaluates: ${a[i+1]}"
declare -A m=([c d]=2 [e]=3)
printf '%s\n' "${!m[@]}" | sort
echo "blank key: ${m[c d]}"
declare -A n
n=([x  y]=1 [z]=2)
printf '[%s]\n' "${!n[@]}" | sort
declare -A t=([$(echo s t)]=1)
echo "substituted key: ${!t[*]}"
v="a b"
b=([k]=$v x)
echo "a keyed value is not split: ${#b[@]} [${b[k]}]"
c=($v)
echo "a plain element still splits: ${#c[@]}"
