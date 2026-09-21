sleep 0.2 &
echo "started $(test -n "$!" && echo yes)"
wait
echo "waited"
sleep 0.1 &
sleep 0.1 &
wait
echo "all done"
