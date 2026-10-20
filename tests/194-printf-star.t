# printf takes a width or a precision from the next argument with `*`, as
# bash does: a negative width left-justifies, a negative precision is no
# precision, and a missing argument is 0. hibr printed the `*` and the
# conversion letter instead -- `printf '%*s' 5 ab` gave `5s`. And a number
# argument that is not one says so and fails, the way bash's does, having
# printed what it could read; hibr printed 0 and succeeded.
printf '[%*s]\n' 5 ab
printf '[%-*s]\n' 5 ab
printf '[%*s]\n' -5 ab
printf '[%.*s]\n' 2 abcdef
printf '[%.*s]\n' -3 abcdef
printf '[%*.*f]\n' 8 2 3.14159
printf '[%0*d]\n' 5 42
printf '[%*s|%*s]\n' 5 ab 3 xyz
printf '[%*s]\n' 5
printf '[%*c]\n' 3 x
printf -v v '[%*s]' 4 z; echo "$v"
printf '[%*s]\n' x ab 2> /dev/null; echo "star not a number: $?"
printf '[%d]\n' 12abc 2> /dev/null; echo "trailing junk: $?"
printf '[%x]\n' zz 2> /dev/null; echo "hex: $?"
printf '[%g]\n' 1.5x 2> /dev/null; echo "float: $?"
printf '[%d|%d]\n' ' 12' "'A"; echo "blanks and a character: $?"
printf '[%d]\n' ''; echo "empty: $?"
