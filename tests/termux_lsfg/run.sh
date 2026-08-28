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
  echo "CHECK: stock GameNative 1.2.0 source at $GAMENATIVE_120_ROOT"
else
  echo 'SKIP: GAMENATIVE_120_ROOT not set; stock GameNative 1.2.0 source pin not checked locally'
fi
python tests/lsfg_compat_contract.py "${CONTRACT_ARGS[@]}"

if [ -n "${GAMENATIVE_120_WCP:-}" ]; then
  if [ -z "${GAMENATIVE_120_ROOT:-}" ]; then
    echo 'FAIL: GAMENATIVE_120_WCP requires GAMENATIVE_120_ROOT'
    exit 2
  fi
  python tests/gamenative_120_wcp_contract.py \
    "$GAMENATIVE_120_ROOT" "$GAMENATIVE_120_WCP"
  echo 'PASS: stock GameNative 1.2.0 Wrapper WCP contract'
elif [ -n "${GAMENATIVE_120_ROOT:-}" ]; then
  echo 'SKIP: GAMENATIVE_120_WCP not set; Wrapper WCP package not checked locally'
else
  echo 'SKIP: stock GameNative 1.2.0 Wrapper WCP contract (set GAMENATIVE_120_ROOT and GAMENATIVE_120_WCP)'
fi

if [ -n "${GAMENATIVE_120_LSFG_DIAG:-}" ]; then
  python tests/gamenative_lsfg_runtime_diag.py "$GAMENATIVE_120_LSFG_DIAG"
  echo 'PASS: stock GameNative 1.2.0 LSFG runtime proof'
else
  echo 'SKIP: GAMENATIVE_120_LSFG_DIAG not set; exported GameNative Wrapper diagnostic not checked'
fi

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
echo 'NOTE: Set WRAPPER_DIAG=1 in stock GameNative 1.2.0, export the resulting wrapper_diag file, and pass it as GAMENATIVE_120_LSFG_DIAG for runtime proof.'
echo 'NOTE: Samsung vendor-ICD execution is validated from the GameNative/Android app process because Android linker namespaces can block the vendor ICD from a raw Termux process.'
