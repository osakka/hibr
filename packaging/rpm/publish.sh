#!/bin/sh
# Add built RPMs to the dnf repository and publish it.
#
#   packaging/rpm/publish.sh build/rpm/hibr-<ver>-1.x86_64.rpm [...]
#
# The repository is a checkout of itdlabs/hibr-rpm (RPM_REPO, default
# ~/hibr-rpm), which mirrors itself to github.com/osakka/hibr-rpm, where
# GitHub Pages serves it. It is a plain dnf/yum repository: the packages
# under packages/<arch>/ and repodata/ from createrepo_c. Each package is
# signed, and so is the metadata (repodata/repomd.xml.asc), with the same
# key as the apt repository, so one key vouches for every packaging:
#
#   sudo curl -o /etc/yum.repos.d/hibr.repo \
#        https://osakka.github.io/hibr-rpm/hibr.repo
#   sudo dnf install hibr
#
# Every package ever published stays, so an older version can be named.

set -eu

[ $# -ge 1 ] || { echo "usage: publish.sh package.rpm..." >&2; exit 2; }
REPO=${RPM_REPO:-$HOME/hibr-rpm}
GNUPGHOME=${APT_GNUPGHOME:-$HOME/.local/share/hibr-apt/gnupg}
export GNUPGHOME
KEY=${APT_KEY:-A1E3CA788A8FB02052DA22B413035BE1B1674431}
URL=${RPM_URL:-https://osakka.github.io/hibr-rpm}

[ -d "$REPO/.git" ] || { echo "publish: $REPO is not a checkout" >&2; exit 1; }
gpg --list-secret-keys "$KEY" > /dev/null 2>&1 ||
	{ echo "publish: no secret key $KEY in $GNUPGHOME" >&2; exit 1; }

DB=$(mktemp -d "${TMPDIR:-/tmp}/hibr-rpmdb-XXXXXX")
trap 'rm -rf "$DB"' EXIT
gpg --armor --export "$KEY" > "$DB/key.asc"
rpm --dbpath "$DB" --initdb
rpm --dbpath "$DB" --import "$DB/key.asc"

git -C "$REPO" pull -q --ff-only 2> /dev/null || true
for rpm in "$@"; do
	arch=$(rpm -qp --qf '%{ARCH}' "$rpm")
	mkdir -p "$REPO/packages/$arch"
	cp "$rpm" "$REPO/packages/$arch/"
	f=$REPO/packages/$arch/$(basename "$rpm")
	rpmsign --define "__gpg $(command -v gpg)" --define "_gpg_name $KEY" \
		--define "_gpg_path $GNUPGHOME" --addsign "$f" < /dev/null > /dev/null 2>&1
	rpm --dbpath "$DB" -Kv "$f" | grep -q 'Signature, key ID.*: OK' ||
		{ echo "publish: $f is not signed by $KEY" >&2; exit 1; }
	echo "== added $(basename "$rpm")"
done

cd "$REPO"
createrepo_c -q --update --general-compress-type=gz .
rm -f repodata/repomd.xml.asc
gpg --batch --yes --local-user "$KEY" --armor --detach-sign \
	-o repodata/repomd.xml.asc repodata/repomd.xml
gpg --armor --export "$KEY" > hibr.asc
cat > hibr.repo <<EOF
[hibr]
name=hibr
baseurl=$URL/
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=$URL/hibr.asc
EOF
touch .nojekyll

git add -A
git commit -q -m "Publish $(for rpm in "$@"; do basename "$rpm" .rpm; done | tr '\n' ' ')"
if ! timeout 120 git push -q origin HEAD 2> push.err; then
	cat push.err >&2
	rm -f push.err
	echo "publish: committed in $REPO, not pushed" >&2
	exit 1
fi
rm -f push.err
echo "== published $URL"
