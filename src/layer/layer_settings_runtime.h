#pragma once

#include <string>

#include <vulkan/vulkan.h>

#include "layer_format_virtualization.h"
#include "layer_settings_types.h"

bool load_layer_settings_from_ini(const std::string& path, LayerSettingsSnapshot* io_settings);
void apply_layer_settings_from_create_info(const VkInstanceCreateInfo* pCreateInfo, LayerSettingsSnapshot* io_settings);
void maybe_load_layer_settings_ini(LayerSettingsSnapshot* io_settings);
LayerSettingsSnapshot snapshot_layer_settings();
void commit_layer_settings(const LayerSettingsSnapshot& settings);
void refresh_layer_settings(const VkInstanceCreateInfo* pCreateInfo);
VirtualizationPolicySettings snapshot_virtualization_policy_settings();
