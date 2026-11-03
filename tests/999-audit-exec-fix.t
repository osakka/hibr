self=$(readlink /proc/$$/exe)
d=$(mktemp -d); cd "$d"
echo "-- the job table in children"
sleep 1 & sleep 1 &
m=$(jobs | wc -l); echo "substitution $m"
jobs | wc -l | sed 's/^/pipeline /'
(jobs | wc -l) | sed 's/^/subshell /'
{ jobs; } | wc -l | sed 's/^/compound stage /'
kill %1 %2 2>/dev/null; wait
echo "-- a background job ignores INT and QUIT when it is a simple command"
trap "echo p" INT
grep SigIgn /proc/self/status & wait
(grep SigIgn /proc/self/status; :) & wait
trap - INT
grep SigIgn /proc/self/status & wait
echo "-- a trapped signal ends wait -n first"
trap "echo got" USR1; (sleep .3; kill -USR1 $$) & sleep 1 & wait -n; echo "rc=$?"; wait; trap - USR1
echo "-- trap - alone"
trap - 2>/dev/null; echo "rc=$?"
echo "-- 1>& file"
{ echo a; echo b >&2; } 1>& f2; cat f2
echo "-- a failed exec runs no exit trap"
"$self" -c 'trap "echo bye" EXIT; exec /nonexistent/x' 2>/dev/null; echo "rc=$?"
echo "-- a file with no #! line runs in a new shell"
printf 'echo "hi from a script with no #! line: $*"\n' > nsb; chmod +x nsb
./nsb x y; echo "rc=$?"
"$self" -c 'exec ./nsb z'; echo "rc=$?"
printf 'a\0b\n' > bin; chmod +x bin; ./bin 2>/dev/null; echo "binary rc=$?"
echo "-- a child that leaves its group is still waited for"
python3 -c "import os,time; time.sleep(.2); os.setpgid(0, os.getpgid(os.getppid())); raise SystemExit(7)" & p=$!; wait $p; echo "wait pid $?"
cd /; rm -rf "$d"
