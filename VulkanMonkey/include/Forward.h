#pragma once

#include "Vulkan.h"
#include "Image.h"
#include "Pipeline.h"
#include <vector>

struct Forward
{
	vk::RenderPass forwardRenderPass;
	std::vector<vk::Framebuffer> frameBuffers{};
	Image MSColorImage, MSDepthImage;
	Pipeline pipeline;
};