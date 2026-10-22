#!/bin/sh
# Build hibr's RPM from the Debian package of the same release.
#
#   packaging/rpm/build-rpm.sh build/deb/hibr_<ver>-1_amd64.deb [outdir]
#
# The RPM holds the deb's own files, byte for byte -- the same `make install`
# the deb was staged from -- so the three packagings cannot differ in what
# they ship. Nothing is stripped, recompressed or rebuilt. Two things are
# the distribution's rather than Debian's: the PAM files, which include the
# system's own `login` stack (Fedora, RHEL and openSUSE have no
# common-auth), and /etc/shells, which has no add-shell outside Debian.
#
# Dependencies are named by library (`libpng16.so.16()(64bit)`), not by
# package, since every RPM distribution calls its packages something else;
# rpm resolves a library to whoever provides it. The binaries need glibc
# 2.35 or newer, which every supported Fedora, RHEL 10 and openSUSE
# Tumbleweed have.

set -eu

[ $# -ge 1 ] || { echo "usage: build-rpm.sh hibr_<ver>-1_amd64.deb [outdir]" >&2; exit 2; }
DEB=$(readlink -f "$1")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(readlink -f "${2:-$SRC/build/rpm}")
VER=$(dpkg-deb -f "$DEB" Version | sed 's/-[^-]*$//')
REL=${RPM_RELEASE:-1}
case $(dpkg-deb -f "$DEB" Architecture) in
amd64) ARCH=x86_64 ;;
arm64) ARCH=aarch64 ;;
*) echo "build-rpm: no RPM name for $(dpkg-deb -f "$DEB" Architecture)" >&2; exit 1 ;;
esac
TOP=$(mktemp -d "${TMPDIR:-/tmp}/hibr-rpm-XXXXXX")
trap 'rm -rf "$TOP"' EXIT
STAGE=$TOP/stage

echo "== building hibr $VER-$REL for $ARCH, from $(basename "$DEB")"
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

mkdir -p "$TOP/SPECS" "$TOP/RPMS"
cat > "$TOP/SPECS/hibr.spec" <<EOF
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _build_id_links none
%global __provides_exclude_from ^/usr/lib/hibr/.*\$
%define _binary_payload w6.xzdio

Name:           hibr
Version:        $VER
Release:        $REL
Summary:        Small, fast shell that runs the bash you already write
License:        MIT
URL:            https://github.com/osakka/hibr
BuildArch:      $ARCH
Recommends:     libpng16.so.16()(64bit)
Recommends:     libturbojpeg.so.0()(64bit)
Suggests:       libssl.so.3()(64bit)
Suggests:       libargon2.so.1()(64bit)
Suggests:       libpoppler-glib.so.8()(64bit)
Suggests:       (libavformat.so.62()(64bit) or libavformat.so.61()(64bit) or libavformat.so.60()(64bit) or libavformat.so.59()(64bit))
Suggests:       /usr/bin/Xvfb

%description
hibr runs a large subset of bash -- variables, quoting, functions,
pipelines, redirections, arrays, traps, job control -- in a small, fast
binary, and adds nested maps, JSON that keeps its types, regex capture,
typed function signatures, results returned without a fork, declared
command-line arguments, native TCP, TLS and Unix sockets, and modules
loaded on demand. It includes a lint (--explain), a dry run (--plan), an
agent mode, and a text-mode desktop with its own apps; the applications
come from the catalogue: catalogue install apps/

%install
cp -a $STAGE/. %{buildroot}/

%post
grep -qx /usr/bin/hibr /etc/shells 2>/dev/null || echo /usr/bin/hibr >> /etc/shells

%postun
if [ "\$1" = 0 ]; then
	sed -i '\\|^/usr/bin/hibr\$|d' /etc/shells
fi

%files
%defattr(-,root,root,-)
/usr/bin/hibr
/usr/bin/desktop
/usr/bin/catalogue
/usr/bin/vw
/usr/lib/hibr
/usr/share/hibr
/usr/include/hibr
/usr/share/man/man1/hibr.1.gz
/usr/share/man/man1/desktop.1.gz
/usr/share/doc/hibr
/usr/lib/systemd/system/hibr-login@.service
%dir /var/lib/hibr
%config(noreplace) /etc/pam.d/hibr
%config(noreplace) /etc/pam.d/hibr-login
EOF

rpmbuild -bb --quiet --define "_topdir $TOP" --define "_rpmdir $TOP/RPMS" \
	--target "$ARCH" "$TOP/SPECS/hibr.spec" > "$TOP/log" 2>&1 ||
	{ cat "$TOP/log" >&2; exit 1; }
mkdir -p "$OUT"
rpm=$(find "$TOP/RPMS" -name '*.rpm' | head -1)
cp "$rpm" "$OUT/"
echo "== $OUT/$(basename "$rpm")"
