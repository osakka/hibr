# A port from $$, and then **the next free one**, which is the difference
# between a test and a coin toss: 19000 + $$ % 400 is 400 possible ports,
# and anything else on the machine may already hold one. This failed a
# release gate on 19278, held by an unrelated long-running server that had
# nothing to do with the suite -- and passed every run afterwards, which is
# exactly what a collision looks like and exactly what must not be called a
# flake. Bound here rather than left to luck, so the rest of the test can
# say `bound: 0` and mean it.
port=$(( 19000 + $$ % 400 ))
tries=0
while [ "$tries" -lt 40 ]; do
  listen -b "$port" PROBE 2> /dev/null && { exec {PROBE}>&-; break; }
  port=$(( port + 1 ))
  [ "$port" -gt 19999 ] && port=19000
  tries=$(( tries + 1 ))
done

listen -b
echo "no port: $?"
listen -b -f 1234
echo "with -f: $?"
listen -b -n 2 1234
echo "with -n: $?"

listen -b $port LFD
echo "bound: $?"
echo "fd is out of the way: $(( LFD >= 10 ))"
echo "result slot matches: $(( RET == LFD ))"

fn srv() {
  accept $LFD C
  recv $C line
  send $C "echo:$line"
}
srv &
sleep 0.4
connect 127.0.0.1 $port sock
send $sock "through a bound socket"
recv $sock answer
echo "client got: $answer"
wait
echo "server done"
