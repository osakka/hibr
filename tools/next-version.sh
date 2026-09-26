#!/bin/sh
# Suggest the next HIBR_VER and/or DT_VER, from what has actually changed
# since the last release tag -- not a guess from how many lines changed,
# which says nothing about whether any of them broke anything (a large,
# safe refactor looks "big"; a one-line ABI change looks "small"), but
# from two things that actually mean something:
#
#   - HIBR_ABI (include/hibr.h) changed since the tag -- every module
#     ABI break already has to bump this by hand, so it is already the
#     project's own signal for "this is not backward compatible",
#     not a new one invented here.
#   - a commit in range carries a trailer line "Breaking: <why>" (own
#     paragraph, own line, the same shape "Co-Authored-By:" already is)
#     -- for anything that breaks compatibility without an ABI number to
#     point at, such as a saved-settings format, a documented behaviour,
#     a public script's own interface.
#
# Either one bumps the major version; anything else that touched files
# in scope bumps minor; no changes in scope prints nothing to bump.
#
# Core and desktop are scored separately and independently, since they
# version independently (HIBR_VER, DT_VER) -- a desktop-only session
# should never bump HIBR_VER, and vice versa.
#
#   tools/next-version.sh            both, from the last tag to HEAD
#   tools/next-version.sh v0.21      both, from a given point
#   make next-version                 the same, via the Makefile

set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
HDR="$SRC/include/hibr.h"
DESKTOP="$SRC/examples/desktop/desktop.hibr"
cd "$SRC"

FROM=${1:-}
if [ -z "$FROM" ]; then
	FROM=$(git describe --tags --abbrev=0 2>/dev/null) ||
		{ echo "next-version: no tag found; pass a starting point" \
			"(a tag or commit) explicitly" >&2; exit 1; }
fi
git rev-parse --verify "$FROM" >/dev/null 2>&1 ||
	{ echo "next-version: $FROM: not a valid commit or tag" >&2; exit 1; }

bump() {
	# $1 = current version (X.Y or X.Y.Z), $2 = major|minor
	ver=$1 kind=$2
	major=${ver%%.*}
	rest=${ver#*.}
	minor=${rest%%.*}
	if [ "$kind" = major ]; then
		echo "$((major + 1)).0"
	else
		echo "$major.$((minor + 1))"
	fi
}

breaking_trailer() {
	# $1 = path spec (files or directories the commit must touch).
	# Empty output, status 0, either way -- "no match" is a normal
	# result here, never an error worth set -e stopping the script over.
	git log "$FROM..HEAD" --grep='^Breaking:' -i --pretty=format:'%h' \
		-- $1 2>/dev/null | head -1
	return 0
}

changed() {
	# $1 = path spec -- anything at all touched in range
	[ -n "$(git diff --name-only "$FROM..HEAD" -- $1)" ]
}

echo "next-version: from $FROM to HEAD"
echo

# -- core: everything except examples/desktop/** --------------------------
CORE_PATHS=". :!examples/desktop"
if changed "$CORE_PATHS"; then
	cur=$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' "$HDR")
	abi_then=$(git show "$FROM:include/hibr.h" 2>/dev/null |
		sed -n 's/^#define HIBR_ABI \([0-9]*\)u\?/\1/p')
	abi_now=$(sed -n 's/^#define HIBR_ABI \([0-9]*\)u\?/\1/p' "$HDR")
	trailer=$(breaking_trailer "$CORE_PATHS")
	if [ "$abi_then" != "$abi_now" ]; then
		echo "core: HIBR_ABI $abi_then -> $abi_now (a module ABI break)"
		echo "  suggest: HIBR_VER $cur -> $(bump "$cur" major)"
	elif [ -n "$trailer" ]; then
		echo "core: a \"Breaking:\" commit in range ($trailer)"
		echo "  suggest: HIBR_VER $cur -> $(bump "$cur" major)"
	else
		echo "core: changed, no ABI break or Breaking: trailer found"
		echo "  suggest: HIBR_VER $cur -> $(bump "$cur" minor)"
	fi
else
	echo "core: nothing changed outside examples/desktop -- no bump"
fi
echo

# -- desktop: examples/desktop/** only --------------------------------------
DT_PATHS="examples/desktop"
if changed "$DT_PATHS"; then
	cur=$(sed -n 's/^DT_VER=\(.*\)/\1/p' "$DESKTOP")
	trailer=$(breaking_trailer "$DT_PATHS")
	if [ -n "$trailer" ]; then
		echo "desktop: a \"Breaking:\" commit in range ($trailer)"
		echo "  suggest: DT_VER $cur -> $(bump "$cur" major)"
	else
		echo "desktop: changed, no Breaking: trailer found"
		echo "  suggest: DT_VER $cur -> $(bump "$cur" minor)"
	fi
else
	echo "desktop: nothing changed under examples/desktop -- no bump"
fi
