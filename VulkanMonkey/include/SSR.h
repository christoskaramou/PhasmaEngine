#pragma once

#include "VulkanContext.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Image.h"
#include "Math.h"
#include <vector>
#include <map>

namespace vm {
	struct SSR
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer UBReflection;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet  DSReflection;
		vk::DescriptorSetLayout DSLayoutReflection;

		void createSSRUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex, vk::Extent2D actualExtent, const vm::vec2 UVOffset[2]);
		void destroy();
	};
}