# `read -n N` stops at N characters *or the delimiter, whichever comes
# first*, and `read -N N` is the form that ignores the delimiter and takes
# exactly N (Gitea #107). hibr read N characters regardless and kept the
# delimiter in the value, so the two spellings would have been one command --
# except that -N was not accepted at all.
#
# Compared against bash. Only stdout and the status are compared, so the
# "invalid number" case below asserts the status rather than the message.
d=$(mktemp -d)
f=$d/r
printf 'ab\ncd\nef\n' > "$f"

read -n 10 x < "$f"; printf 'n 10: [%s] status %d\n' "$x" "$?"
read -N 10 x < "$f"; printf 'N 10: [%s]\n' "${x//$'\n'/|}"
read -n 2 x < "$f"; printf 'n 2 (count first): [%s]\n' "$x"

exec 3< "$f"
read -n 10 x <&3
read -n 10 y <&3
printf 'twice on one descriptor: [%s][%s]\n' "$x" "$y"
exec 3<&-

exec 3< "$f"
read -N 3 x <&3
read -n 10 y <&3
printf 'N 3 takes the delimiter with it: [%s][%s]\n' "${x//$'\n'/|}" "$y"
exec 3<&-

read -d : -n 10 x <<< "ab:cd"
printf 'its own delimiter: [%s]\n' "$x"

printf 'ab\ncd\n' | { read -n 10 x; read -n 10 y; printf 'through a pipe: [%s][%s]\n' "$x" "$y"; }
printf 'ab' | { read -n 10 x; printf 'a pipe that ends first: [%s] status %d\n' "$x" "$?"; }

exec 3< "$f"
read -n 0 x <&3
read -n 10 y <&3
printf 'n 0 takes nothing at all: [%s][%s]\n' "$x" "$y"
exec 3<&-

read -N 0 x < "$f"; printf 'N 0: [%s] status %d\n' "$x" "$?"
read -N 99 x < "$f"; printf 'N more than there is: %d characters, status %d\n' "${#x}" "$?"
read -n k x < "$f" 2> /dev/null; printf 'not a number: status %d\n' "$?"

# -N splits nothing, which is the other half of what the form means: the
# whole of what was taken goes to the first name, blanks and all.
read -N 5 a b <<< "x y z";       printf 'N into two names: [%s][%s]\n' "$a" "$b"
read -n 5 a b <<< "x y z";       printf 'n into two names: [%s][%s]\n' "$a" "$b"
read -N 2 a <<< "  x";           printf 'N keeps leading blanks: [%s]\n' "$a"
IFS=, read -N 5 a b <<< "x,y,z"; printf 'N ignores IFS too: [%s][%s]\n' "$a" "$b"
read -N 5 -a arr <<< "x y z";    printf 'N into an array: %d element(s) [%s]\n' "${#arr[@]}" "${arr[0]}"
read -N 3 <<< "x y z";           printf 'N with no name: [%s]\n' "$REPLY"
rm -rf "$d"
