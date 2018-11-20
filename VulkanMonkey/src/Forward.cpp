#include "../include/Forward.h"
#include <iostream>

void vm::Forward::destroy(vk::Device device)
{
	if (renderPass) {
		device.destroyRenderPass(renderPass);
		renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	MSColorImage.destroy(device);
	MSDepthImage.destroy(device);
	pipeline.destroy(device);
}
