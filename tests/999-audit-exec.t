self=$(readlink /proc/$$/exe)
d=$(mktemp -d); cd "$d"
echo "-- wait"
(exit 3) & (exit 4) & wait; echo "plain $?"
(exit 3) & p1=$!; (exit 4) & p2=$!; wait $p1 $p2; echo "both $?"
(exit 5) & p=$!; sleep 0.2; wait $p; echo "reaped already $?"
wait 99999 2>/dev/null; echo "not a child $?"
echo "-- trap"
trap 'echo x' 2abc 2>/dev/null; echo "2abc $?"
trap 'echo int' INT; trap INT; trap; echo "reset $?"
trap 'echo int' INT; trap 2; trap; echo "numeric reset $?"
echo "-- exec"
"$self" -c 'exec /nonexistent/x; echo after' 2>/dev/null; echo "missing $?"
"$self" -c 'exec /etc/passwd; echo after' 2>/dev/null; echo "not executable $?"
"$self" -c 'shopt -s execfail; exec /nonexistent/x; echo "after $?"' 2>/dev/null
echo "-- redirection"
echo hi >& out.txt; cat out.txt
{ echo o; echo e >&2; } >& both.txt; sort both.txt
x=$(printf '%070000d' 0); cat <<EOF | wc -c
$x
EOF
echo "-- arithmetic for"
"$self" -c 'for ((i=0; 1/0; i++)); do echo body; done; echo after' 2>/dev/null; echo "cfor $?"
echo "-- ignored on entry"
(trap '' INT; "$self" -c 'trap "echo caught" INT; kill -INT $$; echo survived')
cd /; rm -rf "$d"
