# Expansion's quick paths must give the slow path's answers (Gitea #444),
# compared against bash. A trim tries only the ends that can hold the
# pattern's literal last (or first) byte; an unquoted variable with no glob
# byte and no IFS byte is taken as it is whatever IFS holds -- which also
# stops $RANDOM drawing twice when IFS is set: it was read, put down, and
# read again.
f=/usr/lib/x86_64-linux-gnu/libc.so.6
for p in '*/' '*.' '*\*' '*[/]' '*?' '*.so' '/*' '*/x*' '*\\' '*/*' 'x' '*lib' '*(.)'; do
	printf '%s|%s|%s|%s|%s\n' "$p" "${f#$p}" "${f##$p}" "${f%$p}" "${f%%$p}"
done
g='a*b\c/d.e'
for p in '*\*' '*\\' '\**' '*[*]' '*c' 'a*' '*.e' '\\*'; do
	printf '%s|%s|%s|%s|%s\n' "$p" "${g#$p}" "${g##$p}" "${g%$p}" "${g%%$p}"
done
h='日本/語.txt'
printf '%s|%s|%s\n' "${h##*/}" "${h%.*}" "${h#*本}"
RANDOM=5
x=$RANDOM
RANDOM=5
IFS=:
set -- $RANDOM
y=$1
[ "$x" = "$y" ] && echo "one draw of RANDOM with IFS set"
v="a:b c"
printf '[%s]' $v; echo
IFS=
printf '[%s]' $v; echo
IFS=$'\n'
v="x y"
printf '[%s]' $v; echo
