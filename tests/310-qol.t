ls /definitely-nope &> /tmp/hibr-qol-a
echo "combined rc=$?"
grep -c . /tmp/hibr-qol-a
rm -f /tmp/hibr-qol-a
cat <<< "here string works"
set -C
rm -f /tmp/hibr-qol-b
echo one > /tmp/hibr-qol-b
echo two 2>/dev/null > /tmp/hibr-qol-b
echo "noclobber blocked=$?"
echo three >| /tmp/hibr-qol-b
echo "override=$? content=$(cat /tmp/hibr-qol-b)"
set +C
rm -f /tmp/hibr-qol-b
exec {slot}>/tmp/hibr-qol-c
echo "named fd is $((slot >= 10))"
echo via-slot >&$slot
exec {slot}>&-
cat /tmp/hibr-qol-c
rm -f /tmp/hibr-qol-c
printf 'a\\b c\n' | { read -r v w; echo "raw=[$v] second=[$w]"; }
printf 'abcdef' | { read -n 3 x; echo "chars=[$x]"; }
read -t 1 y </dev/null; echo "timeout=$?"
echo(){ echo SHADOW; }
command echo command-bypass
builtin echo builtin-bypass
unset echo
umask | grep -qE '^0?[0-7]{3,4}$' && echo "umask format ok"
set -- -a -b value -c
while getopts "abc:" opt; do
  case "$opt" in
    a) echo "flag a" ;;
    b) echo "flag b" ;;
    c) echo "opt c" ;;
  esac
done
x=abcdefg
echo "sub: ${x:2:3} ${x: -3} ${x:5} ${x:1:-2}"
a=(a b c d e)
echo "slice: [${a[@]:1:2}] [${a[@]: -2}]"
set -- "${a[@]:3}"; echo "slice fields=$#"
w=Hello; echo "case: ${w,} ${w,,} ${w^} ${w^^}"
[ -n "$PPID" ] && [ -n "$HOSTNAME" ] && [ "$UID" = "$(id -u)" ] && echo "special vars ok"
r1=$RANDOM; r2=$RANDOM; [ "$r1" != "$r2" ] && echo "random varies"
f(){ local arr=(p q r); echo "local array ${#arr[@]}"; }; f; echo "outside=[${arr[*]}]"
fn one() { ret one; }; one; x := true; echo "stale=[$x]"
y := str upper quiet; echo "bind=$y"
b=(1 2 3); unset b[1]; echo "unset: ${b[*]} n=${#b[@]}"
printf -v pv "%04d" 42; echo "printf -v=$pv"
echo -e "esc\tape"
echo -ne "no newline"; echo
sleep 0.05 & wait $!; echo "wait pid=$?"
