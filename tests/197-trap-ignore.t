# An ignored signal is a trap, and `trap` has to say so. Compared against
# bash, which does: an ignore listed as a default is an ignore lost, and
# the listing exists to be read back (Gitea #151).
#
# $SH is the shell under test for hibr's run and the reference for bash's,
# so the nested shells below are each the right one.

echo "--- a script's own ignore is listed"
trap '' TSTP
trap 'echo hi' USR2
trap

echo "--- and resetting it removes it"
trap - TSTP
trap

echo "--- -p lists, and -p with names lists only those"
trap 'echo term' TERM
trap -p
echo "one:"
trap -p USR2
echo "rc=$?"

echo "--- -- ends the options"
trap -- 'echo one' USR1
trap -p USR1

echo "--- and the whole point: the listing reads back"
trap '' PIPE
saved=$(trap -p)
trap - PIPE TERM USR1 USR2
echo "cleared:"
trap
eval "$saved"
echo "restored:"
trap

echo "--- a signal the parent ignored is listed, and cannot be changed"
trap - PIPE TERM USR1 USR2
trap '' USR1
"$SH" -c 'trap' 2>&1
"$SH" -c "trap 'echo caught' USR1; trap; kill -USR1 \$\$; echo it did nothing" 2>&1
"$SH" -c 'trap - USR1; trap' 2>&1

echo "--- one the parent did not is trappable as ever"
"$SH" -c "trap 'echo caught' USR2; kill -USR2 \$\$; echo after" 2>&1

echo "--- an empty EXIT trap runs nothing"
trap '' EXIT
