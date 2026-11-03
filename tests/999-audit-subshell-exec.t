# A ( ) or $( ) of one simple command runs its program in place of the
# subshell, as & already did, so it forks once rather than twice (Gitea
# #451). What it prints and returns must not change, compared against bash;
# the fork count itself is in the commit, measured with strace.
x=$(echo hi; echo there)
echo "$x"
y=$(true)
echo "[$y] $?"
z=$(exit 3)
echo "$?"
v=$(echo a | tr a b)
echo "$v"
u=$(cd / && pwd)
echo "$u"
f() { echo fn; }
echo "$(f)"
echo "$(false || echo alt)"
(echo sub)
(exit 4)
echo "$?"
(sh -c 'exit 5')
echo "$?"
w=$(sh -c 'echo out; exit 6')
echo "$w $?"
t=$( (echo nested) )
echo "$t"
