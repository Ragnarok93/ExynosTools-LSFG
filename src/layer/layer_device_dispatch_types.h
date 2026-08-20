#pragma once

#include <vulkan/vulkan.h>

struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkBindBufferMemory bind_buffer_memory = nullptr;
    PFN_vkBindBufferMemory2 bind_buffer_memory2 = nullptr;
#ifdef VK_KHR_bind_memory2
    PFN_vkBindBufferMemory2KHR bind_buffer_memory2_khr = nullptr;
#endif
    PFN_vkMapMemory map_memory = nullptr;
    PFN_vkUnmapMemory unmap_memory = nullptr;
    PFN_vkAllocateMemory allocate_memory = nullptr;
    PFN_vkFreeMemory free_memory = nullptr;
#ifdef VK_KHR_external_memory_fd
    PFN_vkGetMemoryFdKHR get_memory_fd_khr = nullptr;
    PFN_vkGetMemoryFdPropertiesKHR get_memory_fd_properties_khr = nullptr;
#endif
#ifdef VK_KHR_external_semaphore_fd
    PFN_vkCreateSemaphore create_semaphore = nullptr;
    PFN_vkDestroySemaphore destroy_semaphore = nullptr;
    PFN_vkGetSemaphoreFdKHR get_semaphore_fd_khr = nullptr;
    PFN_vkImportSemaphoreFdKHR import_semaphore_fd_khr = nullptr;
#endif
#ifdef VK_KHR_external_fence_fd
    PFN_vkCreateFence create_fence = nullptr;
    PFN_vkDestroyFence destroy_fence = nullptr;
    PFN_vkGetFenceFdKHR get_fence_fd_khr = nullptr;
    PFN_vkImportFenceFdKHR import_fence_fd_khr = nullptr;
#endif
    PFN_vkCreateImage create_image = nullptr;
    PFN_vkDestroyImage destroy_image = nullptr;
    PFN_vkCreateImageView create_image_view = nullptr;
    PFN_vkDestroyImageView destroy_image_view = nullptr;
    PFN_vkCreateRenderPass create_render_pass = nullptr;
    PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
    PFN_vkCreateFramebuffer create_framebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroy_framebuffer = nullptr;
    PFN_vkCreateSampler create_sampler = nullptr;
    PFN_vkDestroySampler destroy_sampler = nullptr;
    PFN_vkCreateCommandPool create_command_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
    PFN_vkResetCommandPool reset_command_pool = nullptr;
    PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
    PFN_vkResetCommandBuffer reset_command_buffer = nullptr;
    PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
    PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
    PFN_vkCreateShaderModule create_shader_module = nullptr;
    PFN_vkDestroyShaderModule destroy_shader_module = nullptr;
    PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = nullptr;
    PFN_vkGetDescriptorSetLayoutSizeEXT get_descriptor_set_layout_size_ext = nullptr;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_descriptor_set_layout_binding_offset_ext = nullptr;
    PFN_vkGetDescriptorEXT get_descriptor_ext = nullptr;
    PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
    PFN_vkCreatePipelineCache create_pipeline_cache = nullptr;
    PFN_vkDestroyPipelineCache destroy_pipeline_cache = nullptr;
    PFN_vkGetPipelineCacheData get_pipeline_cache_data = nullptr;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
    PFN_vkCreateComputePipelines create_compute_pipelines = nullptr;
    PFN_vkDestroyPipeline destroy_pipeline = nullptr;
    PFN_vkCreateQueryPool create_query_pool = nullptr;
    PFN_vkDestroyQueryPool destroy_query_pool = nullptr;
    PFN_vkGetQueryPoolResults get_query_pool_results = nullptr;
    PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
    PFN_vkResetDescriptorPool reset_descriptor_pool = nullptr;
    PFN_vkAllocateDescriptorSets allocate_descriptor_sets = nullptr;
    PFN_vkFreeDescriptorSets free_descriptor_sets = nullptr;
    PFN_vkUpdateDescriptorSets update_descriptor_sets = nullptr;
    PFN_vkGetBufferDeviceAddress get_buffer_device_address = nullptr;
    PFN_vkGetBufferDeviceAddressKHR get_buffer_device_address_khr = nullptr;
    PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets = nullptr;
    PFN_vkCmdBindDescriptorBuffersEXT cmd_bind_descriptor_buffers_ext = nullptr;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT cmd_set_descriptor_buffer_offsets_ext = nullptr;
    PFN_vkCmdPushConstants cmd_push_constants = nullptr;
    PFN_vkCmdDispatch cmd_dispatch = nullptr;
    PFN_vkCmdResetQueryPool cmd_reset_query_pool = nullptr;
    PFN_vkCmdWriteTimestamp cmd_write_timestamp = nullptr;
    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
    PFN_vkCmdPipelineBarrier2 cmd_pipeline_barrier2 = nullptr;
#ifdef VK_KHR_synchronization2
    PFN_vkCmdPipelineBarrier2KHR cmd_pipeline_barrier2_khr = nullptr;
#endif
    PFN_vkCmdCopyBuffer cmd_copy_buffer = nullptr;
    PFN_vkCmdCopyImage cmd_copy_image = nullptr;
    PFN_vkCmdClearDepthStencilImage cmd_clear_depth_stencil_image = nullptr;
    PFN_vkCmdCopyImage2 cmd_copy_image2 = nullptr;
    PFN_vkCmdBlitImage cmd_blit_image = nullptr;
    PFN_vkCmdBlitImage2 cmd_blit_image2 = nullptr;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image = nullptr;
    PFN_vkCmdCopyBufferToImage2 cmd_copy_buffer_to_image2 = nullptr;
#ifdef VK_KHR_copy_commands2
    PFN_vkCmdCopyImage2KHR cmd_copy_image2_khr = nullptr;
    PFN_vkCmdBlitImage2KHR cmd_blit_image2_khr = nullptr;
    PFN_vkCmdCopyBufferToImage2KHR cmd_copy_buffer_to_image2_khr = nullptr;
#endif
};