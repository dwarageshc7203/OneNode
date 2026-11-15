#!/usr/bin/env bash
# Builds the Linux release: a single AppImage that runs on any distribution with glibc >= the one it was built on
# (the release workflow builds on Ubuntu 22.04, so Ubuntu 22.04 and newer, Fedora, Arch, ...).
#
# Usage: packaging/linux/make-appimage.sh <build-dir> <version> <output-dir>
#   <build-dir>   a finished Release build that contains the appOneNode program
#   <version>     goes into the file name, for example v0.1.0 or dev
#   <output-dir>  where OneNode-<version>-linux-x86_64.AppImage is written
#
# OpenSSL is NOT bundled on Linux on purpose: every supported distribution ships libssl.so.3, and the system copy
# gets the security updates. Qt loads it at run time.
set -euo pipefail

BUILD=$(cd "$1" && pwd)
VERSION=$2
mkdir -p "$3"
OUT=$(cd "$3" && pwd)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR/usr/bin"
cp "$BUILD/appOneNode" "$APPDIR/usr/bin/appOneNode"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APPDIR/usr/"

cd "$WORK"
curl -fsSL -o linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
curl -fsSL -o linuxdeploy-plugin-qt "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod +x linuxdeploy linuxdeploy-plugin-qt

export PATH="$WORK:$PATH"                         # linuxdeploy finds its Qt plugin here
export APPIMAGE_EXTRACT_AND_RUN=1                 # build machines often have no FUSE
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"   # tells the Qt plugin which Qt to copy
export QML_SOURCES_PATHS="$ROOT/qml"              # so that only the QML modules we really import are copied
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

./linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --executable "$APPDIR/usr/bin/appOneNode" \
    --desktop-file "$ROOT/packaging/linux/onenode.desktop" \
    --icon-file "$ROOT/packaging/linux/onenode.png" \
    --output appimage

FILE=$(ls OneNode-*.AppImage | head -n 1)
mv "$FILE" "$OUT/OneNode-$VERSION-linux-x86_64.AppImage"
echo "Wrote $OUT/OneNode-$VERSION-linux-x86_64.AppImage"
