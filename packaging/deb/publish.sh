#!/bin/sh
# Add a built package to the apt repository and publish it.
#
#   packaging/deb/publish.sh build/deb/hibr_0.71-1_amd64.deb [...]
#   packaging/deb/publish.sh --prune      # only keep the newest, and publish
#
# The repository is a checkout of itdlabs/hibr-apt (APT_REPO, default
# ~/hibr-apt), which mirrors itself to github.com/osakka/hibr-apt, where
# apt reads it. Its layout is Debian's usual one -- pool/ for the packages,
# dists/stable/ for the signed index -- so `apt` needs nothing special:
#
#   deb [signed-by=/usr/share/keyrings/hibr.gpg] <url> stable main
#
# pool/ keeps the newest APT_KEEP (3) versions of each package for each
# architecture -- the current one and two to go back to -- and the older
# ones are taken out of it on every publish: GitHub Pages serves about a
# gigabyte at most, and with every version kept (5.26 GB by 0.99.481) its
# builds stalled and apt saw nothing new for hours (owner, 2026-11-10).
# Git's history still has every one. The index is signed with the repository
# key in APT_GNUPGHOME (default ~/.local/share/hibr-apt/gnupg), both as
# Release.gpg beside Release and as InRelease, since apt reads either.

set -eu

[ $# -ge 1 ] || { echo "usage: publish.sh package.deb... | --prune" >&2; exit 2; }
KEEP=${APT_KEEP:-3}
if [ "$1" = --prune ]; then
	shift
	[ $# -eq 0 ] || { echo "usage: publish.sh --prune" >&2; exit 2; }
	what="Prune pool/ to the newest $KEEP of each package"
else
	what="Publish $(for deb in "$@"; do basename "$deb" .deb; done | tr '\n' ' ')"
fi
REPO=${APT_REPO:-$HOME/hibr-apt}
GNUPGHOME=${APT_GNUPGHOME:-$HOME/.local/share/hibr-apt/gnupg}
export GNUPGHOME
KEY=${APT_KEY:-A1E3CA788A8FB02052DA22B413035BE1B1674431}
SUITE=stable

[ -d "$REPO/.git" ] || { echo "publish: $REPO is not a checkout" >&2; exit 1; }
gpg --list-secret-keys "$KEY" > /dev/null 2>&1 ||
	{ echo "publish: no secret key $KEY in $GNUPGHOME" >&2; exit 1; }

git -C "$REPO" pull -q --ff-only 2> /dev/null || true
# Each package in its own pool folder, by Debian's usual layout:
# pool/main/h/hibr for hibr, pool/main/b/blit for Blit, which this
# repository has carried as well since 0.99.297.
for deb in "$@"; do
	dpkg-deb -I "$deb" > /dev/null
	pkg=$(dpkg-deb -f "$deb" Package)
	[ -n "$pkg" ] || { echo "publish: $deb names no package" >&2; exit 1; }
	pool=$REPO/pool/main/$(printf '%.1s' "$pkg")/$pkg
	mkdir -p "$pool"
	cp "$deb" "$pool/"
	echo "== added $(basename "$deb")"
done

cd "$REPO"
# The newest KEEP versions of each package for each architecture stay,
# counted by the package each file names -- <pkg>_<version>_<arch>.deb --
# wherever in pool/ it is, not by its folder: a folder can hold a stray of
# another package's.
find pool -name '*.deb' |
	sed -n 's|^\(.*/\)\([^/_]*\)_\([^_/]*\)_\([^_/]*\)\.deb$|\2 \4 \3 \1|p' |
	sort -k1,1 -k2,2 -k3,3Vr |
	awk -v keep="$KEEP" '{ k = $1 " " $2; if (++n[k] > keep) print $4 $1 "_" $3 "_" $2 ".deb" }' |
	while read -r f; do
		rm -f "$f"
		echo "== pruned ${f##*/}"
	done
archs=$(find pool -name '*.deb' | sed 's/.*_\([^_]*\)\.deb$/\1/' | sort -u)
for a in $archs; do
	d=dists/$SUITE/main/binary-$a
	mkdir -p "$d"
	apt-ftparchive --arch "$a" packages pool > "$d/Packages"
	gzip -9nc "$d/Packages" > "$d/Packages.gz"
done
apt-ftparchive \
	-o "APT::FTPArchive::Release::Origin=hibr" \
	-o "APT::FTPArchive::Release::Label=hibr" \
	-o "APT::FTPArchive::Release::Suite=$SUITE" \
	-o "APT::FTPArchive::Release::Codename=$SUITE" \
	-o "APT::FTPArchive::Release::Architectures=$(echo $archs)" \
	-o "APT::FTPArchive::Release::Components=main" \
	-o "APT::FTPArchive::Release::Description=hibr, a small, fast shell" \
	release "dists/$SUITE" > "dists/$SUITE/Release.new"
mv "dists/$SUITE/Release.new" "dists/$SUITE/Release"
rm -f "dists/$SUITE/Release.gpg" "dists/$SUITE/InRelease"
gpg --batch --yes --local-user "$KEY" --armor --detach-sign \
	-o "dists/$SUITE/Release.gpg" "dists/$SUITE/Release"
gpg --batch --yes --local-user "$KEY" --clearsign \
	-o "dists/$SUITE/InRelease" "dists/$SUITE/Release"
gpg --export "$KEY" > hibr.gpg
gpg --armor --export "$KEY" > hibr.asc

git add -A
git commit -q -m "$what"
if ! timeout 120 git push -q origin HEAD 2> push.err; then
	cat push.err >&2
	rm -f push.err
	echo "publish: committed in $REPO, not pushed" >&2
	exit 1
fi
rm -f push.err
echo "== published $SUITE: $(echo $archs)"
