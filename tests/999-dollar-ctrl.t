# $'...' takes \cX, a control character (X & 0x1f, \c? is DEL), and \E as
# well as \e for escape; an escape it does not know keeps its backslash.
# ${x@E} reads the same escapes; a printf format, %b and echo -e take \E
# too, and keep \c as they had it. Compared against bash, byte for byte.

show() { printf '%s' "$1" | od -An -tx1 | tr -s ' \n' ' '; echo; }

echo "--- \\cX and \\E"
show $'\cA|\E|\e'
show $'\ca\cz\c[\c\\\c]\c^\c_\c?\c1\c '
show $'\cAb'
show $'\c\x'

echo "--- a \\c with nothing after it is kept"
show $'\c'
show $'x\c'

echo "--- escapes bash keeps the backslash on"
show $'\q\z\8\9\xg\ug'
show $'\"\?\\\''
show $'\c\''

echo "--- what was there already"
show $'\n\t\r\a\b\f\v\x41\101\u00e9'

echo "--- \${x@E} takes the same"
x='\cA|\c?|\E|\q|\"|\c'
show "${x@E}"

echo "--- printf and echo -e take \\E, and keep \\c as before"
show "$(printf 'a\Eb\cd|')"
show "$(printf %b 'a\Eb\cd|')"
show "$(echo -e 'a\Eb\cd|')"
show "$(printf '\"\?|')"
show "$(printf %b '\"\?|')"
