#pragma once

#include "VulkanContext.h"
#include "Image.h"
#include "Pipeline.h"
#include <vector>

namespace vm {
	struct Forward
	{
		VulkanContext* vulkan;
		Forward(VulkanContext* vulkan);

		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		Image MSColorImage = Image(vulkan);
		Image MSDepthImage = Image(vulkan);
		Pipeline pipeline = Pipeline(vulkan);

		void destroy();
	};
}