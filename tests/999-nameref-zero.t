# An element read through a nameref is the element of what the reference
# reaches, and key 0 is no exception: an entry holding only fields has no
# value of its own, so it reads empty and unset, never as the name the
# reference holds (Gitea #595). A scalar is still its own element 0 through
# a nameref, as in bash, and a reference that reaches no scalar -- to
# nothing, to itself, to an element -- has no element 0 to give.

declare -gA M
M[0][t]=x
M[1][t]=y
f() {
	local -n m=$1
	local k
	for k in 0 1; do
		echo "$k: [${m[$k]}] set=[${m[$k]+x}] def=[${m[$k]-d}] colon=[${m[$k]:-d}] len=[${#m[$k]}]"
	done
	[[ -v m[0] ]] && echo "-v m[0]: set" || echo "-v m[0]: unset"
	echo "arith m[0]: $(( m[0] + 1 ))"
	echo "field: [${m[0][t]}] keys: [${!m[0][@]}]"
}
f M
echo "direct: [${M[0]}] set=[${M[0]+x}]"

declare -n g=M
echo "global ref: [${g[0]}] set=[${g[0]+x}]"

declare -A N
N[0]=val
declare -n rn=N
echo "a value at 0: [${rn[0]}] set=[${rn[0]+x}] len=[${#rn[0]}]"

a=(p q)
declare -n ra=a
echo "indexed: [${ra[0]}] [${ra[1]}] set=[${ra[0]+x}] len=[${#ra[0]}]"

declare -A h=([0]=z [k]=w)
declare -n rh=h
echo "associative: [${rh[0]}] [${rh[k]}]"

sc=hello
declare -n rs=sc
echo "scalar: [${rs[0]}] set=[${rs[0]+x}] len=[${#rs[0]}] def=[${rs[0]-d}] one=[${rs[1]-u}]"

declare -n r2=rs
echo "two hops: [${r2[0]}] len=[${#r2[0]}]"

b=([1]=q)
declare -n rb=b
echo "sparse, no 0: [${rb[0]-u}] set=[${rb[0]+x}]"

declare -n cyc=cyc 2>/dev/null
echo "to itself: [${cyc[0]-u}] set=[${cyc[0]+x}]"

declare -n un=nosuch
echo "to nothing: [${un[0]-u}] set=[${un[0]+x}] len=[${#un[0]}]"

x=(e0 e1)
declare -n re='x[1]'
echo "to an element: [${re[0]-u}] set=[${re[0]+x}]"

set -u
g() { local -n m=$1; echo "under -u: [${m[0]}]"; }
( g M ) 2>/dev/null || echo "under -u: refused"
