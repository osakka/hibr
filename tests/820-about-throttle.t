# about.hibr throttles its Linux CPU/memory reads to AB_SLOWMS, the same as
# its non-Linux fork-based path -- otherwise About's own refresh rate tracks
# whatever tick rate anything else open happens to be asking for (a
# Terminal window's own faster tick would drag About's numbers along with
# it, updating far more often than AB_SLOWMS says). dt_want in about_draw is
# the other half of that fix and is not reachable from here, since it needs
# a real desktop loop; this checks the throttle itself.
#
# Fully deterministic: dt_ms and dt_want are stubbed, so nothing here
# depends on real time or a real /proc reading's actual value -- only on
# whether a deliberately corrupted field survives a call that should be
# throttled, and is gone after one that should not be.

declare -gA AB
AB_SLOWMS=3000
NOWVAL=1000
DT_ROWS=1
dt_want() { :; }
dt_ms() { ret "$NOWVAL"; }
. examples/desktop/apps/about.hibr
AB_OS=Linux

echo "--- about_cpu"
about_cpu 1 > /dev/null
AB[1]["ptotal"]=999999999
NOWVAL=2500
about_cpu 1 > /dev/null
[ "${AB[1]["ptotal"]}" = 999999999 ] && echo "throttle holds within the window"
NOWVAL=4500
about_cpu 1 > /dev/null
[ "${AB[1]["ptotal"]}" != 999999999 ] && echo "and re-reads once it elapses"

echo "--- about_mem"
NOWVAL=1000
unset "AB[1]"
about_mem 1 > /dev/null
AB[1]["cmem"]=12345
NOWVAL=2500
about_mem 1 > /dev/null
[ "${AB[1]["cmem"]}" = 12345 ] && echo "throttle holds within the window"
NOWVAL=4500
about_mem 1 > /dev/null
[ "${AB[1]["cmem"]}" != 12345 ] && echo "and re-reads once it elapses"
