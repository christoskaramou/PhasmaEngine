#pragma once

#include "VulkanContext.h"
#include "Math.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Image.h"
#include <map>

namespace vm {
	struct SSAO
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer UBssaoKernel;
		Buffer UBssaoPVM;
		Image ssaoNoise;
		vk::RenderPass renderPass, blurRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, blurFrameBuffers{};
		Pipeline pipeline;
		Pipeline pipelineBlur;
		vk::DescriptorSetLayout DSLayoutSSAO, DSLayoutSSAOBlur;
		vk::DescriptorSet DSssao, DSssaoBlur;

		void createSSAOUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex, vk::Extent2D actualExtent, const vm::vec2 UVOffset[2]);
		void destroy();
	};
}