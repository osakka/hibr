# varwatch's guard (ADR 0050): in an app's own process the desktop's state
# is read only, and a write that changes nothing is no change -- so an app's
# top-level `X="${X-default}"`, run with X already mirrored from the
# desktop, passes quietly, where a write that would change X is refused and
# leaves it as it was. Whole values and keys alike. Recorded: bash has no
# varwatch.

mod load ./build/mods/varwatch.so
X=5
declare -gA M=([a]=1)
varwatch add X M
varwatch guard on
X="${X-1}"
echo "default line over a set value: $? X=$X"
X=5
echo "the same value again: $? X=$X"
M["a"]=1
echo "the same key value again: $? a=${M["a"]}"
X=6 2> /dev/null
echo "a change: $? X=$X"
M["a"]=2 2> /dev/null
echo "a key's change: $? a=${M["a"]}"
unset X 2> /dev/null
echo "an unset: $? X=${X-gone}"
