# Backslash escapes where bash reads them: a printf format's \NNN (a
# leading 0 among its three digits) and \xHH, %b's \NNN and \0NNN and its
# \c that ends everything, echo -e's \0NNN, \xHH and \c, and ${x@E}.
# Compared against bash.
printf '[\101|\0101|\x41|\x4g|\cZ]\n' | od -c
printf '%b|%s\n' '[\101|\0101|\x41|\cZ]' after | od -c
printf '%b-' a 'b\cc' d | od -c
echo -e '[\101|\0101|\x41|\cZ]' more | od -c
echo -e 'a\tb\\c' | od -c
x='[\101|\0101|\x41]'
echo "${x@E}" | od -c
printf '\357\273\277x\n' | od -c

# -- ends the options, with -v and without: the format is the next word.
printf -- '%s|\n' dashes
printf -v pv -- '[%s]' held; echo "$pv"
printf -- '--%s\n' lead
