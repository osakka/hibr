# A write to a peer that has gone must not kill the shell -- where the
# shell owns the descriptor and has somewhere to report (Gitea #229).
#
# `send` is that verb: it writes with SIGPIPE held off and answers the
# failure, so a server can reply to a client that gave up and carry on.
# The desktop's control socket is the case this was found from -- its
# reply went out through an ordinary redirection, so one client giving up
# mid-answer killed every window with signal 13.
#
# An ordinary redirection stays lethal on purpose, and the second half of
# this test is that: `hibr script | head -3` has to end when head does, and
# that is the same mechanism.

d=${TMPDIR:-/tmp}/hibr-ep-$$
mkdir -p "$d"
f=$d/pipe
mkfifo "$f"

# A reader that takes one line and goes. Nothing here waits on the clock:
# `exec {w}>` on a fifo does not return until a reader has opened it, and
# `wait` does not return until that reader has taken its line and gone --
# so "nobody is reading now" is a fact rather than a bet, which under a
# loaded gate is the difference between a check and a flake.
{ read -r x < "$f"; } &
rpid=$!
exec {w}> "$f"

send "$w" "the line the reader takes" 2> /dev/null
echo "first: $?"
wait "$rpid"

# Nobody is reading now.
send "$w" "into a pipe nobody holds" 2> /dev/null
echo "gone: $?"

# And again: a pending signal left behind by the first would land here.
send "$w" "and again" 2> /dev/null
echo "again: $?"

echo "the script is still running"

# The same write as an ordinary redirection still raises the signal, which
# is what makes a pipeline end. In a subshell, so this script can say so.
( printf '%s\n' "through a redirection" >&"$w" ) 2> /dev/null
echo "redirection: $?"

exec {w}>&-
rm -rf "$d"
echo done
