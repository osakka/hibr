up() { while recv 0 l; do send 1 "UP:$l"; done; }
dn() { while recv 0 l; do send 1 "dn:$l"; done; }

coproc a up
echo "named ends: in>=10 $(( a[in] >= 10 )) out>=10 $(( a[out] >= 10 ))"
echo "bash style agrees: $(( a[0] == a[in] )) $(( a[1] == a[out] ))"
echo "pid recorded: $(( a_PID > 0 ))"

send ${a[out]} one
recv ${a[in]} r1
echo "round trip: $r1"
send ${a[out]} two
recv ${a[in]} r2
echo "again: $r2"

coproc b dn
send ${b[out]} three
recv ${b[in]} r3
send ${a[out]} four
recv ${a[in]} r4
echo "two at once: $r3 and $r4"

echo "keys: ${!a[*]}"
coproc up
echo "default name: $(( COPROC[in] >= 10 ))"
send ${COPROC[out]} five
recv ${COPROC[in]} r5
echo "default round trip: $r5"

coproc
echo "no command: $?"
