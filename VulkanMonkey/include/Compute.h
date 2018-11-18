#pragma once

#include "Vulkan.h"
#include "Buffer.h"
#include "Pipeline.h"

struct Compute
{
	Buffer SBInOut;
	vk::DescriptorSet DSCompute;
	vk::DescriptorSetLayout DSLayoutCompute;
	Pipeline pipelineCompute;
	void createComputeUniforms(vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool);
};