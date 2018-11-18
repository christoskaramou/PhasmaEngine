#pragma once

#include "Buffer.h"
#include "Pipeline.h"
#include "Vulkan.h"
#include "Image.h"
#include <vector>
#include <map>

struct SSR
{
	Buffer UBReflection;
	std::vector<vk::Framebuffer> ssrFrameBuffers{};
	Pipeline pipelineSSR;
	vk::RenderPass ssrRenderPass;
	vk::DescriptorSet  DSReflection;
	vk::DescriptorSetLayout DSLayoutReflection;

	void createSSRUniforms(std::map<std::string, Image>& renderTargets, vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool);
};