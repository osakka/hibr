# A value copied onto itself, or through the element-0 mirror, is copied
# before the old one is freed (Gitea #430): `RET := f` bound freed memory,
# and `IFS[0]=` left the IFS cache pointing at it.
f() { ret hello-world-long-value-here; }
RET := f
echo "[$RET]"
g() { RET["a"]=1; ret -k; }
RET := g
echo "${RET["a"]}"
IFS=" "
IFS[0]=":"
x="a:b c"
set -- $x
echo "$# $1"
r=(one two)
r[0]=uno
echo "$r ${r[1]}"
