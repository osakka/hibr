# times, as bash prints it: the shell's user and system time, then its
# children's, minutes and seconds to the millisecond (Gitea #343). The
# values change from run to run, so what is recorded is their shape -- and
# that := takes the same two lines into the slot and prints nothing.
re='^[0-9]+m[0-9]+\.[0-9]{3}s [0-9]+m[0-9]+\.[0-9]{3}s$'
n=0
while IFS= read -r l; do
  [[ $l =~ $re ]] && n=$((n + 1))
done <<< "$(times)"
echo "printed lines in shape: $n"
x := times
n=0
while IFS= read -r l; do
  [[ $l =~ $re ]] && n=$((n + 1))
done <<< "$x"
echo "bound lines in shape: $n"
out=$(x := times)
echo "printed while bound: [${out}]"
i=0
while [ "$i" -lt 30000 ]; do i=$((i + 1)); done
x := times
read -r u _ <<< "$x"
[ "$u" != 0m0.000s ] && echo "a busy loop shows as the shell's own time"
command true
( : )
x := times
n=0
while IFS= read -r l; do n=$((n + 1)); done <<< "$x"
echo "still two lines after children: $n"
