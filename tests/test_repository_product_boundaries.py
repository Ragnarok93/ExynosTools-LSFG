import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RepositoryProductBoundaryTests(unittest.TestCase):
    def test_general_emulator_workflow_builds_repository_layer(self):
        text = (ROOT / ".github/workflows/build-general-emulator-layer.yml").read_text()
        self.assertIn("--target VkLayer_VortekXclipse", text)
        self.assertIn("scripts/package_emulator_driver.py", text)
        self.assertIn("libVkLayer_VortekXclipse.so", text)
        self.assertNotIn("mesa-wrapper-CI", text)
        self.assertNotIn("libvulkan_wrapper.so", text)

    def test_gamenative_workflow_is_wrapper_only(self):
        path = ROOT / ".github/workflows/build-gamenative-wrapper.yml"
        self.assertTrue(path.is_file(), "GameNative wrapper workflow must be explicitly named")
        text = path.read_text()
        self.assertIn("mesa-wrapper-CI", text)
        self.assertIn("libvulkan_wrapper.so", text)
        self.assertIn('"type": "Wrapper"', text)
        self.assertNotIn("--target VkLayer_VortekXclipse", text)

    def test_ambiguous_old_driver_workflow_is_removed(self):
        self.assertFalse((ROOT / ".github/workflows/build-driver.yml").exists())

    def test_general_layer_metadata_agrees(self):
        meta = json.loads((ROOT / "meta.json").read_text())
        manifest = json.loads((ROOT / "VkLayer_vortek_xclipse.json").read_text())
        layer = manifest["layer"]
        self.assertEqual(meta["layerLibrary"], "libVkLayer_VortekXclipse.so")
        self.assertEqual(meta["layerName"], "VK_LAYER_VORTEK_XCLIPSE")
        self.assertEqual(layer["library_path"], meta["layerLibrary"])
        self.assertEqual(layer["name"], meta["layerName"])

    def test_lsfg_special_behavior_remains_gamenative_gated(self):
        compat = (ROOT / "src/layer/layer_lsfg_compat.cpp").read_text()
        self.assertIn('"LSFG_PROCESS"', compat)
        self.assertIn('"LSFG_CONFIG"', compat)
        self.assertIn("lsfg_process_environment_present", compat)

    def test_stale_packagers_are_removed(self):
        self.assertFalse((ROOT / "scripts/package_driver_release.ps1").exists())
        self.assertFalse((ROOT / "scripts/package_debug_layer_release.ps1").exists())
        self.assertTrue((ROOT / "scripts/package_emulator_driver.py").is_file())


if __name__ == "__main__":
    unittest.main()
