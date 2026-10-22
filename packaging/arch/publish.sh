#!/bin/sh
# Add built packages to the pacman repository and publish it.
#
#   packaging/arch/publish.sh build/arch/hibr-<ver>-1-x86_64.pkg.tar.zst [...]
#
# The repository is a checkout of itdlabs/hibr-arch (ARCH_REPO, default
# ~/hibr-arch), which mirrors itself to github.com/osakka/hibr-arch, where
# GitHub Pages serves it. It is an ordinary pacman repository named
# `hibr`, one folder per architecture: the packages, a detached signature
# beside each (.sig), and the database repo-add keeps (hibr.db, signed
# too), all with the apt repository's key. In /etc/pacman.conf:
#
#   [hibr]
#   Server = https://osakka.github.io/hibr-arch/$arch
#
# and the key, once: pacman-key --add hibr.asc; pacman-key --lsign-key KEY.
# repo-add writes hibr.db and hibr.files as symbolic links to their
# .tar.gz; GitHub Pages serves a link as the link's own text, so they are
# replaced by copies.

set -eu

[ $# -ge 1 ] || { echo "usage: publish.sh package.pkg.tar.zst..." >&2; exit 2; }
REPO=${ARCH_REPO:-$HOME/hibr-arch}
GNUPGHOME=${APT_GNUPGHOME:-$HOME/.local/share/hibr-apt/gnupg}
export GNUPGHOME
KEY=${APT_KEY:-A1E3CA788A8FB02052DA22B413035BE1B1674431}
GPGKEY=$KEY
export GPGKEY

[ -d "$REPO/.git" ] || { echo "publish: $REPO is not a checkout" >&2; exit 1; }
gpg --list-secret-keys "$KEY" > /dev/null 2>&1 ||
	{ echo "publish: no secret key $KEY in $GNUPGHOME" >&2; exit 1; }

git -C "$REPO" pull -q --ff-only 2> /dev/null || true
for pkg in "$@"; do
	arch=$(bsdtar -xOf "$pkg" .PKGINFO | sed -n 's/^arch = //p')
	[ -n "$arch" ] || { echo "publish: $pkg has no .PKGINFO arch" >&2; exit 1; }
	d=$REPO/$arch
	mkdir -p "$d"
	cp "$pkg" "$d/"
	f=$d/$(basename "$pkg")
	rm -f "$f.sig"
	gpg --batch --yes --local-user "$KEY" --detach-sign --no-armor -o "$f.sig" "$f"
	gpg --batch --verify "$f.sig" "$f" 2> /dev/null ||
		{ echo "publish: $f.sig does not verify" >&2; exit 1; }
	for l in hibr.db hibr.files; do
		[ -f "$d/$l.tar.gz" ] && [ ! -L "$d/$l" ] && rm -f "$d/$l" "$d/$l.sig"
	done
	repo-add -q --sign --key "$KEY" "$d/hibr.db.tar.gz" "$f"
	for l in hibr.db hibr.files; do
		rm -f "$d/$l" "$d/$l.sig"
		cp "$d/$l.tar.gz" "$d/$l"
		cp "$d/$l.tar.gz.sig" "$d/$l.sig"
	done
	echo "== added $(basename "$pkg")"
done

cd "$REPO"
gpg --armor --export "$KEY" > hibr.asc
touch .nojekyll

git add -A
git commit -q -m "Publish $(for pkg in "$@"; do basename "$pkg" .pkg.tar.zst; done | tr '\n' ' ')"
if ! timeout 120 git push -q origin HEAD 2> push.err; then
	cat push.err >&2
	rm -f push.err
	echo "publish: committed in $REPO, not pushed" >&2
	exit 1
fi
rm -f push.err
echo "== published https://osakka.github.io/hibr-arch"
