# time is a reserved word in front of a pipeline, as in bash, not a builtin
# that timed one simple command: `time { true; }` was a syntax error and
# `time a | b` timed `a` alone. It times the whole pipeline, compound
# commands included, reports on the shell's standard error once the
# pipeline is done -- so a redirection on the pipeline does not reach the
# report -- reads TIMEFORMAT, and takes -p for POSIX's three lines. Only
# what is not a time is compared: statuses, the output of what was timed,
# the number of lines a report has, and the shape of its figures.
# Compared against bash.

time { echo a; echo b; } 2>/dev/null
{ time { echo c; } 2>/dev/null; } 2>/dev/null

{ time false; } 2>/dev/null
echo "false: $?"
{ time true | false; } 2>/dev/null
echo "pipeline: $?"
{ time ! false; } 2>/dev/null
echo "time !: $?"
{ ! time true; } 2>/dev/null
echo "! time: $?"
{ time (exit 5); } 2>/dev/null
echo "subshell: $?"
{ time while false; do :; done; } 2>/dev/null
echo "loop: $?"
if { time false; } 2>/dev/null; then echo then; else echo else; fi
{ time x=1; } 2>/dev/null
echo "x=$x"
{ time echo piped | tr a-z A-Z; } 2>/dev/null

echo "default: $({ time true; } 2>&1 | wc -l)"
echo "-p: $({ time -p true; } 2>&1 | wc -l)"
echo "alone: $({ time; } 2>&1 | wc -l)"
echo "time time: $({ time time true; } 2>&1 | wc -l)"
{ time -p true; } 2>&1 | sed 's/[0-9]/N/g'
{ time true; } 2>&1 | sed 's/[0-9]/N/g'
{ time; } 2>/dev/null
echo "alone: $?"

TIMEFORMAT='%R'
{ time true; } 2>&1 | grep -c '^[0-9]*\.[0-9][0-9][0-9]$'
TIMEFORMAT='[%0R|%1U|%2S|%lR|%%|%3lS|%0lU]'
{ time true; } 2>&1 | sed 's/[0-9]/N/g'
TIMEFORMAT='%P'
{ time true; } 2>&1 | grep -c '^[0-9][0-9]*\.[0-9][0-9]$'
TIMEFORMAT='%5R tail %'
{ time true; } 2>&1 | sed 's/[0-9]/N/g'
TIMEFORMAT='%R'
{ time -p true; } 2>&1 | wc -l
TIMEFORMAT=
echo "empty: $({ time true; } 2>&1 | wc -l)"
TIMEFORMAT='a%xb'
echo "bad: $({ time echo ran; } 2>/dev/null)"
unset TIMEFORMAT
echo "unset: $({ time true; } 2>&1 | wc -l)"

f() { time return 4; }
{ f; } 2>/dev/null
echo "return: $? $({ f; } 2>&1 | wc -l)"

echo "keyword: $(type time)"
echo time is a word here
\time true 2>/dev/null
echo "quoted: $?"
command time true 2>/dev/null
echo "command: $?"
time=3
echo "time=$time"
