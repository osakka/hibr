fn serve(peer) {
  recv 0 line
  while recv 0 h; do str len "$h" n; [ "$n" -eq 0 ] && break; done
  send -r 1 "HTTP/1.0 200 OK"
  send -r 1 ""
  send 1 "body for $line"
  send 1 "second line"
}
port=$(( 19600 + $$ % 300 ))
listen -n 2 $port serve &
sleep 0.3
mod load ./build/mods/http.so
cat </dev/http/127.0.0.1/$port/hello
echo "--- as lines:"
while read l; do echo "[$l]"; done </dev/http/127.0.0.1/$port/second
wait
mod drop http
cat 2>/dev/null </dev/http/127.0.0.1/$port/x | head -1; echo "after drop: $?"
echo "--- map API:"
scores=([a]=10 [b]=20 [c]=12)
mod load ./build/mods/http.so
msum scores; echo "slot=$RET"
try oops "module says no"
echo "err=$ERR msg=[$ERRMSG]"
