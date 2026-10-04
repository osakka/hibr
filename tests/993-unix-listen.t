# listen -b on a Unix socket path: owner-only, one still answering is not
# taken, a stale one is replaced; accept -t waits at most that long (0
# only looks); a connection's REMOTE is the peer's uid; and {var} gets a
# socket's descriptor as it gets a file's. Recorded: bash has no listen.
d=$(mktemp -d)
listen -b "$d/s" L
echo "bound: $([ -S "$d/s" ] && echo socket)"
ls -l "$d/s" | cut -c1-10
accept -t 0 "$L" C; echo "nothing waiting: $?"
exec {fd}<>"/dev/unix/$d/s"
echo "client fd is a number: $([[ $fd =~ ^[0-9]+$ ]] && echo yes)"
printf 'hello\n' >&"$fd"
accept -t 1 "$L" C; echo "one waiting: $?"
[ "$REMOTE" = "uid:$UID" ] && echo "remote is our own uid"
read -r line <&"$C"; echo "server read: $line"
printf 'back\n' >&"$C"
read -r line <&"$fd"; echo "client read: $line"
exec {fd}>&- {C}>&-
listen -b "$d/s" L2 2> /dev/null; echo "taken while live: $?"
exec {L}>&-
listen -b "$d/s" L3; echo "stale replaced: $?"
exec {L3}>&-
accept -t 0.1 2> /dev/null; echo "no descriptor: $?"
rm -rf "$d"
