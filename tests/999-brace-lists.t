# Brace expansion in every list of words, not only a command's.
for i in {1..3}; do echo "i=$i"; done
for i in {a,b} c x{1,2}y; do echo "j=$i"; done
for i in "{a,b}" '{1..2}' \{a,b\}; do echo "q=$i"; done
for i in {}; do echo "e=$i"; done
for i in {a}; do echo "o=$i"; done
v=z
for i in {a,b}$v; do echo "v=$i"; done
for i in {a,b}{1,2}; do echo "n=$i"; done
for i in {3..1..2}; do echo "s=$i"; done
a=({1..3})
echo "${#a[@]} ${a[*]}"
a=({a,b} c "{d,e}")
echo "${#a[@]} ${a[*]}"
a+=({x,y})
echo "${#a[@]} ${a[*]}"
declare -a c=({1,2})
echo "${#c[@]} ${c[*]}"
f() { local -a d=({p,q}); echo "${#d[@]} ${d[*]}"; }
f
select x in {a,b}; do echo "picked $x"; break; done <<< 2 2>/dev/null
case {a,b} in '{a,b}') echo case-literal ;; esac
y={a,b}
echo "$y"
[[ {a,b} == '{a,b}' ]] && echo test-literal
cat <<< {a,b}
