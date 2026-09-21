port=$(( 19000 + $$ % 400 ))

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
