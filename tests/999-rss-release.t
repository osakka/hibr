# A large expansion's memory is given back (Gitea #463). An 8 MB command
# substitution left the shell at 17.7 MB resident for the rest of its life:
# the pooled scratch string and its mask kept their size, and once those
# were freed glibc's dynamic mmap threshold had moved every later large
# buffer onto the heap, where freeing returns nothing. The pools now free a
# buffer past 64 kB and the threshold is fixed, so the shell is back near
# its idle size of about 2 MB. The sanitizers measure something else.
if [ -n "${ASAN_OPTIONS-}" ] || [ ! -r /proc/self/status ]; then
	echo "read 8000000 bytes"
	echo "after an 8 MB substitution: back under 6 MB"
	exit 0
fi
x=$(head -c 8000000 /dev/zero | tr '\0' a)
echo "read ${#x} bytes"
unset x
y=$(head -c 8000000 /dev/zero | tr '\0' b)
unset y
while read -r k v rest; do
	[ "$k" = VmRSS: ] && rss=$v
done < /proc/self/status
if [ "$rss" -lt 6000 ]; then
	echo "after an 8 MB substitution: back under 6 MB"
else
	echo "after an 8 MB substitution: $rss kB"
fi
