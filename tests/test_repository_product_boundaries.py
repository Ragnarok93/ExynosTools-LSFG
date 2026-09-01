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

    def test_stale_packagers_and_patch_helpers_are_removed(self):
        self.assertFalse((ROOT / "scripts/package_driver_release.ps1").exists())
        self.assertFalse((ROOT / "scripts/package_debug_layer_release.ps1").exists())
        self.assertFalse((ROOT / "tools/apply_lsfg_compat_layer_entry.sh").exists())
        self.assertTrue((ROOT / "scripts/package_emulator_driver.py").is_file())

    def test_dependency_bootstrap_is_single_pinned_path(self):
        self.assertFalse((ROOT / "scripts/bootstrap_submodules.ps1").exists())
        self.assertFalse((ROOT / "scripts/configure_android_local_repos.ps1").exists())
        bootstrap = ROOT / "scripts/bootstrap_vulkan_deps.sh"
        self.assertTrue(bootstrap.is_file())
        text = bootstrap.read_text()
        self.assertIn('VULKAN_TAG="${VULKAN_TAG:-v1.4.341}"', text)
        self.assertIn("Vulkan-Headers", text)
        self.assertIn("Vulkan-Utility-Libraries", text)

    def test_generated_products_and_local_deps_are_ignored(self):
        text = (ROOT / ".gitignore").read_text()
        for needle in (
            "dist/",
            "*.so",
            "*.zip",
            "*.wcp",
            "external/Vulkan-Headers/",
            "external/Vulkan-Utility-Libraries/",
        ):
            self.assertIn(needle, text)


if __name__ == "__main__":
    unittest.main()
