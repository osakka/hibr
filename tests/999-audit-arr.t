# Arrays and text (Gitea #464), each checked against 0.99.427.
#
# arr rebuilt the whole array for every operation, so a push loop was
# quadratic and a push renumbered every key: ([a]=1 [b]=2) became 0, 1, 2.
# A push appends one past the highest numeric key and leaves the others,
# as a+=(x) does; a scalar becomes element 0 first.
a=([2]=x [7]=y)
arr push a p q
declare -p a
m=([a]=1 [b]=2)
arr push m z
declare -p m
s=hi
arr push s t
declare -p s
arr push fresh 1 2
declare -p fresh
u=(b a b c a)
arr uniq u
echo "uniq: ${u[*]}"
arr len u n
echo "len: $n"
arr contains u c && echo "contains c"
arr contains u zz || echo "does not contain zz"
readonly r=(1)
arr push r 2 2> /dev/null
echo "push onto a readonly array: $?"
b=([9]=a [10]=b [99]=c [007]=d)
b+=(e)
declare -p b

# str pad padded with the first byte of a multibyte character, writing
# broken UTF-8; it takes the whole character, and a wide one counts two.
x := str pad ab 5 "é"
echo "[$x]"
x := str pad ab -5 "é"
echo "[$x]"
x := str pad ab 7 "漢"
echo "[$x]"

# A control character in a JSON string is a \u escape.
declare -A j
j[k]=$'a\x01b\x1fc'
json emit j
