#pragma once

#include "Vulkan.h"
#include "Buffer.h"
#include "Pipeline.h"
namespace vm {
	struct Compute
	{
		Buffer SBInOut;
		vk::DescriptorSet DSCompute;
		vk::DescriptorSetLayout DSLayoutCompute;
		Pipeline pipeline;
		void createComputeUniforms(vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool);
		void destroy(vk::Device device);
	};
}