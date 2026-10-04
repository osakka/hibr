#!/bin/sh
# Add a built package to the apt repository and publish it.
#
#   packaging/deb/publish.sh build/deb/hibr_0.71-1_amd64.deb [...]
#
# The repository is a checkout of itdlabs/hibr-apt (APT_REPO, default
# ~/hibr-apt), which mirrors itself to github.com/osakka/hibr-apt, where
# apt reads it. Its layout is Debian's usual one -- pool/ for the packages,
# dists/stable/ for the signed index -- so `apt` needs nothing special:
#
#   deb [signed-by=/usr/share/keyrings/hibr.gpg] <url> stable main
#
# Every package ever published stays in pool/, so an older version can
# still be installed by naming it. The index is signed with the repository
# key in APT_GNUPGHOME (default ~/.local/share/hibr-apt/gnupg), both as
# Release.gpg beside Release and as InRelease, since apt reads either.

set -eu

[ $# -ge 1 ] || { echo "usage: publish.sh package.deb..." >&2; exit 2; }
REPO=${APT_REPO:-$HOME/hibr-apt}
GNUPGHOME=${APT_GNUPGHOME:-$HOME/.local/share/hibr-apt/gnupg}
export GNUPGHOME
KEY=${APT_KEY:-A1E3CA788A8FB02052DA22B413035BE1B1674431}
SUITE=stable

[ -d "$REPO/.git" ] || { echo "publish: $REPO is not a checkout" >&2; exit 1; }
gpg --list-secret-keys "$KEY" > /dev/null 2>&1 ||
	{ echo "publish: no secret key $KEY in $GNUPGHOME" >&2; exit 1; }

git -C "$REPO" pull -q --ff-only 2> /dev/null || true
mkdir -p "$REPO/pool/main/h/hibr"
for deb in "$@"; do
	dpkg-deb -I "$deb" > /dev/null
	cp "$deb" "$REPO/pool/main/h/hibr/"
	echo "== added $(basename "$deb")"
done

cd "$REPO"
archs=$(ls pool/main/h/hibr/*.deb | sed 's/.*_\([^_]*\)\.deb$/\1/' | sort -u)
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
git commit -q -m "Publish $(for deb in "$@"; do basename "$deb" .deb; done | tr '\n' ' ')"
if ! timeout 120 git push -q origin HEAD 2> push.err; then
	cat push.err >&2
	rm -f push.err
	echo "publish: committed in $REPO, not pushed" >&2
	exit 1
fi
rm -f push.err
echo "== published $SUITE: $(echo $archs)"
