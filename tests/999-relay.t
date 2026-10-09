# A helper the shell forks for its own purposes must be owned by somebody.
# net_tls forked the TLS relay, logged its pid and threw it away: nothing
# waits for it, because the descriptor it serves can be dup'd, inherited,
# passed to a child or closed anywhere, so no one place knows the relay has
# ended -- one TLS connection, one zombie, for the life of the shell. A
# desktop that syncs collects them all day.
#
# The relay is reparented to init now (a second fork, the middle waited for
# at once), the way hold's own server already was. The handshake here fails
# -- the listener is a plain socket, not a TLS one -- which is exactly the
# point: the relay is forked before any handshake, so it starts and exits
# either way, and what this checks is who is left holding it.
command -v python3 > /dev/null 2>&1 || { echo "no zombie relay left behind"; exit 0; }
port=$((20000 + $$ % 20000))
python3 -c "
import socket, time
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', $port))
s.listen(4)
for _ in range(3):
    try:
        c, _ = s.accept()
        time.sleep(0.1)
        c.close()
    except OSError:
        break
" &
server=$!
sys sleepms 300 2> /dev/null || /bin/sleep 1

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

export HIBR_TLS_INSECURE=1
# The relay writes its own failure to the stderr it inherited, from its own
# process, so whether that line lands before this shell exits is a race --
# it is in the gate's output and not in a quiet run. The group's redirection
# applies in this shell (no fork), so the relay inherits it.
{
	i=0
	while [ "$i" -lt 3 ]; do
		exec {fd}<>"/dev/tls/127.0.0.1/$port" && exec {fd}>&-
		i=$((i + 1))
	done
} 2> /dev/null
/bin/sleep 1
str upper settle > /dev/null
z := zombies
[ "$z" = 0 ] && echo "no zombie relay left behind" ||
	echo "$z zombie relays left behind"
kill "$server" 2> /dev/null
wait "$server" 2> /dev/null
