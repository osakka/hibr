# An assignment's name ends after its subscripts, not at the first = in the
# word: a key may hold one. hibr split `m["a=b"]=1` at the = inside the key,
# took `m["a` for the name and emptied the whole map -- found when the
# Shortcuts pane's table of keys lost every entry the moment it reached
# Volume Up, whose key is mod-= (0.99.400). Compared against bash.

declare -A m
m[x]=1
m["a=b"]=2
echo "n=${#m[@]} [${m["a=b"]}] [${m[x]}]"

k="c=d"
m["$k"]=3
m["$k"]+=4
echo "n=${#m[@]} [${m["c=d"]}]"

m[e]=f=g
echo "[${m[e]}]"

declare -A t
for key in mod-up mod-= ctrl-= mod-down; do
	t["$key"]+=" $key"
done
echo "n=${#t[@]} [${t["mod-="]}] [${t[mod-down]}]"

x=1=2
echo "x=$x"
f() { echo "y=$y"; }
y=one=two f
