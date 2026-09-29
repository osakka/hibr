# RANDOM and SECONDS are computed, and assigning to them does what it does
# in bash -- seeds the generator, restarts the count -- rather than
# replacing them with a plain variable that reads the same thing for ever.
# Compared against bash, so only what both shells promise is printed: the
# generators differ, the behaviour does not.
RANDOM=7; a=$RANDOM; b=$RANDOM
RANDOM=7; c=$RANDOM; d=$RANDOM
[ "$a$b" = "$c$d" ] && echo "reseeding repeats the sequence"
[ "$a" != "$b" ] && echo "and it still changes from one read to the next"
[ "$a" -ge 0 ] && [ "$a" -lt 32768 ] && echo "in range"
[ "$a" != 7 ] && echo "not the seed itself"
SECONDS=100
s=$SECONDS
[ "$s" -ge 100 ] && [ "$s" -le 101 ] && echo "SECONDS counts on from what it was set to"
