#pragma once

#include "Vulkan.h"
#include "Image.h"
#include "Pipeline.h"
#include <vector>

namespace vm {
	struct Forward
	{
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		Image MSColorImage, MSDepthImage;
		Pipeline pipeline;

		void destroy(vk::Device device);
	};
}