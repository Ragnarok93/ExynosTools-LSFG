import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

EXPECTED_SHA = "3491226faedb7222a5f8b7248c0247957a060836"
REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "tests/lsfg_compat_contract.py"


class StockGameNative120ContractTest(unittest.TestCase):
    @staticmethod
    def make_fixture(root: Path) -> None:
        manager = root / "app/src/main/java/app/gamenative/utils/LsfgVkManager.kt"
        launcher = root / (
            "app/src/main/java/com/winlator/xenvironment/components/"
            "BionicProgramLauncherComponent.java"
        )
        manager.parent.mkdir(parents=True)
        launcher.parent.mkdir(parents=True)
        manager.write_text(
            '\nENV_CONFIG = "LSFG_CONFIG"\n'
            'ENV_PROCESS = "LSFG_PROCESS"\n'
            'RUNTIME_VERSION = "v1.3.3-android-arm64-v8a"\n'
            "VK_LAYER_PATH\n"
            "VkLayer_LS_frame_generation.json\n"
            "liblsfg-vk-layer.so\n"
        )
        launcher.write_text("LsfgVkManager\n")

    def run_contract(self, fake_sha: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            gamenative = td_path / "GameNative"
            self.make_fixture(gamenative)

            bindir = td_path / "bin"
            bindir.mkdir()
            git = bindir / "git"
            git.write_text("#!/bin/sh\nprintf '%s\\n' \"$FAKE_GIT_SHA\"\n")
            git.chmod(0o755)

            env = os.environ.copy()
            env["FAKE_GIT_SHA"] = fake_sha
            env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
            return subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--repo",
                    str(REPO),
                    "--gamenative-root",
                    str(gamenative),
                ],
                text=True,
                capture_output=True,
                env=env,
            )

    def test_accepts_exact_stock_gamenative_120_revision(self) -> None:
        result = self.run_contract(EXPECTED_SHA)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS: stock GameNative v1.2.0 revision", result.stdout)
        self.assertIn("PASS: Bionic LSFG launch integration", result.stdout)

    def test_rejects_any_other_gamenative_revision(self) -> None:
        result = self.run_contract("deadbeef")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("wrong GameNative revision", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
