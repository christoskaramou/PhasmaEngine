#include "Forward.h"
#include <iostream>

using namespace vm;

void Forward::destroy()
{
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	MSColorImage.destroy();
	MSDepthImage.destroy();
	pipeline.destroy();
}
