#pragma once

#include "VulkanContext.h"
#include "Image.h"
#include "Pipeline.h"
#include <vector>

namespace vm {
	struct Forward
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		Image MSColorImage;
		Image MSDepthImage;
		Pipeline pipeline;

		void destroy();
	};
}