import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tests/gamenative_lsfg_runtime_diag.py"

GOOD = """[WRAPPER_DIAG] ================ WRAPPER DIAGNOSTICS ================
[WRAPPER_DIAG] device: Samsung Xclipse 940
[WRAPPER_DIAG]   driver=Samsung (Xclipse) (driverID=13)  driverVersion=0x01000000  apiVersion=1.3.279
[WRAPPER_DIAG] --- ExynosTools LSFG integration ---
[WRAPPER_DIAG]   contract: GameNative-1.2.0@3491226f
[WRAPPER_DIAG]   active: yes
[WRAPPER_DIAG]   process: gamenative-lsfg
[WRAPPER_DIAG]   config: set
[WRAPPER_DIAG]   backend: system-vulkan
[WRAPPER_DIAG]   incoming pNext: present
[WRAPPER_DIAG]   NULL-pNext fallback: disabled
[WRAPPER_DIAG] --- vkCreateDevice ---
[WRAPPER_DIAG]   result: 0 (VK_SUCCESS)
"""


class RuntimeDiagContractTest(unittest.TestCase):
    def run_diag(self, text: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "wrapper_diag.txt"
            path.write_text(text)
            return subprocess.run(
                [sys.executable, str(SCRIPT), str(path)],
                text=True,
                capture_output=True,
            )

    def test_accepts_stock_gamenative_120_xclipse_system_vulkan_lsfg_run(self) -> None:
        result = self.run_diag(GOOD)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS: stock GameNative 1.2.0 LSFG runtime diagnostic", result.stdout)

    def test_rejects_wrong_wrapper_contract(self) -> None:
        result = self.run_diag(GOOD.replace("GameNative-1.2.0@3491226f", "GameNative-current"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GameNative 1.2.0 wrapper contract", result.stdout + result.stderr)

    def test_rejects_custom_backend(self) -> None:
        result = self.run_diag(GOOD.replace("backend: system-vulkan", "backend: adrenotools-custom"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("system Vulkan backend", result.stdout + result.stderr)

    def test_rejects_missing_pnext_chain(self) -> None:
        result = self.run_diag(GOOD.replace("incoming pNext: present", "incoming pNext: none"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incoming LSFG feature pNext chain", result.stdout + result.stderr)

    def test_rejects_non_xclipse_driver(self) -> None:
        result = self.run_diag(GOOD.replace("Samsung (Xclipse)", "Mesa Turnip (Adreno)"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Samsung Xclipse Vulkan driver", result.stdout + result.stderr)

    def test_rejects_failed_device_creation(self) -> None:
        result = self.run_diag(GOOD.replace("result: 0 (VK_SUCCESS)", "result: -8 (FAILED)"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shared-device vkCreateDevice success", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
