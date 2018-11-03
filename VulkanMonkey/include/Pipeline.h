#pragma once
#include "Vulkan.h"

struct Pipeline
{
	vk::Pipeline pipeline;
	vk::GraphicsPipelineCreateInfo pipeinfo;
	vk::ComputePipelineCreateInfo compinfo;

	void destroy(vk::Device device);
};