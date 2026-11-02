# IFS is set when the shell starts, as in bash, and an inherited one is
# not used (Gitea #441): o=$IFS; ...; IFS=$o used to leave it empty and
# splitting off, since an unset IFS saved and restored is an empty one.
self=$(readlink /proc/$$/exe)
printf "%s|" "${IFS-unset}" | od -An -c
o=$IFS; IFS=,; IFS=$o; x="a b"; set -- $x; echo $#
IFS=: "$self" -c 'x=a:b; set -- $x; echo $#'
