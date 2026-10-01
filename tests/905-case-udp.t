# Case conversion knows the cased letters of Latin, Greek, Cyrillic and
# Armenian, whatever the locale (hibr reads text as UTF-8, ADR 0021); bytes
# that are not UTF-8 pass through. recv takes a whole datagram on UDP.
x=élan; y=ÉLAN
echo "${x^} ${x^^} ${y,} ${y,,} ${x@U} ${y@L} ${x@u}"
z="straße ǅ ÿ ıi İ ωμέγα привет ąčę"
echo "${z^^}"
echo "${z,,}"
declare -u up=éa; echo "$up"
str upper "élan" u; str lower "ÉLAN" l; echo "$u $l"
b=$'\xe9x'; printf '%s\n' "${b^^}" | od -c | head -1
port=$(( 20000 + $$ % 20000 ))
listen -u -b "$port" L
exec 3<>/dev/udp/127.0.0.1/$port
send 3 hello; recv "$L" got; echo "recv a line: [$got]"
send -n 3 abcdef; recv -n 3 "$L" part; echo "recv -n 3: [$part]"
try eval 'echo $(( 1 / 0 ))' 2>/dev/null
echo "an arithmetic error inside try fails only that command: ERR=$ERR"
