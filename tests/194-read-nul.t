# read drops a NUL byte from what it takes, as bash does, from a pipe and
# from a file alike, unless NUL is the delimiter; and a timeout bounds a
# read from a descriptor that is seekable and never ends -- /dev/zero is
# both, so the block read for a file never saw the deadline and waited for
# ever, growing by a gigabyte a second.
t=$(mktemp)
printf 'a\0b\nc\n' > "$t"
printf 'a\0b\n' | { read x; echo "pipe ${#x} $x"; }
{ read x; echo "file ${#x} $x"; read y; echo "next $y"; } < "$t"
printf 'a\0b\0' | { read -d '' x; echo "delimited $x"; }
printf 'a\0bcd' | { read -N 3 x; echo "exact ${#x} $x"; }
read -t 1 x < /dev/zero
echo "zero $? ${#x}"
rm -f "$t"
