#include "../include/Forward.h"
#include <iostream>

using namespace vm;

void Forward::destroy()
{
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	MSColorImage.destroy();
	MSDepthImage.destroy();
	pipeline.destroy();
}
