# Locals shadow whole variables, and the declaring builtins take arrays.
#
# A local used to save only the scalar of what it hid, so a function with a
# local named like a global array destroyed that array for good.

q=(9 8)
f() { local q=(1 2); echo "inside: ${q[*]}"; }
f
echo "the global array is back: ${q[*]}"

g() { local q; q[0]=5; }
g
echo "element writes to a local stay local: ${q[*]}"

declare -A m
m[k]=v
h() { local m; m=scalar; }
h
echo "a map comes back whole: ${m[k]}"

x=1
outer() { local x; echo "unset inside: [$x]"; x=2; inner; }
inner() { echo "dynamic scope sees: $x"; }
outer
echo "and out again: $x"

export E=out
e() { local E=in; sh -c 'echo "a child sees the local: $E"'; }
e
sh -c 'echo "and the global after: $E"'

# The declaring builtins take name=(...) as local always did.
declare -a d=(1 2)
echo "declare -a: ${d[1]}"
typeset -a t=(a b)
echo "typeset -a: ${t[1]}"
fa() { declare -A am=([a]=1 [b]=2); echo "declare -A in a function: ${am[b]}"; }
fa
echo "and it was local: [${am[b]}]"
fg() { declare -gA gm=([a]=b); }
fg
echo "declare -gA is global: ${gm[a]}"
fl() {
	local -a la=(1 2)
	local -A lm
	lm[x]=y
	local -r lr=5
	local -i li=3+4
	local -- ld=6
	echo "local with flags: ${la[1]} ${lm[x]} $lr $li $ld"
}
fl
echo "none of it leaked: [${la[*]}][${lm[x]}][$lr][$li][$ld]"

readonly -a ro=(1 2)
echo "readonly -a: ${ro[1]}"
( ro[0]=9 ) 2>/dev/null
echo "an element of a readonly array cannot change: $?"
declare -r -a dr=(a b)
echo "declare -r -a: ${dr[*]}"
readonly rx=1
( declare -a rx=(2) ) 2>/dev/null
echo "an array cannot land on a readonly name: $?"
fr() { local rx=2; }
( fr ) 2>/dev/null
echo "nor can a local shadow one: $?"

# Unkeyed elements carry on from the last numbered key.
a=(z [5]=f g)
echo "after [5]: ${!a[*]} ${a[6]}"
