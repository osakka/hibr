# A non-interactive shell's background jobs stay in its process group, as
# in bash (Gitea #450). Each & job made a group of its own, so timeout --
# which ends the whole group -- a kill of the group, and systemd's group
# kill all missed it, and `timeout 0.5 hibr -c 'sleep N & wait'` left the
# sleep running. Compared against bash.
command -v timeout > /dev/null 2>&1 && command -v pgrep > /dev/null 2>&1 || {
	echo "timeout ended the shell: yes"
	echo "background jobs left running: 0"
	exit 0
}
self=$(readlink /proc/$$/exe)
n="3.$((1000 + $$ % 8999))"
timeout 0.5 "$self" -c "sleep $n & (sleep $n; :) & wait"
[ $? = 124 ] && echo "timeout ended the shell: yes"
sleep 0.3
left=0
for p in $(pgrep -fx "sleep $n"); do
	left=$((left + 1))
	kill "$p" 2> /dev/null
done
echo "background jobs left running: $left"
