# slowest: the call that held a script, named by its own time (0.99.544).
# One long sleep against a wide budget, and predicates rather than counts,
# so a loaded machine or the sanitizer build cannot change what it prints.
slowest start 200
sleep 0.5
f() { sleep 0.3; }
slowest on 200
f
eval "true"
env PASS=hunter2 true
IFS=$'\t' read -r ms n where what <<< "$(slowest)"
[ "$ms" -ge 250 ] && [ "$ms" -lt 500 ] && echo "inner: the sleep inside f, not f and not the earlier one"
[ "$n" -ge 1 ] && echo "inner: at least one call over the budget"
echo "inner: $what"
case $where in *998-slowest.t:6) echo "inner: at the line of the sleep inside f" ;; *) echo "inner at $where" ;; esac
IFS=$'\t' read -r ms n where what <<< "$(slowest all)"
[ "$ms" -ge 450 ] && echo "all: the longer sleep, since start"
[ "$n" -ge 2 ] && echo "all: both sleeps over the budget"
slowest all | grep -c hunter2
slowest start 0
env PASS=hunter2 sleep 0.25
slowest all | grep -c hunter2
slowest all | cut -f4
slowest off
sleep 0.3
IFS=$'\t' read -r ms n where what <<< "$(slowest all)"
[ "$ms" -lt 300 ] && echo "off: nothing more is recorded"
slowest nonsense 2> /dev/null
echo "a bad verb: $?"
