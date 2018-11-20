#pragma once

#include "Buffer.h"
#include "Pipeline.h"
#include "Vulkan.h"
#include "Image.h"
#include <vector>
#include <map>

namespace vm {
	struct SSR
	{
		Buffer UBReflection;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet  DSReflection;
		vk::DescriptorSetLayout DSLayoutReflection;

		void createSSRUniforms(std::map<std::string, Image>& renderTargets, vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool);
		void destroy(vk::Device device);
	};
}