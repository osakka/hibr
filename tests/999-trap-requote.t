# A trap listing is shell input: a single quote in a command is written
# '\'' as bash writes it, ERR, DEBUG and RETURN are names `trap -p` takes,
# and the listing read back into a fresh shell gives the same traps.
# Compared against bash. $SH is the shell under test for hibr's run and
# the reference for bash's, so the nested shells are each the right one.

echo "--- a single quote is re-quoted"
trap "echo 'hi' there" USR1
trap "printf '%s\n' \"it's\"" USR2
trap -p USR1 USR2

echo "--- and the listing evaluates back"
saved=$(trap -p USR1 USR2)
trap - USR1 USR2
trap -p USR1 USR2
echo "cleared rc=$?"
eval "$saved"
[ "$(trap -p USR1 USR2)" = "$saved" ] && echo "same after eval"
kill -USR2 $$

echo "--- ERR, DEBUG and RETURN are names trap -p takes"
trap "echo 'err' \$?" ERR
trap ':' DEBUG
trap "echo 'ret'" RETURN
trap -p ERR DEBUG RETURN
echo "rc=$?"
trap -p ERR USR1
trap - DEBUG RETURN
trap -p DEBUG RETURN
echo "unset rc=$?"

echo "--- in bash's order: signals, then DEBUG, ERR, RETURN"
trap ':' DEBUG
trap -p USR1 USR2 DEBUG ERR
trap - DEBUG

echo "--- a fresh shell reads the listing back"
want=$(trap -p USR1 USR2 ERR)
got=$("$SH" -c "$want
trap -p USR1 USR2 ERR")
[ "$got" = "$want" ] && echo "same in a fresh shell" || printf '%s\n' "$got"

echo "--- a quote at either end, and only quotes"
trap "'" USR1
trap "''a''" USR2
saved=$(trap -p USR1 USR2)
printf '%s\n' "$saved"
got=$("$SH" -c "$saved
trap -p USR1 USR2")
[ "$got" = "$saved" ] && echo "same in a fresh shell"

echo "--- trap - ERR clears it, and an unknown name is still refused"
trap - ERR USR1 USR2
trap -p ERR
echo "rc=$?"
trap -p NOSUCHSIG 2>/dev/null
echo "unknown rc=$?"

echo "--- ERR runs once for a failing pipeline, not in its stages"
trap 'echo "E $?"' ERR
true | false
echo "after pipe"
x=$(false; echo sub)
echo "x=$x"
(false; echo "in subshell")
( trap 'echo "set in the child $?"' ERR; false )
echo "listed in a child: $(trap -p ERR)"
false || true
if false; then :; fi
! false
true && false
echo "rc=$?"
trap - ERR
false
echo done
