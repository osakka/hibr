# read without -r: a backslash quotes the next character -- removed, and the
# character is never a separator when the line is split -- a backslash and a
# newline join the next line on, and a backslash at the very end of input is
# dropped. With -r, read takes every byte as it is. hibr's read was always
# raw, so -r did nothing (Gitea #422). Each case runs through a pipe, which
# is read a byte at a time, and the file cases through a seekable descriptor,
# read in blocks and put back. Compared against bash.

echo "--- an escaped separator stays in its field"
printf 'a\\ b c\n' | { read x y; echo "[$x][$y]"; }
printf 'a\\ b c\n' | { read -r x y; echo "[$x][$y]"; }
printf 'a b\\\\ c\n' | { read x y; echo "[$x][$y]"; }
printf 'a\\:b:c\n' | { IFS=: read x y; echo "[$x][$y]"; }
printf 'k\\=v\n' | { IFS='=' read a b; echo "[$a][$b]"; }
printf 'a\\\tb\tc\n' | { IFS=$'\t' read x y; echo "[$x][$y]"; }
printf ' \\ x\n' | { read x y; echo "[$x][$y]"; }

echo "--- the last name keeps the rest, unescaped"
printf 'a b\\ \n' | { read x y; echo "[$x][$y]"; }
printf 'a b c\\ \n' | { read x y; echo "[$x][$y]"; }
printf 'a  b\\  \n' | { read x y; echo "[$x][$y]"; }
printf 'a b \\ c  \n' | { read x y; echo "[$x][$y]"; }
printf '\\ a \\ \n' | { read x; echo "[$x]"; }
printf 'a:b\\:\n' | { IFS=: read x y; echo "[$x][$y]"; }
printf 'a:b:\\:\n' | { IFS=: read x y; echo "[$x][$y]"; }
printf 'a b\\\\\n' | { read x y; echo "[$x][$y]"; }

echo "--- REPLY is not split, and is unescaped"
printf '\\ a \\ \n' | { read; echo "[$REPLY]"; }
printf 'x\\\\y\n' | { read; echo "[$REPLY]"; }
printf 'x\\y\n' | { read -r; echo "[$REPLY]"; }
printf '\\n\\t\\\\\n' | { read x; echo "[$x]"; }

echo "--- a backslash and a newline continue the line"
printf 'a b \\\n c d\n' | { read x y; echo "[$x][$y]"; }
printf 'a\\\nb\\\nc\n' | { read x; echo "[$x]"; }
printf 'a\\\\\\\nb\n' | { read x; echo "[$x]"; }
printf 'a\\\\\nb\n' | { read x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\\nb\n' | { read -r x; echo "[$x]"; read -r y; echo "[$y]"; }
printf 'one\\\ntwo three\n' | { read -a w; echo "${#w[@]} ${w[*]}"; }
printf 'x y\\\n' | { read a b; echo "[$a][$b] $?"; }
printf '\\\n\n' | { read x; echo "[$x] $?"; }

echo "--- a backslash at the end of input is dropped"
printf 'abc\\' | { read x; echo "[$x] $?"; }
printf 'h\\\\' | { read x; echo "[$x] $?"; }
printf 'a\\\n' | { read x; echo "[$x] $?"; }
printf 'abc\\' | { read -r x; echo "[$x] $?"; }

echo "--- -d: an escaped delimiter is kept, and a newline still continues"
printf 'a\\:b:c\n' | { read -d : x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\\nb:c\n' | { read -d : x; echo "[$x]"; }
printf 'a\\:b:c\n' | { read -r -d : x; echo "[$x]"; }

echo "--- -n and -N count what is left once the backslashes are gone"
printf 'a\\bcdef\n' | { read -n 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\bcdef\n' | { read -r -n 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\bcdef\n' | { read -N 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\\nbcdef\n' | { read -N 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'ab\\\ncdef\n' | { read -n 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'ab\\c\n' | { read -n 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'ab\\\\c\n' | { read -n 3 x; echo "[$x]"; read y; echo "[$y]"; }
printf 'a\\bc' | { read -n 2 x; echo "[$x]"; }
printf 'a\\' | { read -n 5 x; echo "[$x] $?"; }
printf 'a\\' | { read -N 3 x; echo "[$x] $?"; }
printf 'ab\\' | { read -N 2 x; echo "[$x] $?"; }
printf 'a\\\nb\n' | { read -r -N 4 x; echo "[$x]"; }

echo "--- -a splits on what is not escaped"
printf 'a:b\\:c:d\n' | { IFS=: read -a arr; echo "${#arr[@]} [${arr[1]}]"; }
printf '\\ a\\ b \\  c\n' | { read -a w; echo "${#w[@]} [${w[0]}][${w[1]}][${w[2]}]"; }
printf 'a\\ b c\n' | { read -r -a w; echo "${#w[@]} [${w[0]}]"; }

echo "--- an empty or unset IFS"
printf 'a\\ b c\n' | { IFS= read x y; echo "[$x][$y]"; }
printf ' a\\ b \n' | { IFS= read x; echo "[$x]"; }
printf 'a\\ b c\n' | { unset IFS; read x y; echo "[$x][$y]"; }

echo "--- a file, read in blocks, and -u"
f=${TMPDIR:-/tmp}/hibr-read-bs-$$
printf 'a\\ b c\\\n d\nnext line\n' > "$f"
{ read x y; echo "[$x][$y]"; read z; echo "[$z]"; } < "$f"
{ read -r x y; echo "[$x][$y]"; read z; echo "[$z]"; } < "$f"
exec 3< "$f"
read -u 3 x y; echo "[$x][$y]"
read -u 3 z; echo "[$z]"
exec 3<&-
{ read -n 4 x; echo "[$x]"; read y; echo "[$y]"; } < "$f"
{ read -N 6 x; echo "[$x]"; read y; echo "[$y]"; } < "$f"
{ read -d c x; echo "[$x]"; read y; echo "[$y]"; } < "$f"
printf 'p\\:q:r\\\ns:t\n' > "$f"
{ read -d : x; echo "[$x]"; read -d : y; echo "[$y]"; read z; echo "[$z]"; } < "$f"
n=0
while read l; do n=$((n + 1)); echo "$n [$l]"; done < "$f"
rm -f "$f"

echo "--- a here-string"
read x y <<< 'one\ two three'
echo "[$x][$y]"
read -r x y <<< 'one\ two three'
echo "[$x][$y]"
