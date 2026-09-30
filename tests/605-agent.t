# Agent mode, for a script a program runs rather than a person: errors are
# one line of JSON each, an unset variable is an error, an expansion never
# splits or globs, and HIBR_TIMEOUT bounds each foreground process.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
d=$(mktemp -d)
cd "$d"

printf 'echo start\necho "$missing"\necho never\n' > unset.sh
"$h" --agent unset.sh 2>&1
echo "status $?"

printf 'echo ok\nif true; then\n  fi\n' > syntax.sh
"$h" --agent syntax.sh 2>&1
echo "status $?"

"$h" --agent -c 'for w in $(echo a b); do echo "[$w]"; done'
echo hi | "$h" --agent -c 'read -r x; echo "piped input still arrives: $x"'
"$h" -c 'set -o agent; echo "agent is $(set -o | grep agent)"; echo "$gone"' 2>&1

HIBR_TIMEOUT=1 "$h" --agent -c 'sleep 5; echo "status $?"' 2>&1
HIBR_TIMEOUT=1 "$h" --agent -c 'sleep 0.1; echo "in time: status $?"' 2>&1
"$h" -c 'echo "$missing" > /dev/null; ls /no/such/dir 2> /dev/null; x=$(nosuchcmd-xyz 2>&1); echo "outside agent mode: plain text"'
"$h" -c 'eval "if then"' 2>&1
echo "a syntax error's status is 2: $?"

cd /
rm -rf "$d"
