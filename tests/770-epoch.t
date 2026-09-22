# bash 5's clock variables. The values change, so the shape is what is
# checked: EPOCHREALTIME has six digits after the point, EPOCHSECONDS is the
# whole part of it, and time does not run backwards between two reads.

a=$EPOCHREALTIME
b=$EPOCHREALTIME
[[ $a =~ ^[0-9]+\.[0-9]{6}$ ]] && echo "realtime has microseconds"
[ "${b/./}" -ge "${a/./}" ] && echo "and does not go backwards"
s=$EPOCHSECONDS
[[ $s =~ ^[0-9]+$ ]] && echo "seconds is a whole number"
d=$(( s - ${a%.*} ))
[ "$d" -ge 0 ] && [ "$d" -le 1 ] && echo "and agrees with realtime"
[ $(( EPOCHSECONDS > 1000000000 )) = 1 ] && echo "arithmetic sees it"
