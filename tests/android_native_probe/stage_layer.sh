#!/system/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR=${1:-"$ROOT/build-lsfg"}
SRC="$BUILD_DIR/libVkLayer_VortekXclipse.so"
DST="$ROOT/tests/android_native_probe/app/src/main/jniLibs/arm64-v8a/libVkLayer_VortekXclipse.so"

if [ ! -f "$SRC" ]; then
    echo "Missing built layer: $SRC" >&2
    echo "Build the Android ARM64 layer first." >&2
    exit 1
fi

mkdir -p "$(dirname -- "$DST")"
cp -f "$SRC" "$DST"
chmod 755 "$DST"

echo "Staged: $DST"
ls -lh "$DST"
file "$DST"
