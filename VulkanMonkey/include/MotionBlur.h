#pragma once

#include "VulkanContext.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Image.h"
#include "Surface.h"
#include "Swapchain.h"
#include "GUI.h"
#include "Timer.h"
#include <vector>
#include <string>
#include <map>

namespace vm {
	struct MotionBlur
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer UBmotionBlur;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		vk::RenderPass renderPass;
		vk::DescriptorSet DSMotionBlur;
		vk::DescriptorSetLayout DSLayoutMotionBlur;
		
		void createMotionBlurFrameBuffers();
		void createMotionBlurUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
		void draw(uint32_t imageIndex, const vec2 UVOffset[2]);
		void destroy();
	};
}