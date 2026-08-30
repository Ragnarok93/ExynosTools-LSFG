#include "layer_dxvk2_xclipse_policy.h"

XclipseDxvk2Policy compute_xclipse_dxvk2_policy(const XclipseDxvk2PolicyInput& input) {
    XclipseDxvk2Policy policy{};

    policy.active =
        input.is_xclipse &&
        input.is_dxvk_2_or_newer &&
        input.conservative_mode;

    if (!policy.active) {
        return policy;
    }

    // These are optional DXVK optimization paths. Keeping them out of the
    // Xclipse conservative profile reduces exposure to vendor-specific state
    // tracking and descriptor implementations while retaining Vulkan 1.3 core
    // functionality.
    policy.hide_extended_dynamic_state3 = true;
    policy.hide_graphics_pipeline_library = true;
    policy.hide_descriptor_buffer = true;

    // DXVK 2.x depends on robustness2/null descriptors and D3D10/11 depends on
    // transform feedback. Hiding either makes the device ineligible instead of
    // fixing rendering. Flag them for semantic validation and leave them
    // visible until an actual workaround/emulation path exists.
    policy.hide_robustness2 = false;
    policy.hide_transform_feedback = false;
    policy.robustness2_requires_semantic_validation = true;
    policy.transform_feedback_requires_semantic_validation = true;

    return policy;
}
