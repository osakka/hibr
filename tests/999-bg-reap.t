# A script that starts background jobs and never waits for them must not
# collect zombies: each finished job is reaped before the next one starts.
for i in 1 2 3 4 5 6 7 8 9 10; do
	( : ) &
	sleep 0.05
done
sleep 0.2
( : ) &
sleep 0.2
z=0
for f in /proc/[0-9]*/stat; do
	read -r l 2> /dev/null < "$f" || continue
	r=${l##*) }
	set -- $r
	[ "$1" = Z ] && [ "$2" = $$ ] && z=$((z + 1))
done
[ "$z" -le 1 ] && echo "at most one finished job unreaped"

# A job already reaped keeps its status for wait.
( exit 3 ) &
a=$!
sleep 0.2
( : ) &
wait "$a"
echo "status $?"
