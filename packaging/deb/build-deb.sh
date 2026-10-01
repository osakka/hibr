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
Suggests: libssl3 | libssl1.1, libpng16-16
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
