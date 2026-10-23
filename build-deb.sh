#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VERSION=0.1.4
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM

make -C "$ROOT"
mkdir -p "$STAGE/DEBIAN" \
    "$STAGE/etc/sane.d/dll.d" \
    "$STAGE/usr/lib/canon-chmp" \
    "$STAGE/usr/lib/x86_64-linux-gnu/sane"

cp "$ROOT/debian/control" "$STAGE/DEBIAN/control"
cp "$ROOT/debian/canon-chmp.conf" "$STAGE/etc/sane.d/canon-chmp.conf"
cp "$ROOT/debian/dll.d/canon_chmp" "$STAGE/etc/sane.d/dll.d/canon_chmp"
cp "$ROOT/chmp_scan.py" "$STAGE/usr/lib/canon-chmp/chmp_scan.py"
cp "$ROOT/libsane-canon_chmp.so.1.0.0" "$STAGE/usr/lib/x86_64-linux-gnu/sane/"
ln -s libsane-canon_chmp.so.1.0.0 \
    "$STAGE/usr/lib/x86_64-linux-gnu/sane/libsane-canon_chmp.so.1"

mkdir -p "$ROOT/dist"
dpkg-deb --build --root-owner-group "$STAGE" \
    "$ROOT/dist/sane-backend-canon-chmp_${VERSION}_amd64.deb"
