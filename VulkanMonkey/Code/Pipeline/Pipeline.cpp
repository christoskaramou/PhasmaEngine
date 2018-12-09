#include "Pipeline.h"
#include <iostream>

using namespace vm;

void Pipeline::destroy()
{
	if (pipeinfo.layout) {
		vulkan->device.destroyPipelineLayout(pipeinfo.layout);
		pipeinfo.layout = nullptr;
	}

	if (compinfo.layout) {
		vulkan->device.destroyPipelineLayout(compinfo.layout);
		pipeinfo.layout = nullptr;
	}

	if (pipeline) {
		vulkan->device.destroyPipeline(pipeline);
		pipeline = nullptr;
	}
}