# A scalar is its own element 0, for test -v and [[ -v ]] as for an
# expansion: `s=hello; [[ -v s[0] ]]` answered unset (Gitea #597), since
# t_isset read the element with no such rule. It asks xgetk now, the one
# expansions ask. Compared against bash, for scalars, arrays, maps and
# namerefs.

t() {
	if [[ -v $1 ]]; then echo "$1 set"; else echo "$1 unset"; fi
	if test -v "$1"; then echo " test $1 set"; else echo " test $1 unset"; fi
}
s=hello; t 's[0]'; t 's[1]'; t 's'; t 's[1-1]'
i=0; t 's[i]'
e=""; t 'e[0]'
a=(x y); t 'a[0]'; t 'a[1]'; t 'a[2]'
declare -A m=([k]=v); t 'm[0]'; t 'm[k]'
declare -A m0=([0]=z); t 'm0[0]'
declare -n r=s; t 'r[0]'; t 'r[1]'
declare -n ra=a; t 'ra[1]'
declare -n ru=nosuch; t 'ru[0]'
declare -n re='a[1]'; t 're[0]'
t 'nosuch[0]'
declare -i n=5; t 'n[0]'
