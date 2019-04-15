#pragma once

#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm {
	struct Compute
	{
		VulkanContext* vulkan = &VulkanContext::get();

		Buffer SBIn;
		Buffer SBOut;
		vk::DescriptorSet DSCompute;
		static vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;

		static vk::DescriptorSetLayout getDescriptorLayout();
		void update();
		void createComputeStorageBuffers(size_t sizeIn, size_t sizeOut);
		void updateDescriptorSets();
		void destroy();
	};
}