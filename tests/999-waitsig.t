# An interrupted wait is not an answer. A signal with a handler -- the
# console's own SIGWINCH, which every terminal resize and every reattach of
# a held session sends, or a script's own trap -- makes waitpid return
# EINTR, and jc_waitt used to hand that straight back: the caller took it
# for the command having finished, read a status that was never written (an
# uninitialised int, so a random exit code), and nothing ever waited for the
# child again. One zombie per signal that lands inside a wait, for the life
# of the shell, and a random exit status to go with it (Gitea #143).
#
# The zombie is the part this counts, and it is the part that was reported.
# The status half of the same wait is not reliably reproducible: the caller
# reads whatever was on the stack, which is usually a stale 0 and so reads
# as success.
#
# This test shipped counting a substitution's status as well, and that
# failed about one run in ten -- which turned out not to be this bug at
# all but a second one, an interrupted *read* in the substitution's own
# parent (Gitea #144, tests/999-readsig.t). Only the two statuses that are
# the shell's own answer are counted here.
trap ':' WINCH

zombies() {
	local line st p n=0
	local -a ks

	read -r line < "/proc/$$/task/$$/children" 2> /dev/null
	read -ra ks <<< "$line"
	for p in "${ks[@]}"; do
		[ -n "$p" ] || continue
		read -r st 2> /dev/null < "/proc/$p/stat" || continue
		st=${st##*) }
		case ${st%% *} in Z) n=$((n + 1)) ;; esac
	done
	ret "$n"
}

( i=0; while [ $i -lt 60 ]; do kill -WINCH $$ 2> /dev/null;
  /bin/sleep 0.03; i=$((i + 1)); done ) &
storm=$!

bad=0
i=0
while [ "$i" -lt 12 ]; do
	/bin/true || bad=$((bad + 1))
	/bin/false
	[ "$?" = 1 ] || bad=$((bad + 1))
	# Forked for its own sake: another child for the drain to lose. Its
	# own value and status are 999-readsig's business.
	v=$(/bin/echo x)
	i=$((i + 1))
done
echo "wrong statuses during a signal storm: $bad"

/bin/sleep 0.5
str upper settle > /dev/null
z := zombies
# The storm itself is still running and is a child of this shell, so only a
# zombie counts -- and it is not one until it has exited.
echo "zombies left behind: $z"
kill "$storm" 2> /dev/null
wait "$storm" 2> /dev/null
# The status of a killed job is not this test's business; end on purpose.
:
