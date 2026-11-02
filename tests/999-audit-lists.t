# Lists keep their elements apart (Gitea #439, #440), compared against bash.
# A subscript is evaluated once; operators act on each element of an @
# list; and an element of "$@" or "${a[@]}" inside a longer word is a field
# of its own, kept even when empty in quotes, whatever IFS holds.
a=(x y z w v); i=0; echo "${a[i++]}"; echo $i
set -- a "" b; printf "<%s>" "x$@y"; echo
printf "<%s>" x$@y; echo
set -- ""; printf "<%s>" "x$@y"; echo
set -- "" ""; printf "<%s>" "$@"""; echo
b=("" ""); printf "<%s>" "x${b[@]}y"; echo
IFS=,; c=("x y" z); printf "<%s>" ${c[@]}; set -- "a b" c; printf "<%s>" $@; echo; unset IFS
set -- 1 "2 3" 4; printf "<%s>" "${@:1:2}x"; echo
d=(pa pb pc.txt); echo ${d[@]#p} ${d[@]%.txt}; printf "<%s>" "${d[@]#p}"; echo
set -- x.c y.c; echo ${@%.c}; printf "<%s>" "${@%.c}"; echo
e=(pa pb); echo ${e[@]/p/P} ${e[@]/#/-}; printf "<%s>" "${e[*]/p/P}"; echo
IFS=:; set -- x.c y.c; echo "${*%.c}"; unset IFS
set -u; f=(); printf "<%s>" "${f[@]}"; echo "|"; set +u
set -- p q; printf "<%s>" "${u:-$@}"; echo
x="$@"; echo "[$x]"
g=(1 2 3); echo ${#g[@]} "${!g[@]}" ${g[@]:1}
