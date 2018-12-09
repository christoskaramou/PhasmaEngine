#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../GUI/GUI.h"
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
		void draw(uint32_t imageIndex, const vec2 UVOffset[2]);
		void destroy();
	};
}