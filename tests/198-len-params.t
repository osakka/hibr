# ${#@} and ${#*} are how many positional parameters there are, which is
# what $# says -- not the length of the joined parameters (Gitea #152).
# Compared against bash, which makes all three the same number.
set -- a bb ccc
echo "three:        [${#@}] [${#*}] [$#]"
echo "unquoted:     ${#@} ${#*}"

# Inside a function it is the function's own count, as $# is.
f() { echo "in f:         [${#@}] [${#*}] [$#]"; }
f x y
g() { echo "no args:      [${#@}] [${#*}] [$#]"; }
g

set --
echo "none:         [${#@}] [${#*}] [$#]"

# A parameter with a blank in it counts as one, which is the whole point:
# the old answer was strlen of the join, so this read 5.
set -- "a b" c
echo "with a blank: [${#@}] [${#*}] [$#]"

# IFS joins $* and has nothing to do with counting either of them.
IFS=:
set -- a bb ccc
echo "IFS=:         [${#@}] [${#*}]"
IFS=
echo "IFS empty:    [${#@}] [${#*}]"
unset IFS

# One parameter's own length is untouched, and so is an array's count.
set -- alpha beta
echo "a param:      [${#1}] [${#2}]"
arr=(p qq rrr)
echo "an array:     [${#arr[@]}] [${#arr[*]}] [${#arr[2]}]"
echo "a string:     [${#IFS}] [${#arr}]"

# The count is a number wherever a number goes.
set -- a b c d
echo "arithmetic:   $(( ${#@} * 2 ))"
[ "${#@}" -eq 4 ] && echo "test agrees"
case ${#*} in 4) echo "case agrees";; *) echo "case does not";; esac
for i in ${#@} ${#*}; do printf 'word [%s] ' "$i"; done
echo
printf 'printf [%s] [%s]\n' "${#@}" "${#*}"
