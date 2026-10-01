#!/bin/bash
# Point the AUR package at a released version, and publish it.
#
#   packaging/aur/update.sh 0.74
#
# Rewrites PKGBUILD's pkgver and checksum from the GitHub release tarball --
# the same file the AUR build downloads, so the sum is the one it will check
# -- and writes .SRCINFO, which the AUR reads instead of running PKGBUILD.
# It is written here rather than by `makepkg --printsrcinfo`, so a machine
# without pacman can publish too; the fields are PKGBUILD's, in makepkg's
# order.
#
# Then, if AUR_REPO (default ~/aur-hibr) is a clone of
# ssh://aur@aur.archlinux.org/hibr.git, copies the three files in, commits
# and pushes. Without one it stops after updating this directory.

set -eu

[ $# -eq 1 ] || { echo "usage: update.sh version" >&2; exit 2; }
ver=$1
here=$(cd "$(dirname "$0")" && pwd)
url="https://github.com/osakka/hibr/archive/refs/tags/v$ver.tar.gz"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

curl -fsSL -o "$tmp" "$url" ||
	{ echo "update: no release tarball for v$ver yet" >&2; exit 1; }
sum=$(sha256sum "$tmp" | cut -d' ' -f1)

sed -i -e "s/^pkgver=.*/pkgver=$ver/" -e "s/^pkgrel=.*/pkgrel=1/" \
	-e "s/^sha256sums=.*/sha256sums=('$sum')/" "$here/PKGBUILD"

(
	cd "$here"
	. ./PKGBUILD
	printf 'pkgbase = %s\n' "$pkgname"
	printf '\tpkgdesc = %s\n' "$pkgdesc"
	printf '\tpkgver = %s\n' "$pkgver"
	printf '\tpkgrel = %s\n' "$pkgrel"
	printf '\turl = %s\n' "$url"
	printf '\tinstall = %s\n' "$install"
	for a in "${arch[@]}"; do printf '\tarch = %s\n' "$a"; done
	for a in "${license[@]}"; do printf '\tlicense = %s\n' "$a"; done
	for a in "${depends[@]}"; do printf '\tdepends = %s\n' "$a"; done
	for a in "${optdepends[@]}"; do printf '\toptdepends = %s\n' "$a"; done
	for a in "${source[@]}"; do printf '\tsource = %s\n' "$a"; done
	for a in "${sha256sums[@]}"; do printf '\tsha256sums = %s\n' "$a"; done
	printf '\npkgname = %s\n' "$pkgname"
) > "$here/.SRCINFO"
echo "== PKGBUILD and .SRCINFO at $ver ($sum)"

repo=${AUR_REPO:-$HOME/aur-hibr}
if [ ! -d "$repo/.git" ]; then
	echo "== no AUR checkout at $repo; not published"
	exit 0
fi
cp "$here/PKGBUILD" "$here/.SRCINFO" "$here/hibr.install" "$repo/"
cd "$repo"
git add PKGBUILD .SRCINFO hibr.install
if git diff --cached --quiet; then
	echo "== the AUR already has $ver"
	exit 0
fi
git commit -q -m "hibr $ver"
git push -q origin HEAD:master ||
	{ echo "update: committed in $repo, not pushed" >&2; exit 1; }
echo "== published hibr $ver to the AUR"
