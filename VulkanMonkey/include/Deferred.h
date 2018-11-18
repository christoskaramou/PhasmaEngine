#pragma once

#include "Vulkan.h"
#include "Pipeline.h"
#include "Image.h"
#include "Light.h"
#include <map>
#include <vector>

struct Deferred
{
	vk::RenderPass deferredRenderPass, compositionRenderPass;
	std::vector<vk::Framebuffer> deferredFrameBuffers{}, compositionFrameBuffers{};
	vk::DescriptorSet DSDeferredMainLight, DSComposition;
	vk::DescriptorSetLayout DSLayoutComposition;
	Pipeline pipelineDeferred, pipelineComposition;

	void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms, vk::Device device, vk::DescriptorPool descriptorPool);
};