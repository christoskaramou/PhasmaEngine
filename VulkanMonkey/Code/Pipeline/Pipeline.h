#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include <vector>

namespace vm {
	struct Pipeline
	{
		vk::Pipeline pipeline;
		vk::GraphicsPipelineCreateInfo pipeinfo;
		vk::ComputePipelineCreateInfo compinfo;

		void destroy();
	};
}