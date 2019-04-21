#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include <map>
#include <functional>

namespace vm {
	struct Bloom
	{
		VulkanContext* vulkan = &VulkanContext::get();

		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipelineBrightFilter;
		Pipeline pipelineGaussianBlurHorizontal;
		Pipeline pipelineGaussianBlurVertical;
		Pipeline pipelineCombine;
		vk::RenderPass renderPassBrightFilter;
		vk::RenderPass renderPassGaussianBlur;
		vk::RenderPass renderPassCombine;
		vk::DescriptorSet DSBrightFilter;
		vk::DescriptorSet DSGaussianBlurHorizontal;
		vk::DescriptorSet DSGaussianBlurVertical;
		vk::DescriptorSet DSCombine;
		vk::DescriptorSetLayout DSLayoutBrightFilter;
		vk::DescriptorSetLayout DSLayoutGaussianBlurHorizontal;
		vk::DescriptorSetLayout DSLayoutGaussianBlurVertical;
		vk::DescriptorSetLayout DSLayoutCombine;

		void update();
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex, uint32_t totalImages, const vec2 UVOffset[2], std::function<void(Image&, LayoutState)>&& changeLayout, std::map<std::string, Image>& renderTargets);
		void destroy();
	};
}