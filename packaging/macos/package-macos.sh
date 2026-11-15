#!/usr/bin/env bash
# Builds the macOS release: OneNode.app (Apple silicon) in a zip, with Qt and OpenSSL inside it.
#
# Usage: packaging/macos/package-macos.sh <build-dir> <version> <output-dir>
#   <build-dir>   a finished Release build that contains appOneNode.app
#                 (configured with -DONENODE_STATIC_OPENSSL=ON, so the program itself needs no OpenSSL library)
#
# Why OpenSSL goes into the app: OneNode forces Qt's OpenSSL TLS backend (Apple's own TLS has no TLS 1.3 for
# plain sockets). Qt looks for libssl.3.dylib and libcrypto.3.dylib in <app>/Contents/Frameworks.
#
# The app is signed "ad hoc" (no Apple developer account), so on first start macOS says it is from an
# unidentified developer: right-click the app, choose Open.
set -euo pipefail

BUILD=$(cd "$1" && pwd)
VERSION=$2
mkdir -p "$3"
OUT=$(cd "$3" && pwd)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

APP="$OUT/OneNode.app"
rm -rf "$APP"
cp -R "$BUILD/appOneNode.app" "$APP"

macdeployqt "$APP" -qmldir="$ROOT/qml"

# OneNode does not use databases: remove Qt's SQL plugins (they point at libraries that do not exist on this machine)
rm -rf "$APP/Contents/PlugIns/sqldrivers"

# The TLS plugin must be there, or every connection would fail (copy it from Qt if macdeployqt left it out)
TLS_PLUGIN="$APP/Contents/PlugIns/tls/libqopensslbackend.dylib"
if [ ! -f "$TLS_PLUGIN" ]; then
    mkdir -p "$APP/Contents/PlugIns/tls"
    cp "$QT_ROOT_DIR/plugins/tls/libqopensslbackend.dylib" "$TLS_PLUGIN"
fi
[ -f "$TLS_PLUGIN" ] || { echo "libqopensslbackend.dylib is missing from the app"; exit 1; }

FRAMEWORKS="$APP/Contents/Frameworks"
OPENSSL_LIB="$(brew --prefix openssl@3)/lib"
for lib in libssl.3.dylib libcrypto.3.dylib; do
    cp -L "$OPENSSL_LIB/$lib" "$FRAMEWORKS/$lib"
    chmod u+w "$FRAMEWORKS/$lib"
    install_name_tool -id "@rpath/$lib" "$FRAMEWORKS/$lib"
done
# libssl points at libcrypto by an absolute Homebrew path: make it point at its neighbour instead
OLD_CRYPTO=$(otool -L "$FRAMEWORKS/libssl.3.dylib" | awk '/libcrypto/ { print $1; exit }')
install_name_tool -change "$OLD_CRYPTO" "@loader_path/libcrypto.3.dylib" "$FRAMEWORKS/libssl.3.dylib"

# Nothing inside the app may still point at this build machine
BAD=0
while IFS= read -r file; do
    if otool -L "$file" 2>/dev/null | tail -n +2 | grep -qE "/opt/homebrew|/usr/local/(opt|Cellar|lib)"; then
        echo "A path of the build machine is still inside: $file"
        BAD=1
    fi
done < <(find "$APP" -type f \( -name "*.dylib" -o -perm -u+x \))
[ "$BAD" -eq 0 ] || exit 1

cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/"
codesign --force --deep --sign - "$APP"   # ad hoc; modified files must be signed again

ditto -c -k --keepParent "$APP" "$OUT/OneNode-$VERSION-macos-arm64.zip"
echo "Wrote $OUT/OneNode-$VERSION-macos-arm64.zip"
