# A `need` for a module already loaded is met at once, and does not read
# every module's descriptor on the module path again: sys, like nearly every
# module, offers no interface of its own name, so the old order walked the
# path on every call -- 2.5 ms each, which the desktop paid on every frame
# that asked (0.99.394). The bound is generous: 200 calls took about half a
# second the old way and well under a millisecond now.
HIBR_MODPATH=$PWD/build/mods
need sys || exit 1
t0=${EPOCHREALTIME/./}
for ((i = 0; i < 200; i++)); do need sys || exit 1; done
t1=${EPOCHREALTIME/./}
if [ $((t1 - t0)) -lt 100000 ]; then
	echo "200 needs of a loaded module: met at once"
else
	echo "200 needs of a loaded module took $(( (t1 - t0) / 1000 )) ms"
fi
