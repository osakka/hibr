# An app's own console (console cells, ADR 0050): a view drawn through a
# pane of another name, where each view starts, a frame drawn with no clear
# (touch), and the refusals -- a pane name another view has, a name no view
# has. Its records go to nothing; what is checked is what the verbs say,
# and under the sanitizers that the console's own memory is sound.
mod load ./build/mods/console.so
exec {fd}> /dev/null
console cells open "$fd" && echo opened
console cells view p7 1 3 12 w7 && echo "p7 through w7"
console cells view s1 1 1 8 && echo "s1 its own"
console cells view q7 1 2 5 w7 2> /dev/null || echo "q7 through w7 refused: w7 is p7's"
console cells view z 1 0 5 2> /dev/null || echo "a view of no size refused"
a := console cells at p7
b := console cells at s1
echo "p7 at $a, s1 at $b"
[ "$b" -gt "$a" ] && echo "s1 below p7"
console cells at nobody 2> /dev/null || echo "at: no such view"
console cells touch nobody 2> /dev/null || echo "touch: no such view"
console pane origin w7 1 4
console put -p w7 1 4 "rows"
console cells touch p7 && echo touched
console flush && echo flushed
console cells view p7 2 4 20 w7 && echo "p7 again, larger"
a := console cells at p7
b := console cells at s1
[ "$b" -gt "$a" ] && echo "s1 still below p7"
console cells forget p7 && echo "p7 forgotten"
console cells view q7 1 2 5 w7 && echo "q7 through w7 now"
console cells clear q7 && console flush && echo cleared
console cells forget q7
console cells forget s1
echo done
