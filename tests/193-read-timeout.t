# read -t as bash has it: a timeout of 0 says whether input is waiting and
# reads nothing, a fraction of a second is a fraction (it was cut to 0 and
# then waited for ever), a timeout is status 142, and a timeout that is not
# a number is refused. And $! names a process substitution's child, so a
# script can end the one it started. Found building the Sound pane, whose
# desktop watches a `pactl subscribe` it has to be able to stop (Gitea #306).
exec {fd}< <(echo one; sleep 0.4; echo two)
p=$!
[ -n "$p" ] && kill -0 "$p" 2> /dev/null && echo "the child is \$!"
sleep 0.2
read -t 0 -u $fd
echo "waiting: $? reply=[${REPLY-}]"
read -r -u $fd l
echo "then: $l"
read -t 0 -u $fd
echo "nothing waiting: $?"
read -r -u $fd l
echo "then: $l"
exec {fd}<&-

read -t 0.3 x < <(sleep 3)
echo "a fraction times out: $?"
read -t 1.5 x < <(sleep 0.2; echo late)
echo "and waits that long: $? $x"
read -t abc x 2> /dev/null
echo "not a number: $?"
read -t -1 x 2> /dev/null
echo "negative: $?"
echo hi | { read -t 2 v; echo "piped: $? $v"; }
