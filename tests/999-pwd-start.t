# PWD at startup is bash's (0.99.530): an inherited PWD that does not name
# the current directory is replaced by the real one, one that does is kept
# even through a symlink, and one missing is set and exported. hibr kept
# whatever it was given, so a script started by a program with that
# program's own PWD wrote its files in the program's folder -- found when
# a suite's file landed in the tree's root instead of its scratch folder.
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
real=$(pwd -P)
a=$(PWD=/ "$HIBR" -c 'echo "$PWD"')
[ "$a" = "$real" ] && echo "a PWD naming another folder is replaced" || echo "kept a wrong one"
b=$(env -u PWD "$HIBR" -c 'echo "$PWD"')
[ "$b" = "$real" ] && echo "a missing PWD is set" || echo "missing stayed missing: [$b]"
c=$(env -u PWD "$HIBR" -c 'env | grep -c "^PWD="')
echo "and exported: $c"
ln -s "$d" "$d.l"
cd "$d.l" || exit 1
e=$(PWD="$d.l" "$HIBR" -c 'echo "$PWD"')
[ "$e" = "$d.l" ] && echo "one through a symlink to this folder is kept" || echo "lost the link: $e"
cd /
rm -rf "$d" "$d.l"
