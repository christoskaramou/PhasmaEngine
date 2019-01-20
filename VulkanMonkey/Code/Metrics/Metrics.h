#pragma once

#include "../VulkanContext/VulkanContext.h"
#include <vector>

namespace vm {
	struct Metrics {
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();
		
		Metrics();
		~Metrics();
		void start(vk::CommandBuffer& cmd);
		void end();
		void initQueryPool();
		float getGPUFrameTime();
		void destroy();

		vk::QueryPool queryPool;
		std::vector<uint64_t> queryTimes{};

	private:
		vk::PhysicalDeviceProperties gpuProps;
		vk::CommandBuffer _cmd;
	};
}