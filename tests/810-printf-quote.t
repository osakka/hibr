# printf's numeric conversions take 'c as the code of the character c, as
# POSIX asks. ASCII only here, because the bash this is compared with may be
# running in the C locale, where a multibyte character gives its first byte.

printf '%d %d %d\n' "'A" "' " '"z'
printf '%x %o %u\n' "'A" "'A" "'A"
printf '%d\n' "'"
printf '%03d|%-4d|\n' "'a" "'b"
c=Q
printf -v n '%d' "'$c"
echo "$c is $n"
