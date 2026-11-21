# A prefix assignment of HIBR_LOGLEVEL sets the shell's own level for that
# one command and puts it back after, as any prefix does (log levels #555
# meeting prefix assignments #602); a value it refuses changes nothing, and
# its refusal comes before any redirection, as a prefix is applied first.
echo "before: $HIBR_LOGLEVEL"
HIBR_LOGLEVEL=error :
echo "after a prefix: $HIBR_LOGLEVEL"
HIBR_LOGLEVEL=error declare -p HIBR_LOGLEVEL 2> /dev/null
echo "after declare: $HIBR_LOGLEVEL"
HIBR_LOGLEVEL=nonsense :
echo "after a refused one: $HIBR_LOGLEVEL"
f() { echo "inside: $HIBR_LOGLEVEL"; }
HIBR_LOGLEVEL=info f
echo "after a function: $HIBR_LOGLEVEL"
