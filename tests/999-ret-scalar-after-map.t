# A scalar result is a scalar whatever the slot held before: a map left in
# RET by an earlier call used to keep its array mark through `ret VALUE`,
# so `x := f` read ${#x[@]} as 0 and ${x@a} as a -- and a desktop that
# chose JSON by that mark sent a window's id as [] (ADR 0050).
g() { json parse RET <<< '[1, 2]'; ret -k; }
m := g
echo "a map: ${#m[@]} [${m[*]}] [${m@a}]"
f() { g; ret 7; }
x := f
echo "a scalar after it: [$x] ${#x[@]} [${x@a}]"
k() { g; ret -k; }
y := k
echo "ret -k still passes the map: ${#y[@]} [${y@a}]"
s := str upper ab
echo "a builtin after it: [$s] ${#s[@]} [${s@a}]"
declare -a d=(1 2)
d=5
echo "a script's own assignment keeps bash's rule: ${d[*]}"
