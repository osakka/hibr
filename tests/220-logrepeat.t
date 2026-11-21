# logtime collapses a repeated diagnostic in its log (Gitea #555): a
# desktop's log is its shell's standard error under logtime, and a slip in a
# drawing callback wrote the same line twenty times a second for days. The
# log is standard error as it was when logtime was turned on, unless that
# was a terminal. A diagnostic sent there is written the first time it comes
# from a place, with that place on the line after it, and is then counted:
# the count is written with the line again once the shell's own clock
# (SECONDS) is a minute on, before a later diagnostic, and at exit -- by
# each process for what it counted itself. Anything else a command's errors
# are sent to gets every line as before, and so does a shell without
# logtime.

d=${TMPDIR:-/tmp}/hibr-lr-$$
mkdir -p "$d"
cd "$d" || exit 1
unstamp() {
	sed -E 's/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}[+-][0-9]{2}:[0-9]{2} /T /; s/ in [0-9]+ s$/ in N s/'
}
run() {
	"$HIBR" "$@" > out 2> log
	echo "status $?"
	unstamp < out
	echo "log:"
	unstamp < log
}

echo "-- without logtime every line is written"
run -c 'for i in 1 2 3; do nosuch_lr; done'

echo "-- what a command's errors are sent to is not the log: every line, no place"
cat > notlog.hibr <<'S'
shopt -s logtime
x=$(for i in 1 2 3; do nosuch_q; done 2>&1)
printf '%s\n' "$x" | sed -E 's/^[^ ]+ /T /'
for i in 1 2 3; do nosuch_r; done 2> errs
sed -E 's/^[^ ]+ /T /' errs
S
run notlog.hibr

echo "-- three errors a frame, five frames: each once, then its count at exit"
cat > flood.hibr <<'S'
shopt -s logtime
frame() { nosuch_a; nosuch_b; nosuch_c; }
for i in 1 2 3 4 5; do frame; done
echo done
S
run flood.hibr

echo "-- the same text from two places is two errors"
cat > place.hibr <<'S'
shopt -s logtime
nosuch_p
nosuch_p
S
run place.hibr

echo "-- what went to /dev/null first is still written where it is seen"
cat > null.hibr <<'S'
shopt -s logtime
f() { nosuch_n; }
f 2> /dev/null
f
S
run null.hibr

echo "-- each child writes its own first line; the parent's count comes once"
cat > child.hibr <<'S'
shopt -s logtime
f() { nosuch_k; }
f
f
( f )
x=$(f 2>&1)
echo "captured: ${x#* }"
f | cat
f &
wait
( f; f; f )
echo end
S
run child.hibr

echo "-- a minute on the shell's clock writes the count with the line"
cat > clock.hibr <<'S'
shopt -s logtime
f() { nosuch_t; }
f
f
f
SECONDS=1000
f
f
SECONDS=0
f
echo end
S
run clock.hibr

echo "-- from the command line there is no place to name, only the count"
run -c 'shopt -s logtime; for i in 1 2 3; do nosuch_c; done'

echo "-- three hundred different errors: a count is never lost to the bound"
cat > many.hibr <<'S'
shopt -s logtime
for ((i = 0; i < 300; i++)); do
	for j in 1 2; do
		cd "/nonexistent_lr_$i"
	done
done
S
"$HIBR" many.hibr 2> log
echo "written first: $(grep -c ' at many.hibr:4$' log)"
echo "counted: $(grep -c ' at many.hibr:4, came once more in ' log)"
echo "lines: $(wc -l < log), errors named: $(grep -o 'nonexistent_lr_[0-9]*' log | sort -u | wc -l)"
c0=$(grep -n 'nonexistent_lr_0:' log | sed -n 2p | cut -d: -f1)
f299=$(grep -n 'nonexistent_lr_299:' log | head -1 | cut -d: -f1)
[ "$c0" -lt "$f299" ] && echo "the oldest is said when the bound is reached" || echo "the oldest waited: $c0 $f299"

echo "-- agent mode's JSON is never collapsed"
run -c 'set -o agent; set -o logtime; for i in 1 2; do nosuch_j; done'

cd / && rm -rf "$d"
