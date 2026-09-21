mod load ./build/mods/sys.so
type drop

drop
echo "no argument: $?"

drop a b
echo "too many: $?"

drop nobody
echo "not root: $?"

drop nosuchuser____
echo "unknown user, not root: $?"

mod drop sys
drop nobody
echo "after unload: $?"
