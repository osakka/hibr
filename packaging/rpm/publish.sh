#!/bin/sh
# Add built RPMs to the dnf repository and publish it.
#
#   packaging/rpm/publish.sh --prune    # keep the newest three, nothing added
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
# The newest three versions of each package for each architecture stay and
# nothing older, and the history is one commit, force-pushed on every
# publish (packaging/keep.sh, owner 2026-11-18): every package ever
# published had been kept, half a gigabyte and growing.

set -eu
. "$(dirname "$0")/../keep.sh"

[ $# -ge 1 ] || { echo "usage: publish.sh package.rpm... | --prune" >&2; exit 2; }
# --prune publishes what is there, keeping the newest three of each and
# making the history one commit, with nothing new added.
if [ "$1" = --prune ]; then
	[ $# -eq 1 ] || { echo "usage: publish.sh --prune" >&2; exit 2; }
	shift
fi
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
for f in packages/*/*.rpm; do
	[ -e "$f" ] || continue
	printf '%s %s\n' "$(rpm -qp --qf '%{NAME} %{ARCH} %{VERSION}-%{RELEASE}' "$f" 2> /dev/null)" "$f"
done | keep_prune
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

what="Publish $(for rpm in "$@"; do basename "$rpm" .rpm; done | tr '\n' ' ')"
[ $# -eq 0 ] && what="Keep the newest $KEEP of each package"
keep_publish "$what" || exit 1
echo "== published $URL"
