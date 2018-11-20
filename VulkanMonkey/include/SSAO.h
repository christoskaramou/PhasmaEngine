#pragma once

#include "Math.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Image.h"
#include <map>

namespace vm {
	struct SSAO
	{
		Buffer UBssaoKernel, UBssaoPVM;
		Image ssaoNoise;
		vk::RenderPass renderPass, blurRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, blurFrameBuffers{};
		Pipeline pipeline, pipelineBlur;
		vk::DescriptorSetLayout DSLayoutSSAO, DSLayoutSSAOBlur;
		vk::DescriptorSet DSssao, DSssaoBlur;

		void createSSAOUniforms(std::map<std::string, Image>& renderTargets, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool);
		void destroy(vk::Device device);
	};
}