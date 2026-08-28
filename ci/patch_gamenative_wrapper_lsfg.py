#!/usr/bin/env python3
"""Patch leegao's GameNative Vulkan wrapper for LSFG/Xclipse shared-device use.

This intentionally keeps the wrapper as the single Vulkan ICD shim used by stock
GameNative.  It does not alter GameNative itself and does not fabricate LSFG
features.  The only LSFG-specific behavior is to defer BCn compute-pipeline
creation until a real emulated BCn copy needs it.
"""
from __future__ import annotations

import sys
from pathlib import Path


def replace_once(text: str, old: str, new: str, what: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{what}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_gamenative_wrapper_lsfg.py <wrapper-source-root>")

    root = Path(sys.argv[1]).resolve()
    objects = root / "src/vulkan/wrapper/wrapper_objects.h"
    device_c = root / "src/vulkan/wrapper/wrapper_device.c"
    physical_c = root / "src/vulkan/wrapper/wrapper_physical_device.c"
    for path in (objects, device_c, physical_c):
        if not path.is_file():
            raise SystemExit(f"missing wrapper source: {path}")

    # Per-device state for thread-safe lazy BCn initialization.
    s = objects.read_text()
    old = """    // BCn decoding\n    InterceptorState s3tc;\n    InterceptorState bc6;\n    InterceptorState bc7;\n"""
    new = old + """\n    // ExynosTools: LSFG uses the game's existing device.  Do not create BCn\n    // compute pipelines during vkCreateDevice in that shared-device path.\n    simple_mtx_t bcn_init_mutex;\n    bool bcn_interceptors_initialized;\n    bool bcn_interceptors_failed;\n    bool lsfg_shared_device;\n"""
    s = replace_once(s, old, new, "wrapper_device BCn state")
    objects.write_text(s)

    s = device_c.read_text()

    # Insert helpers immediately before WRAPPER_CreateDevice.  The helper uses
    # the exact same pipelines/options as upstream; it merely changes *when*
    # they are created for LSFG processes.
    marker = "\nWRAPPER_CreateDevice(VkPhysicalDevice physicalDevice,\n"
    if s.count(marker) != 1:
        raise SystemExit("WRAPPER_CreateDevice marker changed")
    helper = r'''
static bool
exynostools_nonempty_env(const char *name)
{
   const char *value = getenv(name);
   return value && value[0] != '\0';
}

static bool
exynostools_lsfg_shared_device(void)
{
   return exynostools_nonempty_env("LSFG_PROCESS") &&
          exynostools_nonempty_env("LSFG_CONFIG");
}

static VkResult
exynostools_ensure_bcn_interceptors(struct wrapper_device *device)
{
   VkResult result = VK_SUCCESS;
   simple_mtx_lock(&device->bcn_init_mutex);

   if (device->bcn_interceptors_initialized)
      goto done;
   if (device->bcn_interceptors_failed) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto done;
   }

   bool validate_bcn = get_validate_bcn_masks() > 0;
   bool dump_artifacts = (get_dump_bcn_masks() > 0) || validate_bcn;
   bool use_image_view = use_image_view_mode() && !dump_artifacts;

   result = InterceptorState_Init(&device->s3tc,
      wrapper_device_to_handle(device),
      use_image_view ? sizeof(s3tc_iv_spv) : sizeof(s3tc_spv),
      use_image_view ? s3tc_iv_spv : s3tc_spv,
      use_image_view, 1);
   if (result != VK_SUCCESS) {
      WLOGE("ExynosTools: failed to lazily initialize s3tc BCn state");
      goto failed;
   }

   result = InterceptorState_Init(&device->bc6,
      wrapper_device_to_handle(device),
      use_image_view ? sizeof(bc6_iv_spv) : sizeof(bc6_spv),
      use_image_view ? bc6_iv_spv : bc6_spv,
      use_image_view, 6);
   if (result != VK_SUCCESS) {
      WLOGE("ExynosTools: failed to lazily initialize bc6 BCn state");
      goto failed;
   }

   result = InterceptorState_Init(&device->bc7,
      wrapper_device_to_handle(device),
      use_image_view ? sizeof(bc7_iv_spv) : sizeof(bc7_spv),
      use_image_view ? bc7_iv_spv : bc7_spv,
      use_image_view, 7);
   if (result != VK_SUCCESS) {
      WLOGE("ExynosTools: failed to lazily initialize bc7 BCn state");
      goto failed;
   }

   device->bcn_interceptors_initialized = true;
   WLOGD("ExynosTools: BCn compute interceptors initialized on first real BCn use");
   goto done;

failed:
   // Do not repeatedly construct partial Vulkan state on a shared LSFG device.
   // The BCn copy path falls back to the existing host decoder after this.
   device->bcn_interceptors_failed = true;

done:
   simple_mtx_unlock(&device->bcn_init_mutex);
   return result;
}
'''
    s = s.replace(marker, "\n" + helper + marker, 1)

    # Initialize the dedicated mutex and snapshot LSFG env while creating the
    # same logical VkDevice that LSFG will later use.
    old = """   simple_mtx_init(&device->resource_mutex, mtx_plain);\n   device->physical = physical_device;\n"""
    new = """   simple_mtx_init(&device->resource_mutex, mtx_plain);\n   simple_mtx_init(&device->bcn_init_mutex, mtx_plain);\n   device->lsfg_shared_device = exynostools_lsfg_shared_device();\n   device->physical = physical_device;\n"""
    s = replace_once(s, old, new, "device mutex initialization")

    # Replace upstream eager BCn pipeline creation.  Non-LSFG behavior remains
    # eager to minimize behavioral change; LSFG gets lazy initialization.
    start_tag = "   // Initialize the BCn interceptor states\n"
    end_tag = "\n   result = wrapper_create_device_queue(device, pCreateInfo);\n"
    start = s.find(start_tag)
    if start < 0:
        raise SystemExit("eager BCn init start changed")
    end = s.find(end_tag, start)
    if end < 0:
        raise SystemExit("eager BCn init end changed")
    replacement = r'''   if (device->lsfg_shared_device) {
      WLOG("ExynosTools LSFG: deferring eager BCn compute initialization on shared device");
   } else {
      result = exynostools_ensure_bcn_interceptors(device);
      if (result != VK_SUCCESS) {
         WLOGE("Failed to initialize BCn interceptor states");
         return vk_error(physical_device, result);
      }
   }
'''
    s = s[:start] + replacement + s[end:]

    # In the only compute BCn consumption path, initialize lazily.  If Samsung
    # rejects the helper pipelines, use upstream's CPU BCn fallback instead of
    # failing the LSFG device.
    old = """   struct wrapper_buffer* wbuf = get_wrapper_buffer(_device, srcBuffer);\n"""
    new = """   if (use_compute_shader && !_device->bcn_interceptors_initialized) {\n      result = exynostools_ensure_bcn_interceptors(_device);\n      if (result != VK_SUCCESS) {\n         WLOGE(\"ExynosTools LSFG: BCn compute init failed; falling back to host BCn decode\");\n         use_compute_shader = false;\n         use_image_view = false;\n      }\n   }\n\n   struct wrapper_buffer* wbuf = get_wrapper_buffer(_device, srcBuffer);\n"""
    # This identifier occurs elsewhere, so target the instance in the BCn copy
    # function by slicing from WRAPPER_CmdCopyBufferToImage.
    fn = s.find("WRAPPER_CmdCopyBufferToImage(")
    if fn < 0:
        raise SystemExit("BCn copy hook changed")
    pos = s.find(old, fn)
    if pos < 0:
        raise SystemExit("BCn lazy-init insertion point changed")
    s = s[:pos] + new + s[pos + len(old):]

    # Destroy only the mutex we add.  The upstream wrapper already owns the
    # Vulkan resources associated with its device and tracked buffers/memory.
    old = """   simple_mtx_destroy(&device->resource_mutex);\n   vk_device_finish(&device->vk);\n"""
    new = """   simple_mtx_destroy(&device->bcn_init_mutex);\n   simple_mtx_destroy(&device->resource_mutex);\n   vk_device_finish(&device->vk);\n"""
    s = replace_once(s, old, new, "device mutex destruction")

    device_c.write_text(s)

    # Hard invariant: LSFG-required features must remain genuine passthrough.
    # The wrapper has some deliberate DXVK compatibility fabrication for legacy
    # features; forbid extending that mechanism to the LSFG feature set.
    p = physical_c.read_text()
    forbidden_true = (
        "supported_features->shaderFloat16 = true",
        "supported_features->shaderStorageImageExtendedFormats = true",
        "supported_features->shaderStorageImageReadWithoutFormat = true",
        "supported_features->shaderStorageImageWriteWithoutFormat = true",
        "supported_features->shaderInt16 = true",
        "supported_features->vulkanMemoryModel = true",
        "supported_features->timelineSemaphore = true",
        "supported_features->synchronization2 = true",
    )
    found = [needle for needle in forbidden_true if needle in p]
    if found:
        raise SystemExit("refusing LSFG feature fabrication: " + ", ".join(found))

    # Source-level postconditions used by CI and by reviewers.
    final_objects = objects.read_text()
    final_device = device_c.read_text()
    for needle in (
        "bool lsfg_shared_device;",
        "simple_mtx_t bcn_init_mutex;",
        'exynostools_nonempty_env("LSFG_PROCESS")',
        'exynostools_nonempty_env("LSFG_CONFIG")',
        "deferring eager BCn compute initialization on shared device",
        "BCn compute init failed; falling back to host BCn decode",
    ):
        if needle not in final_objects + final_device:
            raise SystemExit(f"missing postcondition: {needle}")

    print("ExynosTools LSFG wrapper patch applied successfully")


if __name__ == "__main__":
    main()
