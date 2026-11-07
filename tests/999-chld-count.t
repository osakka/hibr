# A CHLD trap runs once for each child, whatever arrives as one signal. Recorded:
# bash loses and doubles some of a burst, so it is no reference here.
c=0; trap 'c=$((c+1))' CHLD
(exit 1) & (exit 2) & (exit 3) & wait; echo "three: $c"
c=0; for i in 1 2 3 4 5; do /bin/true & done; wait; echo "five: $c"
c=0; /bin/true; /bin/true; echo "two in turn: $c"
c=0; /bin/true | /bin/true; echo "a pipeline: $c"
c=0; x=$(/bin/true); echo "a substitution: $c"
c=0; (exit 1) & sleep 0.2; echo "one of each: $c"
trap - CHLD
c=0; (exit 1) & wait; echo "after clearing it: $c"

# A shell given INT ignored hands that on, whatever it handles it with itself.
(trap '' INT; "$HIBR" -i -c "sh -c 'kill -INT \$\$; echo survived'" 2> /dev/null)
