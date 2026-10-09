# An interrupted read is not an end of input. A signal with a handler makes
# read return EINTR, and every loop that stopped at one lost whatever was
# still coming -- worst in a command substitution, where the parent then
# closed the pipe and the child died of SIGPIPE: `v=$(cmd)` came back
# **empty, with status 141**, with nothing anywhere saying why (Gitea #144).
#
# A script's own trap is installed without SA_RESTART on purpose, because
# bash interrupts a blocking `read` builtin so the trap can run -- so a
# read that must not lose data cannot rely on the flag and retries instead.
# The line is drawn at whose child the other end is: a substitution, a
# `:=` from a program and `$(< file)` all read from something this shell
# started or opened, which ends on its own, so they retry; `recv` on a
# socket and the `read` builtin stay interruptible, since a trap has to be
# able to break a wait that may never end.
#
# Sixty substitutions against a signal every 15 ms: 1 to 3 of them failed
# on 0.99.114, every run of five, so the window is certain rather than
# lucky -- which is what the last of these cost, shipped as a check that
# only failed one run in ten.
trap ':' WINCH

( i=0; while [ $i -lt 120 ]; do kill -WINCH $$ 2> /dev/null;
  /bin/sleep 0.015; i=$((i + 1)); done ) &
storm=$!

bad=0
pipe=0
i=0
while [ "$i" -lt 60 ]; do
	v=$(/bin/echo x)
	st=$?
	[ "$v" = x ] || bad=$((bad + 1))
	[ "$st" = 0 ] || bad=$((bad + 1))
	[ "$st" = 141 ] && pipe=$((pipe + 1))
	i=$((i + 1))
done
echo "substitutions that came back wrong: $bad"
echo "of them, killed by SIGPIPE: $pipe"

# `:=` from a program is the other way a child's output is collected, and
# the other leg that can fail. The $(< file) beside it reads a regular
# file, which a local filesystem does not interrupt, so it is here as a
# statement of the rule rather than as a test of it -- say so, rather than
# letting a passing leg look like evidence.
bad=0
i=0
while [ "$i" -lt 60 ]; do
	v=$(< tests/999-readsig.t)
	case $v in *"An interrupted read is not an end of input"*) ;;
	*) bad=$((bad + 1)) ;; esac
	w := /bin/echo y
	[ "$w" = y ] || bad=$((bad + 1))
	i=$((i + 1))
done
echo "a file read and a bound program that came back wrong: $bad"

kill "$storm" 2> /dev/null
wait "$storm" 2> /dev/null
# The status of a killed job is not this test's business; end on purpose.
:
