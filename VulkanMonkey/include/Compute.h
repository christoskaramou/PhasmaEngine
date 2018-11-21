#pragma once

#include "VulkanContext.h"
#include "Buffer.h"
#include "Pipeline.h"
namespace vm {
	struct Compute
	{
		VulkanContext* vulkan;
		Compute(VulkanContext* vulkan);

		Buffer SBInOut = Buffer(vulkan);
		vk::DescriptorSet DSCompute;
		vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline = Pipeline(vulkan);
		void createComputeUniforms();
		void destroy();
	};
}