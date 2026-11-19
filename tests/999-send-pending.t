# send -p writes what a descriptor takes now and never waits: the rest is
# kept for it, RET says how much, and status 3 says some is still waiting.
# The desktop answers app processes this way (wm/rtserve.hibr, ADR 0050):
# a plain send to an app that has stopped reading waited until the app went
# -- 7.7 s, measured -- with the whole desktop stopped behind it.

T=/tmp/hibr-sendp-$$
mkdir -p "$T"
listen -b "$T/s" L
mkfifo "$T/drain" "$T/gone"
( exec {f}<> "/dev/unix$T/s"; read -r -t 20 x < "$T/drain"; head -c $((400 * 1001)) <&$f > "$T/got"; read -r -t 20 x < "$T/gone" ) &
accept -t 10 "$L" c || echo "no connection"
line=$(printf 'x%.0s' {1..1000})
t0=${EPOCHREALTIME/./}
st=0
for ((i = 0; i < 400; i++)); do
	send -p "$c" "$line"
	st=$?
	[ "$st" = 0 ] || break
done
t=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
echo "a reader that never reads: status $st, waited: $([ "$t" -lt 1000 ] && echo no || echo "$t ms")"
q=$RET
for ((j = i + 1; j < 400; j++)); do
	send -p "$c" "$line"
done
echo "the rest is kept: $([ "$RET" -gt "$q" ] && echo yes || echo "no, $RET after $q")"
echo go > "$T/drain"
st=3
for ((k = 0; k < 500 && st == 3; k++)); do
	send -p "$c"
	st=$?
	[ "$st" = 3 ] && sleep 0.01
done
echo "once it reads, a flush empties it: status $st, left $RET"
for ((k = 0; k < 500; k++)); do
	[ "$(wc -c < "$T/got")" = $((400 * 1001)) ] && break
	sleep 0.01
done
echo "all of it arrived: $([ "$(wc -c < "$T/got")" = $((400 * 1001)) ] && echo yes || echo "no, $(wc -c < "$T/got") bytes")"
# A second connection, filled until some of it is kept, and then forgotten:
# nothing more of it is written, and the reader ends with less than was sent.
mkfifo "$T/drain2"
( exec {f}<> "/dev/unix$T/s"; read -r -t 20 x < "$T/drain2"; cat <&$f > "$T/got2" ) &
accept -t 10 "$L" c2 || echo "no second connection"
sent=0
for ((i = 0; i < 400; i++)); do
	send -p "$c2" "$line"
	st=$?
	sent=$((sent + 1001))
	[ "$st" = 0 ] || break
done
for ((i = 0; i < 50; i++)); do
	send -p "$c2" "$line"
	sent=$((sent + 1001))
done
send -c "$c2"
echo "send -c: status $?"
send -p "$c2"
st=$?
echo "and a flush after it has nothing to write: status $st, kept $RET"
exec {c2}>&-
echo go > "$T/drain2"
for ((k = 0; k < 500; k++)); do
	[ -s "$T/got2" ] && [ "$(wc -c < "$T/got2")" -gt 0 ] && ! kill -0 $! 2> /dev/null && break
	sleep 0.01
done
wait $!
echo "the reader got less than was sent: $([ "$(wc -c < "$T/got2")" -lt "$sent" ] && echo yes || echo "no, $(wc -c < "$T/got2") of $sent")"
echo go > "$T/gone"
wait
st=0
for ((k = 0; k < 200 && st != 1; k++)); do
	send -p "$c" "after the reader has gone" 2> /dev/null
	st=$?
	[ "$st" = 1 ] || sleep 0.01
done
echo "a reader that has gone: status $st, and no signal ended this"
send -p 2>&1 | head -1
rm -rf "$T"
