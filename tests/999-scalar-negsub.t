# A negative subscript on a scalar is bash's "bad array subscript": there
# is no array to count back in. The expansion is empty, a test of it is
# false, arithmetic reads 0, and the command goes on. hibr answered empty
# and said nothing (Gitea #602, item 4; ADR 0006). Compared against bash,
# with standard error folded in and the message's prefix taken off, since
# each shell names itself there.

norm() { sed -E 's/^.*: ([^:]+: bad array subscript)$/\1/'; }

s=hello
{ echo "plain [${s[-1]}]"; } 2>&1 | norm
{ echo "two back [${s[-2]}]"; } 2>&1 | norm
{ echo "default [${s[-1]-def}]"; } 2>&1 | norm
{ [[ -v s[-1] ]] && echo "-v set" || echo "-v unset"; } 2>&1 | norm
{ echo "arithmetic $(( s[-1] + 1 ))"; } 2>&1 | norm
f() { local l=v; echo "local [${l[-1]}]"; }
{ f; } 2>&1 | norm
echo "minus zero [${s[-0]}] zero [${s[0]}]"
b=(x y)
echo "array [${b[-1]}] [${b[-2]}]"
echo "end"
