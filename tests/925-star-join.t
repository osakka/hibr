# "$*", "${a[*]}" and "${!a[*]}" join with IFS's first character
set -- x "y z"
a=(p q r)
declare -A m=([k]=1)
show() { printf '<%s>' "$@"; echo; }
show "$*" "${*:1}" "${a[*]}" "${a[*]:0:2}" "${!a[*]}" "${m[*]}"
IFS=-
show "$*" "${*:1}" "${a[*]}" "${a[*]:0:2}" "${!a[*]}" "${a[@]:1}"
show $* ${a[*]}
x=$*
show "$x"
IFS=
show "$*" "${a[*]}"
IFS=$'\t'
s="${a[*]}"
read -ra back <<< "$s"
show "${#back[@]}" "${back[@]}"
f() { local IFS=:; echo "${a[*]}"; }
f
unset IFS
show "${a[*]}"
