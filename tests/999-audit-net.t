# The network builtins' small edges (Gitea #466), each checked against
# 0.99.427 before it was fixed.
#
# coproc took any first word that was not a function, builtin or module as
# the coprocess's NAME, so `coproc ls -l` named it `ls` and ran `-l`. A name
# is taken only when the word after it is a command.
coproc ls -d /
read -r l <&${COPROC[in]}
echo "coproc ls -d /: $l"
coproc cp2 cat
send ${cp2[out]} hey
recv ${cp2[in]} a
echo "a name before a command: $a"
fd=${cp2[out]}
exec {fd}>&-

# A coprocess is a job, so the shell reaps it and `wait` reads its status;
# it was never in the job table, and was a zombie for the shell's life.
coproc Y sh -c 'exit 3'
wait "$Y_PID"
echo "wait on a coprocess: $?"
coproc Z true
p=$Z_PID
/bin/sleep 0.3
: reaped here
st=gone
read -r st 2> /dev/null < "/proc/$p/stat"
case $st in *") Z "*) echo "left a zombie" ;; *) echo "left nothing behind" ;; esac

# recv -n takes a count and nothing else; -1 said success with nothing read
# and abc meant a line. A count of 0 reads nothing and leaves the input.
recv -n -1 0 v < /dev/null 2> /dev/null
echo "recv -n -1: $?"
recv -n abc 0 v < /dev/null 2> /dev/null
echo "recv -n abc: $?"
echo hi | {
	recv -n 0 0 v
	echo "recv -n 0: $? [$v]"
	read -r r
	echo "the input is still there: $r"
}

# accept reads its descriptor as send and recv do.
accept abc 2> /dev/null
echo "accept abc: $?"
accept -t soon 3 2> /dev/null
echo "accept -t soon: $?"
