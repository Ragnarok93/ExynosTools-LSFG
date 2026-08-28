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

CONTRACT_ARGS=(--repo .)
if [ -n "${GAMENATIVE_120_ROOT:-}" ]; then
  need git
  CONTRACT_ARGS+=(--gamenative-root "$GAMENATIVE_120_ROOT")
  echo "CHECK: exact stock GameNative 1.2.0 source at $GAMENATIVE_120_ROOT"
else
  echo 'SKIP: GAMENATIVE_120_ROOT not set; stock GameNative 1.2.0 source contracts not checked locally'
fi
python tests/lsfg_compat_contract.py "${CONTRACT_ARGS[@]}"

if [ -n "${GAMENATIVE_120_ROOT:-}" ]; then
  STOCK_ARGS=("$GAMENATIVE_120_ROOT")
  if [ -n "${GAMENATIVE_120_DRIVER_ZIP:-}" ]; then
    STOCK_ARGS+=("$GAMENATIVE_120_DRIVER_ZIP")
  fi
  python tests/gamenative_120_stock_wrapper_contract.py "${STOCK_ARGS[@]}"
  if [ -n "${GAMENATIVE_120_DRIVER_ZIP:-}" ]; then
    echo 'PASS: stock GameNative 1.2.0 custom-driver ZIP contract'
  else
    echo 'SKIP: GAMENATIVE_120_DRIVER_ZIP not set; packaged custom-driver ZIP not checked locally'
  fi
fi

if [ -f build-lsfg-termux/libVkLayer_VortekXclipse.so ]; then
  need readelf
  readelf -h build-lsfg-termux/libVkLayer_VortekXclipse.so | grep -q 'AArch64'
  if readelf -d build-lsfg-termux/libVkLayer_VortekXclipse.so | grep -E 'RPATH|RUNPATH'; then
    echo 'FAIL: Android layer contains RPATH/RUNPATH'
    exit 1
  fi
  echo 'PASS: ARM64 ExynosTools compatibility layer ELF'
else
  echo 'SKIP: build-lsfg-termux/libVkLayer_VortekXclipse.so not present'
fi

if [ -f build-gamenative-shim/libvulkan_exynostools.so ]; then
  need readelf
  readelf -h build-gamenative-shim/libvulkan_exynostools.so | grep -q 'AArch64'
  readelf -Ws build-gamenative-shim/libvulkan_exynostools.so | \
    grep -q 'vkGetInstanceProcAddr'
  readelf -Ws build-gamenative-shim/libvulkan_exynostools.so | \
    grep -q 'vkCreateInstance'
  if readelf -d build-gamenative-shim/libvulkan_exynostools.so | grep -E 'RPATH|RUNPATH'; then
    echo 'FAIL: GameNative driver shim contains RPATH/RUNPATH'
    exit 1
  fi
  echo 'PASS: ARM64 stock-Wrapper driver shim ELF'
else
  echo 'SKIP: build-gamenative-shim/libvulkan_exynostools.so not present'
fi

echo 'PASS: Termux-native LSFG compatibility suite'
echo 'NOTE: GameNative wrappers remain stock. ExynosTools is validated as the custom driver loaded through the existing AdrenoTools path.'
echo 'NOTE: Samsung vendor-ICD execution still requires an Android app-process/device test because raw Termux may be blocked by Android linker namespaces.'
