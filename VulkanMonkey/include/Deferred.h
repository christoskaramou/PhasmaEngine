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
		VulkanContext* vulkan;
		Deferred(VulkanContext* vulkan);

		vk::RenderPass renderPass, compositionRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, compositionFrameBuffers{};
		vk::DescriptorSet DSDeferredMainLight, DSComposition;
		vk::DescriptorSetLayout DSLayoutComposition;
		Pipeline pipeline = Pipeline(vulkan);
		Pipeline pipelineComposition = Pipeline(vulkan);

		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void destroy();
	};
}