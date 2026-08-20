#!/data/data/com.termux/files/usr/bin/bash
set -e

PROJECT_DIR="$(pwd)"
SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/android-sdk}}"
TERMUX_PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"

echo "=============================================="
echo " Android Native Probe - Termux Toolchain Fix"
echo "=============================================="
echo
echo "Project : $PROJECT_DIR"
echo "SDK     : $SDK_ROOT"
echo "Termux  : $TERMUX_PREFIX"
echo

if [ ! -f "$PROJECT_DIR/settings.gradle.kts" ]; then
    echo "ERROR: settings.gradle.kts was not found."
    echo "Run this script from tests/android_native_probe."
    exit 1
fi

if [ ! -f "$PROJECT_DIR/app/build.gradle.kts" ]; then
    echo "ERROR: app/build.gradle.kts was not found."
    exit 1
fi

if [ ! -x "$TERMUX_PREFIX/bin/cmake" ]; then
    echo "ERROR: ARM64 Termux CMake was not found:"
    echo "  $TERMUX_PREFIX/bin/cmake"
    exit 1
fi

if [ ! -x "$TERMUX_PREFIX/bin/ninja" ]; then
    echo "ERROR: ARM64 Termux Ninja was not found:"
    echo "  $TERMUX_PREFIX/bin/ninja"
    exit 1
fi

if [ ! -x "$TERMUX_PREFIX/bin/aapt2" ]; then
    echo "ERROR: ARM64 Termux AAPT2 was not found:"
    echo "  $TERMUX_PREFIX/bin/aapt2"
    echo
    echo "Install it with:"
    echo "  pkg install aapt2"
    exit 1
fi

NDK_DIR="$SDK_ROOT/ndk/27.2.12479018"

if [ ! -f "$NDK_DIR/source.properties" ]; then
    echo "ERROR: NDK 27.2.12479018 is missing:"
    echo "  $NDK_DIR"
    exit 1
fi

echo "[1/4] Backing up files inside the project..."

BACKUP_DIR="$PROJECT_DIR/.tool-fix-backup"

mkdir -p "$BACKUP_DIR"

cp "$PROJECT_DIR/app/build.gradle.kts" \
   "$BACKUP_DIR/app-build.gradle.kts"

if [ -f "$PROJECT_DIR/gradle.properties" ]; then
    cp "$PROJECT_DIR/gradle.properties" \
       "$BACKUP_DIR/gradle.properties"
fi

if [ -f "$PROJECT_DIR/local.properties" ]; then
    cp "$PROJECT_DIR/local.properties" \
       "$BACKUP_DIR/local.properties"
fi

echo "Backup directory:"
echo "  $BACKUP_DIR"
echo

echo "[2/4] Configuring ARM64 AAPT2..."

GRADLE_PROPERTIES="$PROJECT_DIR/gradle.properties"

touch "$GRADLE_PROPERTIES"

if grep -q '^android\.aapt2FromMavenOverride=' "$GRADLE_PROPERTIES"; then
    sed -i \
        "s#^android\\.aapt2FromMavenOverride=.*#android.aapt2FromMavenOverride=$TERMUX_PREFIX/bin/aapt2#" \
        "$GRADLE_PROPERTIES"
else
    printf 'android.aapt2FromMavenOverride=%s\n' \
        "$TERMUX_PREFIX/bin/aapt2" >> "$GRADLE_PROPERTIES"
fi

echo "AAPT2:"
grep '^android\.aapt2FromMavenOverride=' "$GRADLE_PROPERTIES"
echo

echo "[3/4] Configuring ARM64 Termux CMake..."

APP_GRADLE="$PROJECT_DIR/app/build.gradle.kts"

python - "$APP_GRADLE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

old_block = '''    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }'''

new_block = '''    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }'''

if old_block in text:
    text = text.replace(old_block, new_block)
elif 'version = "3.22.1"' in text:
    text = text.replace('            version = "3.22.1"\n', '')
else:
    print("CMake version line was already removed or was not present.")

path.write_text(text)
PY

LOCAL_PROPERTIES="$PROJECT_DIR/local.properties"

if [ ! -f "$LOCAL_PROPERTIES" ]; then
    printf 'sdk.dir=%s\n' "$SDK_ROOT" > "$LOCAL_PROPERTIES"
fi

if grep -q '^sdk\.dir=' "$LOCAL_PROPERTIES"; then
    sed -i \
        "s#^sdk\\.dir=.*#sdk.dir=$SDK_ROOT#" \
        "$LOCAL_PROPERTIES"
else
    printf 'sdk.dir=%s\n' "$SDK_ROOT" >> "$LOCAL_PROPERTIES"
fi

if grep -q '^cmake\.dir=' "$LOCAL_PROPERTIES"; then
    sed -i \
        "s#^cmake\\.dir=.*#cmake.dir=$TERMUX_PREFIX#" \
        "$LOCAL_PROPERTIES"
else
    printf 'cmake.dir=%s\n' "$TERMUX_PREFIX" >> "$LOCAL_PROPERTIES"
fi

echo "local.properties:"
cat "$LOCAL_PROPERTIES"
echo

echo "[4/4] Verifying ARM64 build tools..."
echo

echo "--- CMake ---"
"$TERMUX_PREFIX/bin/cmake" --version | head -n 1
file "$TERMUX_PREFIX/bin/cmake"
echo

echo "--- Ninja ---"
"$TERMUX_PREFIX/bin/ninja" --version
file "$TERMUX_PREFIX/bin/ninja"
echo

echo "--- AAPT2 ---"
"$TERMUX_PREFIX/bin/aapt2" version
file "$TERMUX_PREFIX/bin/aapt2"
echo

echo "--- NDK ---"
cat "$NDK_DIR/source.properties"
echo

echo "--- Project CMake configuration ---"
sed -n '/externalNativeBuild/,/^[[:space:]]*}/p' \
    "$APP_GRADLE"
echo

echo "Stopping existing Gradle daemons..."
gradle --stop || true

echo
echo "=============================================="
echo " Running Android native probe build"
echo "=============================================="
echo

gradle --no-daemon assembleDebug --stacktrace
