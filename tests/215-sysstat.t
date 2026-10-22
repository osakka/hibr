# lib/sysstat: one CPU and memory formula for About and Task Manager (Gitea
# #354). CPU is top(1)'s: eight fields, idle and iowait both idle, guest and
# guest_nice left out because the kernel counts them inside user and nice
# already. The samples here have large guest fields, which a sum of every
# field counted twice, and an iowait that a reading of idle alone missed.
d=$(mktemp -d)
SY_OS=Linux
SY_STAT=$d/stat
SY_MEMINFO=$d/meminfo
. programs/desktop/lib/sysstat.hibr
echo "cpu  1000 0 500 8000 500 0 0 0 900 0" > "$d/stat"
a := sy_cpupct one
echo "first sample: $a"
echo "cpu  1600 0 800 8800 700 0 0 0 1500 0" > "$d/stat"
b := sy_cpupct one
echo "busy since: $b"
c := sy_cpupct two
echo "another key starts its own: $c"
printf 'MemTotal:  1000 kB\nMemFree:  100 kB\nMemAvailable:  250 kB\n' > "$d/meminfo"
m := sy_mempct
echo "memory: $m"
rm -rf "$d"
