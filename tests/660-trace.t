# The trace module's argument handling, which needs no network.
mod load ./build/mods/trace.so && echo "loaded"

trace 2>/dev/null; echo "no host rc=$?"
trace --nonsense 2>/dev/null; echo "bad option rc=$?"
trace -q no-such-host-anywhere.invalid 2>/dev/null; echo "unresolvable rc=$?"

# the target is recorded before any probe goes out, so a failed trace still
# says what it was trying to reach
trace -q -m 1 -w 1 127.0.0.1 > /dev/null 2>&1
echo "target=${TRACE[0]["target"]} ip=${TRACE[0]["ip"]}"

# -v puts the hops somewhere else
trace -q -m 1 -w 1 -v PATH2 127.0.0.1 > /dev/null 2>&1
echo "second var target=${PATH2[0]["target"]}"

# live mode needs a display, and says which one
trace -l 127.0.0.1 2>/dev/null; echo "live without a display rc=$?"

mod drop trace > /dev/null && echo "dropped"
