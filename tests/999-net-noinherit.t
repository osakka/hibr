# A socket the shell's net builtins make -- listen's, accept's, connect's --
# is the script's: a program it starts does not inherit it, except through a
# redirection, so no program left running keeps a connection open for its
# peer after the script has let it go (ADR 0050). Before 2026-11-19 only the
# listening socket was close-on-exec (Gitea #411), and an app's connection
# to the desktop never ended while any program the desktop had started since
# was alive.

T=/tmp/hibr-noinherit-$$
listen -b "$T.sock" L
connect -U "$T.sock" C
accept -t 5 "$L" A || echo "no connection"
mine="$(readlink /proc/$$/fd/$L) $(readlink /proc/$$/fd/$C) $(readlink /proc/$$/fd/$A)"
held='n=0; for f in /proc/self/fd/*; do l=$(readlink "$f"); case " $MINE " in *" $l "*) n=$((n + 1)) ;; esac; done; echo $n'
echo "a program started now holds none of them: $(MINE=$mine bash -c "$held")"
echo "given one through a redirection, it has that one: $(MINE=$mine bash -c "$held" 5<&$A)"
sleep 30 &
sp=$!
exec {A}>&-
st=0
for ((i = 0; i < 40; i++)); do
	recv -p "$C" got 2> /dev/null || { st=1; break; }
	sleep 0.05
done
echo "let go while a program runs, the peer sees its end: $([ "$st" = 1 ] && echo yes || echo no)"
kill "$sp"
wait "$sp" 2> /dev/null
connect -U "$T.none" X 2> /dev/null
echo "a path nothing listens on: status $?"
exec {C}>&- {L}>&-
rm -f "$T.sock"
