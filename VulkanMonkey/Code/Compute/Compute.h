#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../VulkanContext/VulkanContext.h"  // for VulkanContext

namespace vm {
	struct Compute
	{
		VulkanContext* vulkan = &VulkanContext::get();

		Buffer SBInOut;
		vk::DescriptorSet DSCompute;
		vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;
		void createComputeUniforms();
		void destroy();
	};
}