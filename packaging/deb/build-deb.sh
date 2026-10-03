#!/bin/sh
# Build hibr's Debian package from this tree, for the machine it runs on.
#
#   packaging/deb/build-deb.sh [outdir]      default: build/deb
#
# It installs the tree into a staging directory with PREFIX=/usr -- the same
# `make install` everyone else uses, so the package holds exactly what a
# source install would -- writes the control file, and packs it with
# dpkg-deb. Nothing here needs root: fakeroot gives the files root as owner.
#
# The package depends on libc, at least the newest symbol version the
# binary and its modules actually use (read from them, not guessed), and
# suggests libssl and libpng, which hibr loads only when TLS or an image is
# first wanted.

set -eu

SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$SRC/build/deb}
cd "$SRC"

VER=$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' include/hibr.h)
REV=${DEB_REVISION:-1}
ARCH=$(dpkg --print-architecture)
PKG=hibr_${VER}-${REV}_${ARCH}
STAGE=$OUT/$PKG

[ -n "$VER" ] || { echo "build-deb: no HIBR_VER in include/hibr.h" >&2; exit 1; }
echo "== building hibr $VER for $ARCH"
trap 'make -s > /dev/null' EXIT
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN"
make -s PREFIX=/usr > /dev/null
make -s PREFIX=/usr DESTDIR="$STAGE" install > /dev/null

glibc=$(for f in "$STAGE/usr/bin/hibr" "$STAGE"/usr/lib/hibr/*.so; do
	objdump -T "$f"
done | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
size=$(du -sk "$STAGE/usr" | cut -f1)

install -d "$STAGE/etc/pam.d"
cat > "$STAGE/etc/pam.d/hibr" <<'PAM'
#%PAM-1.0
# hibr's screen lock (the auth module): checks the password of whoever is
# already logged in, with the system's own rules. It opens no session.
@include common-auth
@include common-account
PAM
chmod 644 "$STAGE/etc/pam.d/hibr"
cat > "$STAGE/etc/pam.d/hibr-login" <<'PAM'
#%PAM-1.0
# hibr's login screen (/usr/share/hibr/desktop/login/login.hibr, run by
# hibr-login@.service): the same rules as a console login.
auth       optional   pam_faildelay.so  delay=3000000
auth       requisite  pam_nologin.so
@include common-auth
@include common-account
session    required   pam_loginuid.so
session    optional   pam_keyinit.so force revoke
session    required   pam_env.so readenv=1
session    required   pam_env.so readenv=1 envfile=/etc/default/locale
session    required   pam_limits.so
@include common-session
@include common-password
PAM
chmod 644 "$STAGE/etc/pam.d/hibr-login"
printf '/etc/pam.d/hibr\n/etc/pam.d/hibr-login\n' > "$STAGE/DEBIAN/conffiles"
install -d "$STAGE/usr/lib/systemd/system"
cat > "$STAGE/usr/lib/systemd/system/hibr-login@.service" <<'UNIT'
# hibr's login screen on a text terminal, in place of getty. Shipped off:
#   systemctl enable --now hibr-login@tty2
# takes tty2 (and stops getty there); `systemctl disable --now
# hibr-login@tty2` gives it back. Keep one getty, so a broken login can
# never take every console away. The screen ends after each session and
# Restart= brings a fresh one; KillMode=process leaves a session's own
# processes alone where no logind has moved them out of this unit.
[Unit]
Description=hibr login on %I
Documentation=file:///usr/share/hibr/desktop/README.md
After=systemd-user-sessions.service plymouth-quit-wait.service getty-pre.target
After=rc-local.service
Before=getty.target
IgnoreOnIsolate=yes
Conflicts=getty@%i.service rescue.service
Before=rescue.service
ConditionPathExists=/dev/tty0

[Service]
ExecStart=/usr/bin/hibr /usr/share/hibr/desktop/login/login.hibr
Type=idle
Restart=always
RestartSec=1
UtmpIdentifier=%I
StandardInput=tty
StandardOutput=tty
StandardError=journal
TTYPath=/dev/%I
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
IgnoreSIGPIPE=no
SendSIGHUP=yes
KillMode=process
Environment=TERM=linux
UnsetEnvironment=LANG LANGUAGE LC_CTYPE LC_NUMERIC LC_TIME LC_COLLATE LC_MONETARY LC_MESSAGES LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE LC_MEASUREMENT LC_IDENTIFICATION

[Install]
WantedBy=getty.target
UNIT
chmod 644 "$STAGE/usr/lib/systemd/system/hibr-login@.service"

install -d "$STAGE/usr/share/doc/hibr"
install -m 644 LICENSE "$STAGE/usr/share/doc/hibr/copyright"
gzip -9n < CHANGELOG.md > "$STAGE/usr/share/doc/hibr/changelog.gz"
gzip -9nf "$STAGE/usr/share/man/man1/hibr.1"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: hibr
Version: $VER-$REV
Architecture: $ARCH
Maintainer: osakka <osakka@gmail.com>
Installed-Size: $size
Depends: libc6 (>= $glibc)
Suggests: libssl3 | libssl1.1, libpng16-16, libturbojpeg0, libavformat62 | libavformat61 | libavformat60 | libavformat59
Section: shells
Priority: optional
Homepage: https://github.com/osakka/hibr
Description: small, fast shell that runs the bash you already write
 hibr runs a large subset of bash -- variables, quoting, functions,
 pipelines, redirections, arrays, traps, job control -- in a small, fast
 binary, and adds nested maps, JSON that keeps its types, regex capture,
 typed function signatures, results returned without a fork, declared
 command-line arguments, native TCP, TLS and Unix sockets, and modules
 loaded on demand. It includes a lint (--explain), a dry run (--plan), an
 agent mode, and a text-mode desktop with its own apps.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ] && command -v add-shell > /dev/null; then
	add-shell /usr/bin/hibr
fi
EOF
cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] && command -v remove-shell > /dev/null; then
	remove-shell /usr/bin/hibr
fi
EOF
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

fakeroot dpkg-deb --build --root-owner-group "$STAGE" "$OUT/$PKG.deb" > /dev/null
rm -rf "$STAGE"
echo "== $OUT/$PKG.deb"
