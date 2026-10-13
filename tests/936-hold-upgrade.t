# A session taken onto a new hibr, and a session whose server could not exec
# (Gitea #198). hold renders every client from the emulator in its *server*
# process, so a restart -- which re-execs the desktop inside that server --
# can never replace the modules a session draws through: `hold upgrade`
# re-execs the server itself and takes the pty, the program and the emulator
# with it.
#
# Two things make that checkable, and one plausible thing does not. The
# program's own pid is *not* evidence: it is the same whether the exec
# happened or not, which is what made an earlier verification of this
# feature worth nothing. /proc/<pid>/environ is worse than useless --
# pt_rename overwrites the environment block, so the server's own
# HIBR_HOLD_RESUME is not there even when the exec plainly happened, and
# three readings of "it did not happen" came from that. What does answer is
# the mappings: the modules are copied here so they can be replaced
# underneath the running session, and a replaced mapping reads `(deleted)`
# until the server has loaded the new file at the same path, which is
# exactly what apt leaves behind.
#
# Its own TMPDIR, short and unique: hd_dir keys the socket directory off it,
# a unix socket path is 108 bytes so the directory cannot be deep, and
# nothing here may be able to reach a session of whoever runs the suite.
# Recorded: none of it is bash's.

[ -r /proc/self/maps ] || { echo "no /proc, skipped"; exit 0; }
d=/tmp/hibr-upg-$$
mkdir -p "$d/mods"
export TMPDIR=$d
export HIBR_MODPATH=$d/mods
for m in pty term hold; do cp "./build/mods/$m.so" "$d/mods/$m.so"; done
if ! mod load "$d/mods/hold.so" 2> /dev/null; then
	echo "no hold module, skipped"
	rm -rf "$d"
	exit 0
fi

# The server is a fork of this shell, so it maps the copies above.
hold new -d v /bin/sh -c 'echo hi; sleep 60' > /dev/null
srv=
for p in /proc/[0-9]*; do
	read -r c 2> /dev/null < "$p/comm" || continue
	if [ "$c" = "hold:v" ]; then srv=$p; fi
done
if [ -n "$srv" ]; then echo "the server names itself"; else echo "no server found"; fi

# Replaced rather than rewritten, which is what apt does to every file it
# ships: the old inode lives on in the mapping while the name points at the
# new one.
for m in pty term hold; do
	cp "./build/mods/$m.so" "$d/mods/$m.new"
	mv "$d/mods/$m.new" "$d/mods/$m.so"
done
n := grep -c '(deleted)' "$srv/maps"
if [ "${n:-0}" -gt 0 ]; then
	echo "its modules were replaced underneath it"
else
	echo "nothing was replaced [$n]"
fi
r := hold list
read -r nm st was sz <<< "$r"

hold upgrade v
# The server answers an upgrade before it execs and serves nothing while it
# is at it, so the next answer it gives is necessarily after the attempt:
# this needs no waiting, and a wait would hide a server that never got there.
r := hold list
read -r nm st pid sz <<< "$r"
if [ "$pid" = "$was" ]; then
	echo "the same program, after the exec"
else
	echo "the program changed [$r]"
fi
n := grep -c '(deleted)' "$srv/maps"
if [ "${n:-1}" = 0 ]; then
	echo "and nothing it maps is stale any more"
else
	echo "still stale: $n"
fi
hold kill v > /dev/null 2>&1

# The other way about: an exec that cannot happen must leave the session
# whole. hd_upgrade releases the pty first so the new image can adopt it,
# and a failed exec used to carry on with an id the pty module no longer
# knew -- `hold list` answered a pid of -1, a client attaching never
# resized the program again and its exit would never have been noticed,
# while the screen went on being drawn, which is what made it look fine.
# $HIBR is what it execs, and the server takes the value this shell has
# when it forks.
keep=$HIBR
HIBR=$d/not-a-shell
hold new -d u /bin/sh -c 'echo hi; sleep 60' > /dev/null
HIBR=$keep
r := hold list
read -r nm st was sz <<< "$r"
case $was in
'' | -*) echo "the program is not known [$r]" ;;
*) echo "the program is known" ;;
esac
hold upgrade u
r := hold list
read -r nm st pid sz <<< "$r"
if [ "$pid" = "$was" ]; then
	echo "a refused exec leaves the terminal known"
else
	echo "the terminal was lost [$r]"
fi
# And twice, since taking it back has to leave it as it was rather than
# merely once-recoverable.
hold upgrade u
r := hold list
read -r nm st pid sz <<< "$r"
if [ "$pid" = "$was" ]; then
	echo "and again after a second refusal"
else
	echo "the terminal was lost on the second [$r]"
fi
if kill -0 "$was" 2> /dev/null; then
	echo "with the program still running"
else
	echo "the program went"
fi
hold kill u > /dev/null 2>&1

# A name nothing answers to is told so rather than being confused with the
# other refusal: a server from before this verb closes the connection
# without a reply, which is why the client asks for one, and that case
# cannot be reached from here -- it needs a server built before 0.99.159,
# and it was measured against one by hand.
hold upgrade nosuch 2>&1 | sed 's/.*hold: //'
rm -rf "$d"
