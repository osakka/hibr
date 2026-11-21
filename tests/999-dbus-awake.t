# The dbus module as the session's org.freedesktop.ScreenSaver (Blit #171):
# a program's Inhibit is answered with a cookie and kept until UnInhibit, or
# until the program leaves the bus, however it leaves -- a one-shot `gdbus
# call` leaves at once, a killed one without a word. Both object paths
# programs call are served, a method it does not keep is refused rather than
# left waiting, and once the desktop lets the name go nobody holds it.
#
# Against a dbus-daemon of its own, so nothing here reaches the session of
# whoever runs the suite; and never waiting on the bus, only pumping it, as
# the desktop does. Recorded: none of it is bash's.

for c in dbus-daemon gdbus python3; do
	command -v "$c" > /dev/null 2>&1 || { echo "skip: no $c"; exit 77; }
done
python3 -c 'import dbus' 2> /dev/null || { echo "skip: no python3 dbus"; exit 77; }
d=/tmp/hibr-awake-$$
mkdir -p "$d"
dbus-daemon --session --nofork --address="unix:path=$d/bus" > /dev/null 2>&1 &
dp=$!
for ((i = 0; i < 100; i++)); do [ -S "$d/bus" ] && break; sleep 0.02; done
export DBUS_SESSION_BUS_ADDRESS="unix:path=$d/bus"
if ! mod load "$PWD/build/mods/dbus.so" 2> /dev/null || ! dbus open > /dev/null 2>&1; then
	kill "$dp"
	rm -rf "$d"
	echo "skip: no libdbus-1"
	exit 77
fi

# A program that keeps the screen awake while it plays: Inhibit, and the
# cookie it was given written down; d/release takes it back, d/again asks
# again, and it waits to be killed.
cat > "$d/hold.py" << 'E'
import os, sys, time, dbus
d = sys.argv[1]
b = dbus.SessionBus()
o = b.get_object("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver")
s = dbus.Interface(o, "org.freedesktop.ScreenSaver")
c = s.Inhibit("player", "video")
open(d + "/cookie", "w").write("%d\n" % c)
while not os.path.exists(d + "/release"):
    time.sleep(0.02)
s.UnInhibit(c)
open(d + "/released", "w").close()
while not os.path.exists(d + "/again"):
    time.sleep(0.02)
c = s.Inhibit("player", "video again")
open(d + "/cookie2", "w").write("%d\n" % c)
while True:
    time.sleep(1)
E

# Pump until an event matches, never waiting on the bus itself; events one
# pump said after the match are kept for the next call, in order.
Q=()
fn pu(want, int n = 200) {
	local e k i
	for ((i = 0; i < n; i++)); do
		e := dbus pump
		Q+=("${e[@]}")
		while [ "${#Q[@]}" -gt 0 ]; do
			k=${Q[0]}
			Q=("${Q[@]:1}")
			[[ $k == $want ]] && { echo "event: $k"; return 0; }
		done
		sleep 0.05
	done
	echo "never: $want"
	return 1
}
# Pump until a file is there, so a client's call is answered meanwhile.
fn pf(f, int n = 200) {
	local e i
	for ((i = 0; i < n; i++)); do
		[ -e "$f" ] && return 0
		e := dbus pump
		sleep 0.05
	done
	echo "never there: ${f##*/}"
	return 1
}

echo "--- nobody asked for the name yet"
s := dbus awake
echo "state $s"
echo "--- asked, the desktop is the session's screen saver"
dbus awake on
pu "awake *"
s := dbus awake
echo "state $s"

echo "--- a program keeps the screen awake, and is given a cookie"
python3 "$d/hold.py" "$d" 2> /dev/null &
hp=$!
pu "awake 1"
pf "$d/cookie"
read -r c < "$d/cookie"
echo "its cookie: $c"
a := dbus awakes
printf 'held: %s\n' "${a[@]}"

echo "--- one that asks at the other path and leaves at once lets go as it leaves"
timeout 10 gdbus call --session --dest org.freedesktop.ScreenSaver --object-path /ScreenSaver \
	--method org.freedesktop.ScreenSaver.Inhibit oneshot brief > "$d/one" 2>&1 &
pu "awake 2"
pu "awake 1"
wait $!
cat "$d/one"
a := dbus awakes
printf 'held: %s\n' "${a[@]}"

echo "--- UnInhibit takes the cookie back"
: > "$d/release"
pu "awake 0"
pf "$d/released"
a := dbus awakes
echo "held: ${#a[@]}"

echo "--- asked again and killed without a word, it is let go"
: > "$d/again"
pu "awake 1"
pf "$d/cookie2"
read -r c < "$d/cookie2"
echo "its second cookie: $c"
kill -9 "$hp"
wait "$hp" 2> /dev/null
pu "awake 0"

echo "--- a method the saver does not keep is refused, not left waiting"
timeout 10 gdbus call --session --dest org.freedesktop.ScreenSaver --object-path /org/freedesktop/ScreenSaver \
	--method org.freedesktop.ScreenSaver.GetActive > "$d/ga" 2>&1 &
gp=$!
pf "$d/none" 20 > /dev/null
wait "$gp"
echo "status $?"
grep -q UnknownMethod "$d/ga" && echo "answered UnknownMethod"

echo "--- let go, the name is nobody's"
dbus awake off
pf "$d/none" 10 > /dev/null
s := dbus awake
echo "state $s"
timeout 10 gdbus call --session --dest org.freedesktop.ScreenSaver --object-path /ScreenSaver \
	--method org.freedesktop.ScreenSaver.Inhibit late ask > "$d/late" 2>&1
echo "status $?"
grep -q ServiceUnknown "$d/late" && echo "nobody has org.freedesktop.ScreenSaver"

dbus close
kill "$dp"
wait "$dp" 2> /dev/null
rm -rf "$d"
