#!/bin/sh
# Cut a release: bump HIBR_VER (and optionally the desktop's own DT_VER),
# tag it, and -- once the tag is live on the public mirror -- refresh the
# Homebrew formula against it.
#
#   ./release.sh 0.22                    bump HIBR_VER, tag v0.22
#   ./release.sh 0.22 --desktop 1.0      also bump the desktop's DT_VER
#   ./release.sh 0.22 --formula          refresh the Homebrew formula for
#                                        a tag already pushed and synced
#                                        to the public mirror
#
# Two steps, not one: the Homebrew formula's tarball URL has to point at
# a tag github.com/osakka/hibr can actually serve, which only exists
# after this repo's own tag is pushed and the mirror has synced from it
# -- not something this script can make happen on its own schedule.
#
# Refuses to run against a dirty tree, without CHANGELOG.md's own top
# entry already naming the version being released, or with the test
# suite failing: a version bump is a promise about what shipped, and
# none of those three are things a script should paper over.

set -eu

SRC=$(cd "$(dirname "$0")" && pwd)
HDR="$SRC/include/hibr.h"
DESKTOP="$SRC/examples/desktop/desktop.hibr"
FORMULA="$SRC/packaging/homebrew/hibr.rb"
CHANGELOG="$SRC/CHANGELOG.md"
REPO_SLUG=osakka/hibr

VERSION=
DTVERSION=
DO_FORMULA=0

usage() {
	awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
	exit "${1:-0}"
}

die() { printf 'release: %s\n' "$*" >&2; exit 1; }
step() { printf '== %s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
	--desktop) DTVERSION=$2; shift ;;
	--formula) DO_FORMULA=1 ;;
	-h | --help) usage ;;
	-*) die "unknown option: $1" ;;
	*) [ -z "$VERSION" ] || die "version given twice: $VERSION and $1"
	   VERSION=$1 ;;
	esac
	shift
done
[ -n "$VERSION" ] || usage 1
case $VERSION in
[0-9]*.[0-9]* | [0-9]*.[0-9]*.[0-9]*) ;;
*) die "version must look like 0.22 or 1.2.3, not $VERSION" ;;
esac
if [ -n "$DTVERSION" ]; then
	case $DTVERSION in
	[0-9]*.[0-9]* | [0-9]*.[0-9]*.[0-9]* | [0-9]*) ;;
	*) die "desktop version must look like 1.0, not $DTVERSION" ;;
	esac
fi

cd "$SRC"

if [ "$DO_FORMULA" = 1 ]; then
	step "checking the tree is clean"
	[ -z "$(git status --porcelain)" ] ||
		die "uncommitted changes present; commit or stash them first"
	step "refreshing the Homebrew formula for v$VERSION"
	[ -f "$FORMULA" ] || die "$FORMULA not found"
	url="https://github.com/$REPO_SLUG/archive/refs/tags/v$VERSION.tar.gz"
	tarball=$(mktemp)
	FDONE=0
	cleanup() {
		rm -f "$tarball"
		[ "$FDONE" = 1 ] && return 0
		git checkout -- "$FORMULA" 2>/dev/null || true
	}
	trap cleanup EXIT
	if ! curl -fsSL "$url" -o "$tarball"; then
		die "could not fetch $url -- has v$VERSION been pushed and \
synced to the public mirror yet?"
	fi
	sha=$(sha256sum "$tarball" | cut -d' ' -f1)
	sed -i \
		-e "s#^  url \".*\"#  url \"$url\"#" \
		-e "s#^  version \".*\"#  version \"$VERSION\"#" \
		-e "s#^  sha256 \".*\"#  sha256 \"$sha\"#" \
		"$FORMULA"
	git add "$FORMULA"
	git commit -m "Homebrew formula: v$VERSION"
	FDONE=1
	step "done -- push this commit, then update osakka/homebrew-hibr \
separately: it is a different published repo, not part of this one's \
own history or CI"
	exit 0
fi

step "checking the tree is clean"
[ -z "$(git status --porcelain)" ] ||
	die "uncommitted changes present; commit or stash them first"

step "checking CHANGELOG.md already has an entry for $VERSION"
top=$(awk '/^## /{print; exit}' "$CHANGELOG")
[ "$top" = "## $VERSION" ] ||
	die "CHANGELOG.md's top entry is \"$top\", not \"## $VERSION\" -- \
write the release notes first, then run this again"

step "building"
make >/tmp/release-build.$$ 2>&1 || {
	cat /tmp/release-build.$$ >&2
	rm -f /tmp/release-build.$$
	die "build failed; nothing was bumped or tagged"
}
rm -f /tmp/release-build.$$

step "running the test suite"
tests/run.sh >/tmp/release-tests.$$ 2>&1 || {
	cat /tmp/release-tests.$$ >&2
	rm -f /tmp/release-tests.$$
	die "tests failed; nothing was bumped or tagged"
}
rm -f /tmp/release-tests.$$

step "checking what needs bumping actually exists, before touching any of it"
grep -q '^#define HIBR_VER "' "$HDR" || die "HIBR_VER not found in $HDR"
if [ -n "$DTVERSION" ]; then
	grep -q '^DT_VER=' "$DESKTOP" || die "DT_VER not found in $DESKTOP"
fi

# From here on, a failure reverts the three files below rather than
# leaving a half-bumped tree behind for the next run to trip over --
# cleared only once the release commit itself has actually landed.
DONE=0
revert() {
	[ "$DONE" = 1 ] && return 0
	git checkout -- "$HDR" "$DESKTOP" "$FORMULA" 2>/dev/null || true
}
trap revert EXIT

step "bumping HIBR_VER to $VERSION"
sed -i "s/^#define HIBR_VER \".*\"/#define HIBR_VER \"$VERSION\"/" "$HDR"

if [ -n "$DTVERSION" ]; then
	step "bumping DT_VER to $DTVERSION"
	sed -i "s/^DT_VER=.*/DT_VER=$DTVERSION/" "$DESKTOP"
fi

if [ -f "$FORMULA" ]; then
	step "updating the formula's own version field (url/sha256 wait for --formula)"
	sed -i "s#^  version \".*\"#  version \"$VERSION\"#" "$FORMULA"
fi

step "committing and tagging"
git add "$HDR" "$FORMULA"
[ -n "$DTVERSION" ] && git add "$DESKTOP"
git commit -m "Release v$VERSION"
git tag -a "v$VERSION" -m "v$VERSION"
DONE=1

cat <<EOF

v$VERSION is committed and tagged locally. Next:

  1. git push origin main --follow-tags
  2. wait for github.com/$REPO_SLUG to sync the new tag from the mirror
  3. ./release.sh $VERSION --formula
  4. push that commit too, then update osakka/homebrew-hibr by hand --
     it is a separate published repo this script cannot reach
EOF
