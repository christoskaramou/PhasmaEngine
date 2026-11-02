#pragma once

#include "API/Vulkan/VulkanHeaders.h"

namespace pe
{
    // One VkPipelineCache for every Vulkan pipeline, kept on disk between runs (ShaderCache/_pipelines/vulkan.bin). It is
    // internally synchronized, so worker-thread builds (MaterialX modules, RT hit groups) share it. A file from another
    // GPU or driver, a truncated or corrupted one, or one past the size cap is ignored and the cache starts empty.
    vk::PipelineCache LoadVulkanPipelineCache(vk::Device device, vk::PhysicalDevice gpu);

    // Waits for a worker's save, writes the cache's data (through a temporary file, so a crash never leaves a torn file)
    // and destroys the cache.
    void SaveAndDestroyVulkanPipelineCache(vk::Device device, vk::PipelineCache cache);

    // Copies the cache's data and writes the file on a worker thread, at most every 5 s with the last request always
    // written: after a large build, so a killed or crashed process (the test harness stops editors that way) keeps what
    // it compiled.
    void SaveVulkanPipelineCacheAsync(vk::Device device, vk::PipelineCache cache);
} // namespace pe
