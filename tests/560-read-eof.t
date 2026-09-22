# read at end of file: the variable is still assigned, and the status is 1.

v=preset; read -r v </dev/null; echo "eof rc=$? v=[$v]"
v=preset; read -r v x </dev/null; echo "eof2 rc=$? v=[$v] x=[$x]"
read -r </dev/null; echo "eof-reply rc=$? REPLY=[$REPLY]"

# a last line with no newline is delivered, and still reports end of file
printf 'partial' | { read -r v; echo "partial rc=$? v=[$v]"; }
printf 'a\nb' | { while read -r l; do echo "loop got [$l]"; done; echo "loop end"; }
printf 'a\nb\n' | { while read -r l; do echo "full got [$l]"; done; echo "full end"; }

# splitting still works, and the trailing field keeps the remainder
printf 'one two three four\n' | { read -r a b c; echo "split [$a][$b][$c]"; }
printf '  pad  \n' | { read -r a; echo "trim [$a]"; }

# -n stops at the count and that is not end of file
printf 'abcdef\n' | { read -rn 3 a; echo "n3 rc=$? a=[$a]"; }
printf 'ab' | { read -rn 5 a; echo "n5-short rc=$? a=[$a]"; }

# -a fills an array, and reports end of file the same way
printf 'x y z\n' | { read -ra arr; echo "arr rc=$? n=${#arr[@]} [${arr[*]}]"; }
printf 'x y z' | { read -ra arr; echo "arr-eof rc=$? [${arr[*]}]"; }

# -d changes the delimiter
printf 'a:b:' | { read -rd : a; echo "d rc=$? a=[$a]"; }
