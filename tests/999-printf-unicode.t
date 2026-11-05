# \u and \U in printf's format, in %b and in echo -e, as bash reads them:
# compared against bash, byte for byte.
printf 'é|\U0001F600|\u41|\U41\n' | od -An -c
printf '%b|%s\n' 'é\U0001F600' 'é' | od -An -c
echo -e 'é\U0001F600' | od -An -c
printf 'ሴ5|\U000000411\n' | od -An -c
printf '\u0000x\ud800\n' | od -An -c
printf '\U110000\U00200000\U7FFFFFFF\n' | od -An -c
printf '\UFFFFFFFFx\n' | od -An -c
printf '\uZ|a\U\n' 2> /dev/null | od -An -c
printf '%b\n' '\uZ' 2> /dev/null | od -An -c
echo -e '\uZ' | od -An -c
printf '\uZ' 2> /dev/null; echo " status $?"
