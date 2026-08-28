#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

need() { command -v "$1" >/dev/null 2>&1 || { echo "MISSING: $1"; exit 2; }; }
need python
need clang++

TMP="${TMPDIR:-/data/data/com.termux/files/usr/tmp}/exynostools-lsfg-test.$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/compat.cpp" <<'CPP'
#include <cstdlib>
#include <iostream>
#include "layer_lsfg_compat.h"
static int check(bool v, const char* n) { if (!v) { std::cerr << "FAIL: " << n << '\n'; return 1; } std::cout << "PASS: " << n << '\n'; return 0; }
int main() {
  unsetenv("LSFG_PROCESS"); unsetenv("LSFG_CONFIG");
  if (check(!exynos_lsfg_process_active(), "empty environment inactive")) return 1;
  setenv("LSFG_PROCESS", "gamenative-lsfg", 1);
  if (check(!exynos_lsfg_process_active(), "process marker alone inactive")) return 1;
  setenv("LSFG_CONFIG", "/tmp/conf.toml", 1);
  if (check(exynos_lsfg_process_active(), "GameNative marker pair active")) return 1;
  auto s = snapshot_lsfg_compat();
  if (check(s.enabled && s.process_environment_present, "snapshot active")) return 1;
  return 0;
}
CPP

clang++ -std=c++17 -Wall -Wextra -Werror \
  -Isrc/layer src/layer/layer_lsfg_compat.cpp "$TMP/compat.cpp" \
  -o "$TMP/compat"
"$TMP/compat"
python tests/lsfg_compat_contract.py --repo .

if [ -f build-lsfg-termux/libVkLayer_VortekXclipse.so ]; then
  need readelf
  readelf -h build-lsfg-termux/libVkLayer_VortekXclipse.so | grep -q 'AArch64'
  if readelf -d build-lsfg-termux/libVkLayer_VortekXclipse.so | grep -E 'RPATH|RUNPATH'; then
    echo 'FAIL: Android layer contains RPATH/RUNPATH'
    exit 1
  fi
  echo 'PASS: ARM64 layer ELF and no RPATH/RUNPATH'
else
  echo 'SKIP: build-lsfg-termux/libVkLayer_VortekXclipse.so not present'
fi

echo 'PASS: Termux-native LSFG compatibility suite'
echo 'NOTE: Samsung vendor-ICD execution is validated by tests/android_native_probe, because Android linker namespaces can block the vendor ICD from a raw Termux process.'
