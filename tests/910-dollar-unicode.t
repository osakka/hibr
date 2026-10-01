# $'...' names a character by its code point: \u with up to four hex
# digits, \U with up to eight, put in as UTF-8. Compared against bash, in a
# UTF-8 locale, since bash decides by the locale and hibr always writes
# UTF-8.
export LC_ALL=C.UTF-8
show() { printf '%s' "$1" | od -An -tx1; }
show $'│'
show $'éx'
show $'\u41'
show $'\U0001F514'
show $'\U41B'
show $'a┌b┐c'
show $'\uZZ'
show $'\U'
v=$'──'
echo "${#v}"
