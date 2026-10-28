# bash's ${x@A}, @a, @P, @K, @k, [[ -o opt ]], [[ -R name ]] and [ -o opt ]
# (Gitea #401), compared against bash itself. An assoc array here has one key:
# hibr keeps a map's keys in the order they went in and bash in its hash's,
# which is deliberate, so a test of two keys would compare the orders.
x=1; declare -ix y=2; declare -r z=3; declare -a a=(p "q r"); declare -A h=([k]=v)
declare -l lo=AbC; export ex=e; declare -iux iu=5; declare -ar ar=(1 2)
declare -n ref=lo
for v in x y z a h lo ex iu ar; do printf "%-3s A=[%s] a=[%s]\n" $v "${!v@A}" "${!v@a}"; done
echo "x=[${x@A}] ref=[${ref@a}] [${ref@A}]"
echo "a[@]A=[${a[@]@A}] h[@]A=[${h[@]@A}]"
echo "aK=[${a[@]@K}] hK=[${h[@]@K}] xK=[${x@K}]"
printf '<%s>' "${a[@]@k}"; echo
printf '<%s>' ${a[@]@k}; echo
printf '<%s>' "${a[@]@Q}"; echo
printf '<%s>' "${a[@]^^}"; echo
printf '<%s>' "${a[@]@a}"; echo
q='it'"'"'s $x'; echo "qK=[${q@K}] qQ=[${q@Q}] qA=[${q@A}]"
unset nope; echo "unset: Q=[${nope@Q}] K=[${nope@K}] A=[${nope@A}] a=[${nope@a}]"
e=; echo "empty: A=[${e@A}] K=[${e@K}]"
p='\w is here'; cd /tmp; echo "P=[${p@P}]"
echo "names: ${!ar@}"
[[ -o errexit ]]; echo "-o errexit off: $?"
set -o noglob; [[ -o noglob ]]; echo "-o noglob on: $?"; [ -o noglob ]; echo "[ -o noglob ]: $?"; set +o noglob
[[ -o nosuchoption ]]; echo "-o unknown: $?"
[[ -R ref ]]; echo "-R ref: $?"
[[ -R x ]]; echo "-R x: $?"
[ a -o "" ]; echo "binary -o still: $?"
declare -iu d; d=3; echo "iu=[$d]"
cd /usr; set -f; echo "set -f: bi* lib*"; echo bi*; set +f; echo "and back:" bi*
