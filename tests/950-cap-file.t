# $(< file) is the file's contents, read without running anything
d=/tmp/hibr-capf-$$
mkdir -p "$d"
printf 'one\ntwo\n\n\n' > "$d/a b.txt"
t=$(< "$d/a b.txt")
echo "[$t]"
f=$d/a\ b.txt
t=$( <"$f" )
echo "${#t}"
x=$(< "$d/missing" 2> /dev/null)
echo "status $? [$x]"
t=$(< "$d/a b.txt" cat)
echo "with a command it is a command: [$t]"
rm -rf "$d"
