# The copy module (Gitea #402): a tree copied with its modes, times and
# links, each item under a hidden name until it is whole; what is already
# there, what is missing and what would go inside itself named and left;
# a stop file stopping it with nothing half-written; a move that has to
# cross a filesystem copied and then removed (HIBR_COPYEXDEV makes every
# rename answer EXDEV, so the path is the same on any machine), and one
# that need not left to a rename.
mod load ./build/mods/copy.so
t=$(mktemp -d)
cd "$t" || exit 1
mkdir -p src/d/e dst dst2 dst3
printf hello > src/a
chmod 640 src/a
touch -d @1577934245 src/a
ln -s ../a src/d/l
printf xyz > src/d/e/f
copy run dst src/a src/d src/missing; echo "status $?"
stat -c '%a %Y' dst/a
readlink dst/d/l
cat dst/d/e/f; echo
ls -A dst
copy run dst src/a; echo "again: $?"
copy run src/d src/d; echo "into itself: $?"
: > stop
copy run -k stop dst2 src/d; echo "stopped: $?"
ls -A dst2
rm stop
cp -a src mv1
HIBR_COPYEXDEV=1 copy run -m dst3 mv1/d; echo "moved: $?"
ls -A mv1 dst3
r := copy rename dst3 mv1/a; echo "renamed, ${#r[@]} left: $(ls -A mv1) / $(ls -A dst3 | tr '\n' ' ')"
cp -a src/a b
r := HIBR_COPYEXDEV=1 copy rename dst3 b; echo "across, left for the copy: ${r[*]}"
copy run dst3 nothere 2> /dev/null; echo "a missing source: $?"
copy run nofolder src/a; echo "no folder: $?"
cd / && rm -rf "$t"
