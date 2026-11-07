# A script's own writers set element 0 of an array and leave the rest, as
# assignment does; a prefix is temporary; set -u reaches arithmetic; a closed
# standard output is an error.
a=(1 2 3); declare a=x; echo "${#a[@]} ${a[*]}"
a=(1 2 3); export a=x; echo "${#a[@]} ${a[*]}"
a=(1 2 3); for a in x y; do :; done; echo "${#a[@]} ${a[*]}"
a=(1 2 3); read a <<< x; echo "${#a[@]} ${a[*]}"
a=(1 2 3); b=(4 5); read a b <<< "x y z"; echo "${a[*]}|${b[*]}"
a=(1 2 3); read -a a <<< "p q"; echo "${#a[@]} ${a[*]}"
a=(1 2 3); read a < /dev/null; echo "$? ${#a[@]} ${a[*]}"
a=(1 2 3); printf -v a x; echo "${#a[@]} ${a[*]}"
a=(1 2 3); set -- -x; getopts x a; echo "${#a[@]} ${a[*]}"
a=(1 2 3); a=x true; echo "${#a[@]} ${a[*]}"
f() { echo "in ${a[*]}"; }; a=(1 2 3); a=x f; echo "${#a[@]} ${a[*]}"
b=1; b=x true; echo "$b"
c=x true; echo "[${c-u}]"
g() { local a; read a <<< x; echo "$a"; }; a=(1 2 3); g; echo "${a[*]}"
(set -u; [[ nope -eq 0 ]]; echo no) 2> /dev/null; echo "st $?"
(set -u; echo $((nope)); echo no) 2> /dev/null; echo "st $?"
(set -u; (( n++ )); echo no) 2> /dev/null; echo "st $?"
(set -u; n=1; (( n++ )); (( y = 3 )); echo "$n $y"); echo "st $?"
(set -u; a=(1); echo $((a[5])); n=0; echo $(( n ? nope : 2 )) $(( 0 && nope ))); echo "st $?"
echo e >&- 2> /dev/null; echo $?
printf x >&- 2> /dev/null; echo $?
echo -n >&-; echo $?
s=hello; echo "${s:1:-1}"
a=(1 2 3 4); echo "${a[@]:1:2} ${a[@]: -2}"
