# errexit inside what the shell forks (Gitea #270). Recorded: ADR 0002
# scopes errexit more tightly than bash, so the outcomes are hibr's own.
#
# A pipeline stage ran with errexit off, the one place `set -e` silently
# stopped meaning anything: `{ false; echo X; } | cat` printed X, where
# bash stops the stage at `false`. It keeps errexit now, and is exempt
# exactly when the pipeline's status is tested, as any command is. A child
# that stops says nothing -- its parent sees the status and reports it
# once -- where a ( ) and an inheriting $( ) each printed the errexit line
# twice. And `!` never trips errexit, as POSIX says: the status it gives
# is the point of writing it.
#
# Each case runs in a shell of its own, since the point of most of them is
# that the shell stops.
run() {
	printf '%s\n' "-- $1"
	"$HIBR" -c "$1" 2>&1
	printf 'status %s\n' "$?"
}
run 'set -e; { false; echo X; } | cat; echo after'
run 'set -e; f() { false; echo F; }; f | cat; echo after'
run 'set -e; set -o | grep "^errexit "'
run 'set -e; if { false; echo X; } | cat; then echo tested; fi'
run 'set -e; if false | false; then :; else echo "tested, else"; fi'
run 'set -e; ! true; echo "after !"'
run 'set -e; ! { false; echo X; } | cat; echo "after ! pipeline"'
run 'set -e; ( false; echo X ); echo after'
run 'set -e; shopt -s inherit_errexit; v=$(false; echo X); echo "after [$v]"'
run 'set -e; v=$(false; echo X); echo "without inherit_errexit [$v]"'
run 'set -e; true | false; echo after'
run 'set -e; false & wait; echo "a background job is not this shell"'
