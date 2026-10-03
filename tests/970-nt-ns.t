# -nt and -ot compare modification times to the nanosecond, as bash does:
# two files changed in the same second are still told apart.
d=$(mktemp -d)
touch -d '2026-01-01 00:00:00.100' "$d/a"
touch -d '2026-01-01 00:00:00.900' "$d/b"
touch -d '2026-01-01 00:00:00.900' "$d/c"
[ "$d/b" -nt "$d/a" ] && echo "test b newer"
[ "$d/a" -ot "$d/b" ] && echo "test a older"
[[ $d/b -nt $d/a ]] && echo "[[ b newer"
[[ $d/a -ot $d/b ]] && echo "[[ a older"
[ "$d/b" -nt "$d/c" ] || echo "same time is not newer"
[[ $d/c -ot $d/b ]] || echo "same time is not older"
[ "$d/a" -nt "$d/missing" ] && echo "anything is newer than nothing"
[ "$d/missing" -ot "$d/a" ] && echo "nothing is older than anything"
rm -rf "$d"
