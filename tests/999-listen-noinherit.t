# A listening socket is not handed to the programs its listener starts
# (Gitea #411): the desktop's control socket was inherited by every program
# run in a terminal window, and a connect that reached one of those copies
# was accepted by nobody. A redirection still hands a program what it is
# given: the dup2 behind it clears close-on-exec.
d=$(mktemp -d)
listen -b "$d/s" fd || { echo "no listen"; exit 1; }
/bin/sh -c "[ -e /proc/self/fd/$fd ] && echo 'a program started is handed it' || echo 'a program started is not handed it'"
/bin/sh -c '[ -e /proc/self/fd/7 ] && echo "and one given it by a redirection has it" || echo "and one given it by a redirection does not"' 7<&$fd
exec {fd}>&-
rm -rf "${d:?}"
