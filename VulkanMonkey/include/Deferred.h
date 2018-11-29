#pragma once

#include "VulkanContext.h"
#include "Pipeline.h"
#include "Image.h"
#include "Light.h"
#include <map>
#include <vector>

namespace vm {
	struct Deferred
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		vk::RenderPass renderPass, compositionRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, compositionFrameBuffers{};
		vk::DescriptorSet DSComposition;
		vk::DescriptorSetLayout DSLayoutComposition;
		Pipeline pipeline;
		Pipeline pipelineComposition;

		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void destroy();
	};
}