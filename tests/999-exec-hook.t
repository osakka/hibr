# A module's child must not outlive an exec in place. Restart Desktop execs
# the desktop in place, and the web module's engine, a child of the image
# being replaced, was left to die unreaped -- one zombie per restart, the
# only zombies on the machine. hibr_onexec lets a module end its children
# and wait for them before exec replaces the process; a subshell's exec is
# not the parent's and runs none of the parent's hooks. The check after
# the exec is a hibr, which waits only for its own children, where dash
# would reap any it found and hide the zombie.
d=/tmp/hibr-exechook-$$
mkdir -p "$d"
self=$(readlink /proc/$$/exe)
if [ "$(uname)" = Linux ] && (tools/tccld -shared -fPIC -Iinclude -o "$d/exechook.so" tests/exechook.c ||
	gcc -shared -fPIC -Iinclude -o "$d/exechook.so" tests/exechook.c) 2> /dev/null
then
	cat > "$d/run.hibr" <<'S'
need exechook || exit 1
c=$(exechook pid)
( exec /bin/true )
kill -0 "$c" && echo "a subshell's exec left the parent's child alone"
exec "$HIBR" -c 'sleep 0.3; z=$(ps -o stat= --ppid $$ | grep -c Z); echo "zombies after the exec: $z"'
S
	HIBR_MODPATH="$d:${HIBR_MODPATH:-./build/mods}" "$self" "$d/run.hibr" 2>&1
	echo "status $?"
else
	echo "no compiler or not Linux, skipped"
fi
rm -rf "$d"
