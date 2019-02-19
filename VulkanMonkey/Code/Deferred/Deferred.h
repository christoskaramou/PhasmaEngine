#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Light/Light.h"
#include "../GUI/GUI.h"
#include "../Shadows/Shadows.h"
#include "../SkyBox/Skybox.h"
#include <map>

namespace vm {
	struct Deferred
	{
		VulkanContext* vulkan = &VulkanContext::get();

		vk::RenderPass renderPass, compositionRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, compositionFrameBuffers{};
		vk::DescriptorSet DSComposition;
		vk::DescriptorSetLayout DSLayoutComposition;
		Pipeline pipeline;
		Pipeline pipelineComposition;

		void createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms);
		void draw(uint32_t imageIndex, Shadows& shadows, SkyBox& skybox, mat4& invViewProj, vec2 UVOffset[2]);
		void destroy();
	};
}