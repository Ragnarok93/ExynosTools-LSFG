import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY_H = ROOT / "src" / "layer" / "layer_dxvk2_xclipse_policy.h"
POLICY_CPP = ROOT / "src" / "layer" / "layer_dxvk2_xclipse_policy.cpp"


class Dxvk2XclipsePolicyContractTest(unittest.TestCase):
    def test_policy_quarantines_only_optional_unverified_paths(self):
        self.assertTrue(POLICY_H.exists(), "DXVK2 Xclipse policy header is missing")
        self.assertTrue(POLICY_CPP.exists(), "DXVK2 Xclipse policy implementation is missing")

        harness = textwrap.dedent(
            r'''
            #include "layer_dxvk2_xclipse_policy.h"

            int main() {
                XclipseDxvk2PolicyInput input{};
                input.is_xclipse = true;
                input.is_dxvk_2_or_newer = true;
                input.conservative_mode = true;

                const XclipseDxvk2Policy p = compute_xclipse_dxvk2_policy(input);
                if (!p.active) return 1;
                if (!p.hide_extended_dynamic_state3) return 2;
                if (!p.hide_graphics_pipeline_library) return 3;
                if (!p.hide_descriptor_buffer) return 4;

                // Required DXVK 2.x features must never be silently hidden by
                // the conservative policy. They need semantic probing and, if
                // broken, a real workaround/emulation path.
                if (p.hide_robustness2) return 5;
                if (p.hide_transform_feedback) return 6;
                if (!p.robustness2_requires_semantic_validation) return 7;
                if (!p.transform_feedback_requires_semantic_validation) return 8;

                input.is_xclipse = false;
                const XclipseDxvk2Policy non_xclipse = compute_xclipse_dxvk2_policy(input);
                if (non_xclipse.active) return 9;
                if (non_xclipse.hide_extended_dynamic_state3) return 10;

                input.is_xclipse = true;
                input.is_dxvk_2_or_newer = false;
                const XclipseDxvk2Policy dxvk1 = compute_xclipse_dxvk2_policy(input);
                if (dxvk1.active) return 11;

                input.is_dxvk_2_or_newer = true;
                input.conservative_mode = false;
                const XclipseDxvk2Policy disabled = compute_xclipse_dxvk2_policy(input);
                if (disabled.active) return 12;
                if (disabled.hide_graphics_pipeline_library) return 13;

                return 0;
            }
            '''
        )

        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            harness_path = td_path / "policy_contract.cpp"
            binary_path = td_path / "policy_contract"
            harness_path.write_text(harness, encoding="utf-8")

            compile_cmd = [
                "c++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'src' / 'layer'}",
                str(POLICY_CPP),
                str(harness_path),
                "-o",
                str(binary_path),
            ]
            compiled = subprocess.run(compile_cmd, text=True, capture_output=True)
            self.assertEqual(
                compiled.returncode,
                0,
                msg=f"policy contract failed to compile:\n{compiled.stdout}\n{compiled.stderr}",
            )

            executed = subprocess.run([str(binary_path)], text=True, capture_output=True)
            self.assertEqual(
                executed.returncode,
                0,
                msg=f"policy contract returned {executed.returncode}:\n{executed.stdout}\n{executed.stderr}",
            )


if __name__ == "__main__":
    unittest.main()
