#!/bin/sh
# Build hibr's Debian package in Debian 12 (packaging/deb/Containerfile.bookworm),
# rootless, from a clean copy of the tree: the package then runs on Debian 12
# and up, whatever the machine building it runs. Both the image and the build
# are build steps with chroot isolation, since rootless podman cannot start a
# container on every machine (this one's runtime says "did not get container
# create message"); the .deb is copied out with --output.
#
#   packaging/deb/build-bookworm.sh [outdir]      default: build/deb
set -eu
SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$SRC/build/deb}
IMG=hibr-build-bookworm
REV=${DEB_REVISION:-1}
podman image exists "$IMG" ||
	podman build --isolation chroot -q -t "$IMG" \
		-f "$SRC/packaging/deb/Containerfile.bookworm" "$SRC/packaging/deb" > /dev/null
CF=$(mktemp)
trap 'rm -f "$CF"' EXIT
cat > "$CF" <<CF
FROM localhost/$IMG
COPY . /src
WORKDIR /src
RUN rm -rf build && DEB_REVISION=$REV packaging/deb/build-deb.sh /out
FROM scratch
COPY --from=0 /out /
CF
mkdir -p "$OUT"
podman build --isolation chroot -q -f "$CF" -o "type=local,dest=$OUT" "$SRC" > /dev/null
ls "$OUT"/*.deb
