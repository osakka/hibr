# A script runs a command at a time, as it is read, the way bash does:
# a syntax error late in it stops it there, after what came before has run.
# checkfirst (and agent mode) parses the whole text first instead, so a
# script that does not parse runs none of it.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
d=$(mktemp -d)
cd "$d"

printf 'echo ran\nf() { echo f; }\nif\n' > late.sh
for mode in "" --checkfirst; do
  echo "=== ${mode:-as read}"
  "$h" $mode late.sh 2>&1; echo "file: $?"
  cat late.sh | "$h" $mode 2>&1; echo "pipe: $?"
  "$h" $mode < late.sh 2>&1; echo "redirect: $?"
  "$h" $mode -c "$(cat late.sh)" 2>&1; echo "-c: $?"
  "$h" $mode -c '. ./late.sh; echo "source: $?"; f' 2>&1
  "$h" $mode -n late.sh 2>&1; echo "-n: $?"
done
echo "=== agent mode checks first"
"$h" --agent late.sh 2>&1; echo "agent: $?"
"$h" --agent -c 'set +o checkfirst; . ./late.sh' 2>&1; echo "agent, checkfirst off: $?"
echo "=== the option is an option"
"$h" -c 'shopt checkfirst; set -o checkfirst; shopt checkfirst'

echo "=== a read takes the script's next line"
printf 'read x\nhello\necho "got $x"\necho "line $LINENO"\n' > rd.sh
cat rd.sh | "$h"
"$h" < rd.sh
{
  echo 'big() {'
  i=0
  while [ "$i" -lt 300 ]; do echo "  : $i"; i=$((i + 1)); done
  echo '}'
  echo 'read y; echo "got $y"'
  echo 'data'
  echo 'echo "line $LINENO"'
} > big.sh
"$h" < big.sh

echo "=== eval and source stop where their text stops parsing"
"$h" -c 'eval "echo e1
fi"; echo "eval: $?"' 2>&1
echo "=== stopping keeps its status"
printf 'echo one\nexit 7\necho never\n' | "$h"; echo "exit: $?"
printf 'set -e\nfalse\necho never\n' | "$h" 2>&1; echo "errexit: $?"

cd /
rm -rf "$d"
