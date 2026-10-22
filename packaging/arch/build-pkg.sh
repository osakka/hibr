#!/bin/sh
# Build hibr's Arch package from the Debian package of the same release.
#
#   packaging/arch/build-pkg.sh build/deb/hibr_<ver>-1_amd64.deb [outdir]
#
# Like the RPM, the package holds the deb's own files, byte for byte --
# nothing stripped or rebuilt -- so the packagings cannot differ in what
# they ship; only the PAM files are Arch's (they include the system's own
# `login` stack) and /etc/shells is kept by hibr.install. It is built here,
# on Debian, with makepkg and a configuration of its own: no Arch machine
# is needed to make one, only to install it.
#
# This is the binary package the pacman repository serves
# (packaging/arch/publish.sh). packaging/aur/ is the other route, a recipe
# that builds from source on the user's machine, published only to the AUR.

set -eu

[ $# -ge 1 ] || { echo "usage: build-pkg.sh hibr_<ver>-1_amd64.deb [outdir]" >&2; exit 2; }
DEB=$(readlink -f "$1")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(readlink -f "${2:-$SRC/build/arch}")
VER=$(dpkg-deb -f "$DEB" Version | sed 's/-[^-]*$//')
REL=${ARCH_PKGREL:-1}
case $(dpkg-deb -f "$DEB" Architecture) in
amd64) CARCH=x86_64 ;;
arm64) CARCH=aarch64 ;;
*) echo "build-pkg: no Arch name for $(dpkg-deb -f "$DEB" Architecture)" >&2; exit 1 ;;
esac
TOP=$(mktemp -d "${TMPDIR:-/tmp}/hibr-arch-XXXXXX")
trap 'rm -rf "$TOP"' EXIT
STAGE=$TOP/stage

echo "== building hibr $VER-$REL for $CARCH, from $(basename "$DEB")"
dpkg-deb -x "$DEB" "$STAGE"
cat > "$STAGE/etc/pam.d/hibr" <<'PAM'
#%PAM-1.0
# hibr's screen lock (the auth module): checks the password of whoever is
# already logged in, with the rules a console login uses. No session.
auth       include      login
account    include      login
PAM
cat > "$STAGE/etc/pam.d/hibr-login" <<'PAM'
#%PAM-1.0
# hibr's login screen (/usr/share/hibr/desktop/login/login.hibr, run by
# hibr-login@.service): exactly the rules of a console login.
auth       include      login
account    include      login
password   include      login
session    include      login
PAM
chmod 644 "$STAGE/etc/pam.d/hibr" "$STAGE/etc/pam.d/hibr-login"
cp "$SRC/packaging/aur/hibr.install" "$TOP/hibr.install"

cat > "$TOP/makepkg.conf" <<EOF
CARCH="$CARCH"
CHOST="$CARCH-pc-linux-gnu"
BUILDENV=(!distcc !color !ccache !check !sign)
OPTIONS=(!strip !docs !libtool !staticlibs emptydirs zipman !purge !debug !lto)
INTEGRITY_CHECK=(sha256)
PKGEXT='.pkg.tar.zst'
SRCEXT='.src.tar.gz'
COMPRESSZST=(zstd -c -T0 -19 -)
PACKAGER="osakka <osakka@gmail.com>"
EOF

cat > "$TOP/PKGBUILD" <<EOF
pkgname=hibr
pkgver=$VER
pkgrel=$REL
pkgdesc='Small, fast shell that runs the bash you already write'
arch=('$CARCH')
url='https://github.com/osakka/hibr'
license=('MIT')
depends=('glibc')
optdepends=('libpng: PNG pictures'
            'libjpeg-turbo: JPEG pictures'
            'openssl: TLS (/dev/tls, HTTPS, mail, WebDAV)'
            'argon2: the vault'
            'ffmpeg: video and sound'
            'poppler-glib: the PDF Reader'
            'xorg-server-xvfb: X programs in desktop windows')
backup=('etc/pam.d/hibr' 'etc/pam.d/hibr-login')
install=hibr.install
options=(!strip !debug)

package() {
	cp -a "$STAGE"/. "\$pkgdir"/
}
EOF

(cd "$TOP" && MAKEPKG_CONF="$TOP/makepkg.conf" PKGDEST="$TOP/out" \
	makepkg --nodeps --noconfirm --force --skipinteg > "$TOP/log" 2>&1) ||
	{ cat "$TOP/log" >&2; exit 1; }
mkdir -p "$OUT"
pkg=$(find "$TOP/out" -name '*.pkg.tar.zst' | head -1)
cp "$pkg" "$OUT/"
echo "== $OUT/$(basename "$pkg")"
