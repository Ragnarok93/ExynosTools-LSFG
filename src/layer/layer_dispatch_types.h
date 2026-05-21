#pragma once

#include <vulkan/vulkan.h>

struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 get_physical_device_properties2 = nullptr;
    PFN_vkGetPhysicalDeviceFeatures get_physical_device_features = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 get_physical_device_features2 = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties get_physical_device_format_properties = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties get_physical_device_image_format_properties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties2 get_physical_device_format_properties2 = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 get_physical_device_image_format_properties2 = nullptr;
#ifdef VK_KHR_get_physical_device_properties2
    PFN_vkGetPhysicalDeviceProperties2KHR get_physical_device_properties2_khr = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2KHR get_physical_device_features2_khr = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties2KHR get_physical_device_format_properties2_khr = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2KHR get_physical_device_image_format_properties2_khr = nullptr;
#endif
};
