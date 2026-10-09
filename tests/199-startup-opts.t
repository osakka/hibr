# Every option `set` takes, hibr takes on the command line too -- bundled
# (-ex), in the + form, and as `-o name` -- and -i, -s and -- behave as in
# bash (Gitea #153). Before 0.99.122 each of them was read as the name of a
# script to open: `hibr -x script` said "-x: No such file or directory".
#
# $SH is the shell under test for hibr's run and the reference for bash's,
# so every line below runs its own shell.
#
# $- itself is tests/201-dash-flags.t, recorded, because hibr's letters are
# its own options: bash's include h and B, which hibr has no option for.
d=/tmp/hibr-startup-$$
mkdir -p "$d"
printf 'echo "script ran, $1=$1"\n' > "$d/s.sh"

echo "--- errexit, four ways of asking"
"$SH" -e -c 'false; echo not reached'; echo "status $?"
"$SH" -ex -c 'false; echo not reached' 2>/dev/null; echo "bundled status $?"
"$SH" -o errexit -c 'false; echo not reached'; echo "-o status $?"
"$SH" +e -c 'false; echo reached'; echo "+e status $?"

echo "--- nounset: nothing is printed and the shell stops"
echo "printed $("$SH" -u -c 'echo "[${nope}]"; echo not reached' 2>/dev/null | wc -c) bytes"

echo "--- xtrace traces"
"$SH" -x -c 'echo traced' 2>&1 | grep -c '^+ echo'

echo "--- and the option is on inside"
"$SH" -e -c 'case $- in *e*) echo errexit on;; *) echo errexit off;; esac'
"$SH" -c 'case $- in *e*) echo errexit on;; *) echo errexit off;; esac'

echo "--- noexec runs nothing"
"$SH" -n -c 'echo not reached'; echo "status $?"

echo "--- a command, a name and arguments"
"$SH" -c 'echo "$0 saw $# [$1] [$2]"' myname one two

echo "--- commands on standard input"
echo 'echo "stdin ran, $# args [$1]"' | "$SH" -s one two
echo 'echo "no -s needed, $# args"' | "$SH"

echo "--- -s does not open a file named after it"
echo 'echo "stdin ran, [${1##*/}] is a word not a file"' | "$SH" -s "$d/s.sh"

echo "--- -- ends the options"
"$SH" -- "$d/s.sh" arg1
"$SH" -x -- "$d/s.sh" arg2 2>/dev/null

echo "--- a login shell with nothing of its own to read"
HOME=$d "$SH" -lc 'echo login ran'

echo "--- interactive on purpose, and not"
HOME=$d "$SH" -i -c 'case $- in *i*) echo interactive;; *) echo not;; esac'
"$SH" -c 'case $- in *i*) echo interactive;; *) echo not;; esac'

echo "--- an option nobody has, and a -c with nothing to run"
"$SH" -q -c 'echo not reached' 2>/dev/null; echo "status $?"
"$SH" -c 2>/dev/null; echo "status $?"

rm -rf "$d"
