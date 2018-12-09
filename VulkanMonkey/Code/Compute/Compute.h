#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include <vector>

namespace vm {
	struct Compute
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer SBInOut;
		vk::DescriptorSet DSCompute;
		vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;
		void createComputeUniforms();
		void destroy();
	};
}