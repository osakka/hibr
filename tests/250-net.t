port=$(( 19500 + $$ % 400 ))
fn handle(peer) {
  recv 0 line
  send 1 "echo:$line"
}
listen -n 2 $port handle &
sleep 0.4
exec 3<>/dev/tcp/127.0.0.1/$port
echo "via-path" >&3
read r <&3
echo "path client got: $r"
exec 3>&-
connect 127.0.0.1 $port sock
send $sock "via-builtin"
recv $sock answer
echo "builtin client got: $answer"
wait
echo "server done"
match "$answer" "^echo:(.*)$" M && echo "peeled: ${M[1]}"
