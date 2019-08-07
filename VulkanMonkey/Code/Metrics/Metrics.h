#pragma once

#include "../VulkanContext/VulkanContext.h"
#include <vector>

namespace vm {
	struct Metrics {
		Metrics();
		~Metrics();
		void start(const vk::CommandBuffer& cmd);
		void end(float* res = nullptr);
		void initQueryPool();
		float getTime();
		void destroy() const;

		vk::QueryPool queryPool;
		std::vector<uint64_t> queryTimes{};

	private:
		vk::PhysicalDeviceProperties gpuProps;
		vk::CommandBuffer _cmd;
	};
}