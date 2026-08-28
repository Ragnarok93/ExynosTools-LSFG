# Stock GameNative 1.2.0 + LSFG Device Test

This is the end-to-end device handoff for the ExynosTools-LSFG Wrapper WCP. The compatibility target is the unmodified GameNative app v1.2.0 codebase at commit `3491226faedb7222a5f8b7248c0247957a060836`. GameNative 1.2.0 itself owns and launches its bundled LSFG runtime (`v1.3.3-android-arm64-v8a`).

## 1. Install the compatibility Wrapper

Import `ExynosTools-LSFG-GameNative-1.2.0.wcp` through GameNative's Contents Manager. In the Bionic container select:

- Graphics Driver: `Wrapper-ExynosTools-LSFG`
- Graphics Driver Version: `System`
- Use AdrenoTools Turnip: **OFF**
- LSFG: **ON**

Do not select the older ExynosTools custom-driver packages for this test. The stock-1.2.0 LSFG compatibility route must reach Samsung Vulkan through Android's system Vulkan loader.

## 2. Enable the existing Wrapper diagnostic

GameNative 1.2.0's Environment tab supports arbitrary per-container environment variables. Add:

```text
WRAPPER_DIAG=1
```

For a predictable filename, also add:

```text
WRAPPER_DIAG_APPID=lsfg-proof
```

No GameNative source modification is required. `WRAPPER_DIAG` is implemented by the Wrapper shipped inside the WCP.

## 3. Launch an LSFG workload

Launch a Vulkan/DXVK or VKD3D title with LSFG enabled and let it reach normal rendering. The Wrapper emits its report when `vkCreateDevice` completes or fails.

With `WRAPPER_DIAG_APPID=lsfg-proof`, the default in-container report path is:

```text
/data/data/app.gamenative/files/imagefs/usr/tmp/wrapper_diag_lsfg-proof.txt
```

That path is inside GameNative's private app storage. Raw Termux normally cannot read another app's private directory. Export/copy the report using whatever device-access path is already available in the test environment; the validator only needs the resulting text file.

## 4. Validate the report

Direct validator:

```bash
python tests/gamenative_lsfg_runtime_diag.py /path/to/wrapper_diag_lsfg-proof.txt
```

Or include it in the Termux suite:

```bash
export GAMENATIVE_120_LSFG_DIAG=/path/to/wrapper_diag_lsfg-proof.txt
tests/termux_lsfg/run.sh
```

For the strongest local contract check, also provide an exact stock GameNative 1.2.0 checkout and the downloaded WCP:

```bash
export GAMENATIVE_120_ROOT=/path/to/GameNative-1.2.0
export GAMENATIVE_120_WCP=/path/to/ExynosTools-LSFG-GameNative-1.2.0.wcp
export GAMENATIVE_120_LSFG_DIAG=/path/to/wrapper_diag_lsfg-proof.txt
tests/termux_lsfg/run.sh
```

The runtime validator requires all of these observations in the same Wrapper report:

- `driver=Samsung (Xclipse)`
- `contract: GameNative-1.2.0@3491226f`
- `active: yes`
- `process: gamenative-lsfg`
- `config: set`
- `backend: system-vulkan`
- `incoming pNext: present`
- `NULL-pNext fallback: disabled`
- `result: 0 (VK_SUCCESS)`

These markers prove that the stock-1.2.0 compatibility Wrapper was running, GameNative's LSFG launch environment was present, the Wrapper selected the system Vulkan route, the incoming shared-device feature chain was not empty, the unsafe NULL-pNext retry was blocked, the underlying Vulkan driver identified itself as Samsung/Xclipse, and shared-device creation succeeded.

## 5. Interpret failures

- `backend: adrenotools-custom` — the test is using a custom AdrenoTools backend instead of the required system/Samsung route. Disable custom Turnip/driver selection and repeat.
- `driver` is not `Samsung (Xclipse)` — the actual Vulkan backend is not the Xclipse vendor driver expected for this test.
- `active: no`, missing `gamenative-lsfg`, or `config: missing` — GameNative's LSFG manager did not arm the process as expected.
- `incoming pNext: none` — the device-create call did not contain the feature chain expected from the shared-device LSFG path; do not treat the run as compatible.
- `NULL-pNext fallback: allowed` — wrong/old Wrapper or LSFG was not active at the Wrapper boundary.
- `result` is not `VK_SUCCESS` — inspect the rest of the Wrapper report and GameNative/LSFG logs for unsupported feature/extension, device-lost, or loader failures.
- missing `GameNative-1.2.0@3491226f` contract marker — the installed WCP predates the pinned runtime-proof build or is not the stock-1.2.0 package.

## 6. What this does not prove

A passing Wrapper diagnostic proves the critical loader/backend/shared-device creation boundary. It does **not** by itself prove that LSFG generated and presented interpolated frames correctly.

Final end-to-end acceptance still requires an actual GameNative 1.2.0 run where:

1. LSFG remains enabled without a crash, device loss, or watchdog stall;
2. output/presentation FPS rises above the base rendered FPS for a multiplier greater than 1;
3. generated frames remain visually coherent through camera motion, UI motion, and scene changes;
4. BCn-dependent titles still render correctly through ExynosTools where virtualization is needed;
5. disabling LSFG restores ordinary presentation while keeping the same ExynosTools Wrapper selected.
