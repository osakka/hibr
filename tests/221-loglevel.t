# Log levels (Gitea #555): one ordered set -- error, warning, info, debug,
# trace -- and one setting for it, which `hibr -d`, `set -d` and
# HIBR_LOGLEVEL all write and HIBR_LOGLEVEL names. In the log logtime keeps
# (standard error as it was when logtime was turned on), a line below error
# carries its level after `hibr: `, so a warning is never read as an error;
# an error keeps the form it has everywhere, and nothing outside the log is
# tagged. Agent mode's JSON is keyed by the level's name already.

d=${TMPDIR:-/tmp}/hibr-ll-$$
mkdir -p "$d"
cd "$d" || exit 1
unstamp() {
	sed -E 's/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}[+-][0-9]{2}:[0-9]{2} /T /; s/ in [0-9]+ s$/ in N s/'
}

echo "-- the variable names the level in force, and every spelling sets it"
"$HIBR" -c 'echo "default: $HIBR_LOGLEVEL"'
"$HIBR" -d 2 -c 'echo "-d 2: $HIBR_LOGLEVEL"'
"$HIBR" -d debug -c 'echo "-d debug: $HIBR_LOGLEVEL"' 2> /dev/null
"$HIBR" -c 'set -d 0; echo "set -d 0: $HIBR_LOGLEVEL"'
"$HIBR" -c 'HIBR_LOGLEVEL=4; echo "a number is stored as its name: $HIBR_LOGLEVEL"' 2> /dev/null
"$HIBR" -c 'HIBR_LOGLEVEL=loud; echo "the rest of the line is abandoned, as for a readonly"
echo "refused, status $?, still $HIBR_LOGLEVEL"' 2>&1
"$HIBR" -d 9 -c 'echo never' 2>&1
echo "-d 9: status $?"
"$HIBR" -c 'HIBR_LOGLEVEL=info; f() { local HIBR_LOGLEVEL=error; getopts a o -z; echo "in f: $HIBR_LOGLEVEL"; }; f; echo "after f: $HIBR_LOGLEVEL"' 2>&1
"$HIBR" -c 'HIBR_LOGLEVEL=info; unset HIBR_LOGLEVEL; getopts a o -z; sleep 0 & disown; echo "unset: back to warning, [$HIBR_LOGLEVEL]"' 2>&1

echo "-- a child: forked keeps it, a new shell takes it only when exported"
"$HIBR" -c 'set -d 2; ( echo "subshell: $HIBR_LOGLEVEL" ); "$HIBR" -c "echo \"new shell: \$HIBR_LOGLEVEL\""; export HIBR_LOGLEVEL; "$HIBR" -c "echo \"exported: \$HIBR_LOGLEVEL\"" 2> /dev/null'
HIBR_LOGLEVEL=1 "$HIBR" -d info -c 'echo "environment, then -d: $HIBR_LOGLEVEL"'
HIBR_LOGLEVEL=loud "$HIBR" -c 'echo "an environment naming no level: status $?, $HIBR_LOGLEVEL"' 2>&1
HIBR_DEBUG=4 "$HIBR" -c 'echo "HIBR_DEBUG is gone: $HIBR_LOGLEVEL"'

echo "-- in the log, each level below error says its name"
cat > levels.hibr <<'S'
shopt -s logtime
set -d 4
nosuch_ll
getopts a o -z
a=(1 2)
x=${a[-1]}
EPOCHSECONDS=1
sleep 0 &
disown
S
"$HIBR" levels.hibr 2> log
grep -E 'hibr: (nosuch_ll|warning: getopts|info: disowned|debug: EPOCHSECONDS|trace: subscript)' log | unstamp | sed 's/disowned job \[1\].*/disowned job [1]/'

echo "-- outside the log nothing is tagged"
"$HIBR" -c 'shopt -s logtime; x=$(getopts a o -z 2>&1); echo "${x#* }"'
"$HIBR" -c 'getopts a o -z' 2>&1

echo "-- a warning collapsed keeps its name"
cat > rep.hibr <<'S'
shopt -s logtime
for i in 1 2 3; do OPTIND=1; getopts a o -z; done
S
"$HIBR" rep.hibr 2> log
unstamp < log

echo "-- agent mode's JSON is keyed by the level"
"$HIBR" -c 'set -o agent; getopts a o -z; nosuch_ll' 2>&1

cd / && rm -rf "$d"
