#pragma once

#include "Math.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Image.h"
#include <map>

struct SSAO
{
	Buffer UBssaoKernel, UBssaoPVM;
	Image ssaoNoise;
	vk::RenderPass ssaoRenderPass, ssaoBlurRenderPass;
	std::vector<vk::Framebuffer> ssaoFrameBuffers{}, ssaoBlurFrameBuffers{};
	Pipeline pipelineSSAO, pipelineSSAOBlur;
	vk::DescriptorSetLayout DSLayoutSSAO, DSLayoutSSAOBlur;
	vk::DescriptorSet DSssao, DSssaoBlur;

	void createSSAOUniforms(std::map<std::string, Image>& renderTargets, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool);
};