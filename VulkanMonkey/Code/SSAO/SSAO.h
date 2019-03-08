#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../GUI/GUI.h"
#include "../Camera/Camera.h"
#include <map>

namespace vm {
	struct SSAO
	{
		VulkanContext* vulkan = &VulkanContext::get();

		Buffer UB_Kernel;
		Buffer UB_PVM;
		Image noiseTex;
		vk::RenderPass renderPass, blurRenderPass;
		std::vector<vk::Framebuffer> frameBuffers{}, blurFrameBuffers{};
		Pipeline pipeline;
		Pipeline pipelineBlur;
		vk::DescriptorSetLayout DSLayout, DSLayoutBlur;
		vk::DescriptorSet DSet, DSBlur;

		void update(Camera& camera);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex, const vec2 UVOffset[2]);
		void destroy();
	};
}