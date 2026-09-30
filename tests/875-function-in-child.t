# A function run in the background or as a pipeline stage runs its whole
# body: the fork made for the stage may be replaced by the stage's own
# program, never by the first program somewhere inside a function it calls.
f() { /bin/true; echo "after the program"; return 3; }
f & wait $!; echo "background: $?"
f | cat; echo "pipeline: $?"
out=$(f); echo "substitution: [$out] $?"
g() { sleep 0.01; echo "g went on"; }
g | cat
echo in | { /bin/cat; echo "group went on"; }
e() { eval '/bin/true; echo "eval went on"'; }
e | cat
x=$(/bin/true; echo "substitution went on"); echo "$x"
/bin/sh -c 'exit 4' & wait $!; echo "plain program in the background: $?"
