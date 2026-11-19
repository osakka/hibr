# console key in a process that holds no terminal of its own -- an app's own
# process, with no display (ADR 0050), or a child forked while the console
# was open -- waits on the watched descriptors and the time, and never reads
# a key: it ends at once when a watched one has something, after the time
# when none does, and `console woke` names what is ready. With nothing
# watched and no time given there is nothing to wait for, and it says so
# rather than waiting for ever. Until 0.99.553 it refused at once, "console:
# not open", so an app's own process had no wait at all.

mod load ./build/mods/console.so > /dev/null || exit 1
T=/tmp/hibr-console-bare-$$
mkfifo "$T"
exec {f}<> "$T"
console watch "$f"

t0=${EPOCHREALTIME/./}
read -r c0 _ _ < "/proc/$$/schedstat"
console key 400
st=$?
read -r c1 _ _ < "/proc/$$/schedstat"
t=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
c=$(( (c1 - c0) / 1000000 ))
echo "nothing ready: status $st, waited out the time: $([ "$t" -ge 350 ] && echo yes || echo "no, $t ms")"
echo "and spent none of it on the processor: $([ "$c" -lt 40 ] && echo yes || echo "no, $c ms")"

( sleep 0.2; echo hi > "$T" ) &
t0=${EPOCHREALTIME/./}
k := console key 3000
st=$?
t=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
w := console woke "$f"
echo "a watched one has something: status $st, key [$k], woke at once: $([ "$t" -lt 2000 ] && echo yes || echo "no, $t ms"), woke [$([ "$w" = "$f" ] && echo it)]"
read -r -u "$f" line
echo "and it is still there to read: $line"
wait

t0=${EPOCHREALTIME/./}
console key -q 300
st=$?
t=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
echo "quiet, with something left out: status $st, waited out the time: $([ "$t" -ge 250 ] && echo yes || echo "no, $t ms")"

console unwatch "$f"
console key 2> /dev/null
echo "nothing watched and no time: status $?"
console key 2>&1
exec {f}>&-
rm -f "$T"
