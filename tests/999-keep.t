# packaging/keep.sh, what every package repository keeps: the newest three
# versions of each package for each architecture, history one commit, and
# never a publish over a newer one made elsewhere. Against a throwaway
# remote and its clones, so nothing real is touched.

T=/tmp/hibr-keep-$$
H=$PWD
mkdir -p "$T"
cd "$T" || exit 1
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
git init -q --bare -b main remote.git
git clone -q remote.git a 2> /dev/null
cd a || exit 1
mkdir -p pool
for v in 0.9 0.10 0.11 0.12 0.2; do
	: > "pool/demo_${v}_amd64.deb"
done
: > pool/other_1.0_all.deb
git add -A
git commit -q -m one
git commit -q --allow-empty -m two
git push -q origin main 2> /dev/null
cd "$T" || exit 1
git clone -q remote.git b 2> /dev/null

sh -c '
. "$1/packaging/keep.sh"
cd a
keep_sync . || exit 1
find pool -name "*.deb" |
	sed -n "s|^\(.*/\)\([^/_]*\)_\([^_/]*\)_\([^_/]*\)\.deb$|\2 \4 \3 \1\2_\3_\4.deb|p" |
	keep_prune
keep_publish "Keep the newest three" > /dev/null || exit 1
echo "kept: $(ls pool | tr "\n" " ")"
echo "commits here: $(git rev-list --count HEAD)"
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] && echo "the remote has the same one commit"
' x "$H"

echo "-- a clone that is behind refuses to publish"
sh -c '
. "$1/packaging/keep.sh"
cd b
: > pool/demo_0.13_amd64.deb
keep_sync . 2> /dev/null && echo "it published over a newer remote" || echo "refused: not what its origin has"
' x "$H"

echo "-- a lease that has gone stale refuses the push"
sh -c '
. "$1/packaging/keep.sh"
cd a
keep_sync . || exit 1
KEEP_LEASE=0000000000000000000000000000000000000000
: > pool/demo_0.13_amd64.deb
keep_publish "Publish 0.13" > /dev/null 2>&1
echo "status $?"
git ls-remote ../remote.git main | cut -c1-40 > ../there
[ "$(cat ../there)" != "$(git rev-parse HEAD)" ] && echo "the remote is untouched"
' x "$H"

cd "$H" || exit 1
rm -rf "$T"
