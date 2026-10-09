# Task Manager's End Task and its ps fallback, called directly on a child of
# this test's own -- never through the window, where the selected row is
# whatever the machine running the suite happens to be running.
. examples/desktop/system/tasks.hibr > /dev/null
sleep 60 &
p=$!
tasks_killpid "$p" TERM
wait "$p"
echo "tasks_killpid ended its own child: status $?"
sleep 60 &
p=$!
TK[7]["sel"]=0
TK[7][0]["pid"]=$p
tasks_end 7 KILL
wait "$p"
echo "tasks_end ended the selected row's process: status $?"
tasks_scan_ps 9
[ "${TK[9]["n"]}" -gt 0 ] && echo "the ps fallback lists processes"
[ -n "${TK[9][0]["pid"]}" ] && [ -n "${TK[9][0]["name"]}" ] &&
	echo "each with a pid and a name"
tasks_fulltoggle 9
echo "full command lines: ${TK[9]["full"]}"
tasks_fulltoggle 9
echo "and back: ${TK[9]["full"]}"
