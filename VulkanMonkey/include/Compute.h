#pragma once

#include "VulkanContext.h"
#include "Buffer.h"
#include "Pipeline.h"
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