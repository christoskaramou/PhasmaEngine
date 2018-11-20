#pragma once

#include "Vulkan.h"
#include "Pipeline.h"
#include "Image.h"
#include "Light.h"
#include <map>
#include <vector>

namespace vm {
	struct Deferred
	{
		vk::RenderPass renderPass, compositionRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, compositionFrameBuffers{};
		vk::DescriptorSet DSDeferredMainLight, DSComposition;
		vk::DescriptorSetLayout DSLayoutComposition;
		Pipeline pipeline, pipelineComposition;

		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms, vk::Device device, vk::DescriptorPool descriptorPool);
		void destroy(vk::Device device);
	};
}