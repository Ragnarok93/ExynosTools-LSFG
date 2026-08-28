#!/usr/bin/env python3
"""Apply the minimal LSFG/Xclipse hardening to GameNative's pinned wrapper.

The July 2026 gn_wrapper2 source already has lazy, opt-in GPU BCn transcode, so
we deliberately do not replace its BCn implementation.  The important shared-
device hazard is WRAPPER_SAFE_CREATE_DEVICE: upstream retries a failed device
create with pNext=NULL.  LSFG v1.3.3 merges shaderFloat16/vulkanMemoryModel and
other required feature state into the game's device-create chain, so that retry
can make the game start with an LSFG-incapable VkDevice.

For an LSFG process this patch therefore:
  * never drops the VkDeviceCreateInfo pNext chain;
  * disables optional wrapper device-fault injection to keep the shared-device
    chain as close as possible to the game's + LSFG's requested state;
  * forces the optional GPU BCn transcode path off even if globally requested;
  * emits unambiguous markers that CI can verify in the final ELF.

No LSFG-required Vulkan feature is fabricated.  Physical support remains the
source of truth.
"""
from __future__ import annotations

import sys
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one source match, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_gamenative_wrapper_lsfg.py <wrapper-root>")

    root = Path(sys.argv[1]).resolve()
    device = root / "src/vulkan/wrapper/wrapper_device.c"
    physical = root / "src/vulkan/wrapper/wrapper_physical_device.c"
    instance = root / "src/vulkan/wrapper/wrapper_instance.c"
    for p in (device, physical, instance):
        if not p.is_file():
            raise SystemExit(f"missing pinned wrapper source: {p}")

    s = device.read_text()

    # Source-contract guards: this patch is intentionally tied to the pinned
    # GameNative wrapper revision.  Refuse to guess if upstream structure moves.
    required_upstream = (
        'getenv("WRAPPER_SAFE_CREATE_DEVICE")',
        'WRAPPER_LOG(info, "Forcing device creation with a NULL pNext chain")',
        'wrapper_create_info.pNext = NULL;',
        'getenv("WRAPPER_BCN_GPU")',
        'wrapper_bcn_gpu_ready(struct wrapper_device *device)',
        'process_pnext_chain((VkBaseInStructure *)&wrapper_create_info, device->physical);',
    )
    for needle in required_upstream:
        if needle not in s:
            raise SystemExit(f"pinned wrapper contract changed: {needle}")

    helper_marker = "\nVKAPI_ATTR VkResult VKAPI_CALL\nwrapper_CreateDevice(VkPhysicalDevice physicalDevice,\n"
    helper = r'''
static bool
exynostools_lsfg_active(void)
{
   const char *process = getenv("LSFG_PROCESS");
   const char *config = getenv("LSFG_CONFIG");
   return process && process[0] != '\0' && config && config[0] != '\0';
}
'''
    s = replace_once(s, helper_marker, "\n" + helper + helper_marker,
                     "CreateDevice insertion point")

    old = """   bool used_fallback_create = false;\n\n   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator,\n"""
    new = """   bool used_fallback_create = false;\n   const bool exynostools_lsfg = exynostools_lsfg_active();\n   if (exynostools_lsfg)\n      WRAPPER_LOG(info, \"ExynosTools LSFG: preserving shared-device feature chain\");\n\n   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator,\n"""
    s = replace_once(s, old, new, "LSFG CreateDevice state")

    old = """   bool enable_device_fault = wrapper_device_fault &&\n      physical_device->base_supported_extensions.EXT_device_fault;\n"""
    new = """   /* Device-fault reporting is diagnostic-only.  Do not append another\n    * optional feature struct to LSFG's shared-device chain on Xclipse. */\n   bool enable_device_fault = !exynostools_lsfg && wrapper_device_fault &&\n      physical_device->base_supported_extensions.EXT_device_fault;\n"""
    s = replace_once(s, old, new, "device-fault LSFG guard")

    old = """   if (result != VK_SUCCESS) {\n      if (wrapper_safe_create_device) {\n         WRAPPER_LOG(info, \"Forcing device creation with a NULL pNext chain\");\n         wrapper_create_info.pNext = NULL;\n         used_fallback_create = true;\n         result = physical_device->dispatch_table.CreateDevice(\n            physical_device->dispatch_handle, &wrapper_create_info,\n               pAllocator, &device->dispatch_handle);\n      }\n\n      if (result != VK_SUCCESS) {\n"""
    new = """   if (result != VK_SUCCESS) {\n      if (wrapper_safe_create_device && !exynostools_lsfg) {\n         WRAPPER_LOG(info, \"Forcing device creation with a NULL pNext chain\");\n         wrapper_create_info.pNext = NULL;\n         used_fallback_create = true;\n         result = physical_device->dispatch_table.CreateDevice(\n            physical_device->dispatch_handle, &wrapper_create_info,\n               pAllocator, &device->dispatch_handle);\n      } else if (wrapper_safe_create_device && exynostools_lsfg) {\n         WRAPPER_LOG(error,\n            \"ExynosTools LSFG: refusing NULL-pNext fallback; required LSFG features must stay enabled\");\n      }\n\n      if (result != VK_SUCCESS) {\n"""
    s = replace_once(s, old, new, "safe-create LSFG guard")

    old = """static bool\nwrapper_bcn_gpu_ready(struct wrapper_device *device)\n{\n   /* Default OFF. The GPU compute transcode is correct in isolation (its BC7\n"""
    new = """static bool\nwrapper_bcn_gpu_ready(struct wrapper_device *device)\n{\n   /* LSFG already runs a compute-heavy shared-device pipeline.  The wrapper's\n    * optional BCn GPU transcode is not needed for correctness (CPU fallback is\n    * authoritative) and must not inject compute work into LSFG presentation. */\n   if (exynostools_lsfg_active())\n      return false;\n\n   /* Default OFF. The GPU compute transcode is correct in isolation (its BC7\n"""
    s = replace_once(s, old, new, "BCn GPU LSFG guard")

    device.write_text(s)

    # Invariants against accidental feature fabrication.  The wrapper may fake
    # unrelated DXVK compatibility features, but these must remain genuine.
    p = physical.read_text()
    forbidden = (
        "supported_features->shaderFloat16 = true",
        "supported_features->shaderStorageImageExtendedFormats = true",
        "supported_features->shaderStorageImageReadWithoutFormat = true",
        "supported_features->shaderStorageImageWriteWithoutFormat = true",
        "supported_features->shaderInt16 = true",
        "supported_features->vulkanMemoryModel = true",
        "supported_features->vulkanMemoryModelDeviceScope = true",
        "supported_features->timelineSemaphore = true",
        "supported_features->synchronization2 = true",
    )
    fabricated = [needle for needle in forbidden if needle in p]
    if fabricated:
        raise SystemExit("refusing LSFG feature fabrication: " + ", ".join(fabricated))

    # The single-wrapper WCP must retain direct system-Vulkan fallback.  Custom
    # AdrenoTools chaining is intentionally not part of the LSFG test route.
    i = instance.read_text()
    if 'DEFAULT_VULKAN_PATH "/system/lib64/libvulkan.so"' not in i:
        raise SystemExit("wrapper no longer has the Android system Vulkan path")
    if "return dlopen(DEFAULT_VULKAN_PATH" not in i:
        raise SystemExit("wrapper no longer falls back to system Vulkan")

    final = device.read_text()
    postconditions = (
        "exynostools_lsfg_active",
        "ExynosTools LSFG: preserving shared-device feature chain",
        "ExynosTools LSFG: refusing NULL-pNext fallback",
        "if (exynostools_lsfg_active())\n      return false;",
    )
    for needle in postconditions:
        if needle not in final:
            raise SystemExit(f"missing postcondition: {needle}")

    print("ExynosTools LSFG shared-device hardening applied")


if __name__ == "__main__":
    main()
