# A standby display's record says which boot and which start of its process
# wrote it, so a record left from before a reboot -- whose process id is by
# then some other process -- is stale rather than "already waiting" for ever.
source "${BASH_SOURCE%/*}/../programs/desktop/wm/standby.hibr" 2> /dev/null
b := dt_sbboot
s := dt_procstarted $$
[ -n "$b" ] && [ -n "$s" ] && echo "this boot and this start are known"
dt_sbalive $$ "$b" "$s" && echo "the process that wrote it: waiting"
dt_sbalive $$ "x$b" "$s" || echo "written in another boot: stale"
dt_sbalive $$ "$b" 1 || echo "the same id, another process: stale"
dt_sbalive $$ - - || echo "a record from before either was kept: stale"
dt_sbalive 999999 "$b" "$s" || echo "a process that has gone: stale"
