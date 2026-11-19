# A scalar is its own element 0 in arithmetic too: `n=7; $(( n[0] + 1 ))`
# was 1 (Gitea #598), since the arithmetic getter read the element with no
# such rule. It asks xgetk, the one expansions ask, nameref and all.
# Compared against bash.

n=7; echo $(( n[0] + 1 )) $(( n[1] + 1 )) $(( n + 1 ))
i=0; echo $(( n[i] )) $(( n[1-1] ))
x="3 * 4"; echo $(( x[0] ))
declare -n r=n; echo $(( r[0] + 2 )) $(( r ))
declare -n ru=nosuch; echo $(( ru[0] + 0 ))
a=(5 6); echo $(( a[0] + a[1] ))
declare -n ra=a; echo $(( ra[1] ))
declare -A m=([k]=9); echo $(( m[0] + 0 ))
declare -A m0=([0]=4); echo $(( m0[0] ))
e=""; echo $(( e[0] + 1 ))
declare -i d=5; echo $(( d[0] * 2 ))
(( n[0] += 1 )); echo $n; declare -p n
let 'n[0]++'; declare -p n
echo $(( n[0]++ )); declare -p n
declare -n re='a[1]'; echo $(( re[0] + 0 ))
c=0; (( c[0] )) && echo t || echo f
