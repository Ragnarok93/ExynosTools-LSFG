#pragma once

struct XclipseDxvk2PolicyInput {
    bool is_xclipse = false;
    bool is_dxvk_2_or_newer = false;
    bool conservative_mode = true;
};

struct XclipseDxvk2Policy {
    bool active = false;

    // Optional DXVK 2.x acceleration paths. These may be hidden when the
    // Xclipse conservative profile is active so DXVK stays on Vulkan 1.3 core
    // paths that have broader Samsung-driver coverage.
    bool hide_extended_dynamic_state3 = false;
    bool hide_graphics_pipeline_library = false;
    bool hide_descriptor_buffer = false;

    // Required DXVK 2.x functionality is deliberately never hidden by this
    // policy. If semantic probes show these paths are broken, the layer needs
    // a real workaround/emulation path rather than extension spoofing.
    bool hide_robustness2 = false;
    bool hide_transform_feedback = false;
    bool robustness2_requires_semantic_validation = false;
    bool transform_feedback_requires_semantic_validation = false;
};

XclipseDxvk2Policy compute_xclipse_dxvk2_policy(const XclipseDxvk2PolicyInput& input);
