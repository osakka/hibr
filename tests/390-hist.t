history -c
history -s "echo first"
history -s "ls -l /tmp"
history -p '!!'
history -p 'sudo !!'
history -p '!$'
history -p '!ec'
history -p '!1' '!-1'
history -p 'x=$!' 'a != b' "'!!'" 'echo "!!"'
history -p '!zzz' 2>/dev/null; echo "missing event status=$?"
history -p '!9' 2>/dev/null; echo "out of range status=$?"
history -c; history | wc -l
mkdir -p /tmp/hibr-cdp/proj/deep
CDPATH=/tmp/hibr-cdp
cd proj; pwd
cd /; CDPATH=/tmp/hibr-cdp/proj:/tmp
cd deep >/dev/null; pwd
CDPATH=/nothing-here; cd /tmp; cd hibr-cdp; pwd
cd ./proj; pwd
rm -rf /tmp/hibr-cdp
printf '2\n\n9\n1\n' | { select c in red green blue; do echo "chose [$c] reply=$REPLY"; [ "$c" = red ] && break; done; } 2>/dev/null
set -- one two
printf '2\n' | { select c; do echo "positional choice: $c"; break; done; } 2>/dev/null
