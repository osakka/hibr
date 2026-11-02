self=$(readlink /proc/$$/exe)
echo "-- subscripts that only look like operators"
a=(5 6 7); i=1; echo "${a[i?2:0]} ${a[~-2]}"
echo "-- slices"
a=([5]=x [2]=y [9]=z); echo "${a[@]:3}|${a[@]: -1}|${a[@]:3:1}"
b=(p q r s); echo "${b[@]:1}|${b[@]:1:2}|${b[@]: -2}"
c=(); c[3]=d; c[1]=e; echo "${c[@]:2}"
echo "-- assignment operators join"
p() { printf '<%s>' "$@"; echo; }
set -- "a b" c; unset u; p "${u:=$@}"; echo "$u"
unset u; p "${u=$@}"; unset u; p "${u:-$@}"; p "${u:+$@}"
echo "-- set -u status"
(set -u; echo $nope) 2>/dev/null; echo "subshell $?"
x=$(set -u; echo $nope) 2>/dev/null; echo "substitution $?"
"$self" -c 'set -u; echo $nope' 2>/dev/null; echo "dash c $?"
printf 'set -u\necho $nope\n' > /tmp/r2-$$.sh; "$self" /tmp/r2-$$.sh 2>/dev/null; echo "script $?"; rm -f /tmp/r2-$$.sh
echo "-- ranges"
echo {9223372036854775806..9223372036854775807}
echo {1..3..9223372036854775807}
echo {0..2..-9223372036854775808}
echo {99999999999999999999..1}
echo {-9223372036854775808..-9223372036854775807}
echo {3..1..2}
echo "-- replacement"
x=abc; r='\&'; echo "${x/b/$r}"; r='[&]'; echo "${x/b/$r}"
echo "-- length of a scalar's element"
x=abc; echo "${#x[0]} ${#x[1]} ${x[0]}"
echo "-- lists inside a longer word, and lists that are set"
a=(x y); p "k${!a[@]}k"
pre1=1 pre2=2; p "k${!pre@}k"; IFS=,; p "${!pre*}"; unset IFS
set -- "a b" c; p "${@:-x}"; b=("a b" c); p "${b[@]:-x}"
set -- "" ""; p "${@:-x}"; set -- ""; p "${@:-x}"; IFS=; set -- "" ""; p "${*:-x}"; unset IFS
IFS=; set -- "a b" "" c; p $*; p x$*y; p ${b[*]}; unset IFS
