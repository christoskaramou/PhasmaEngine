#include "../include/Pipeline.h"
#include <iostream>

using namespace vm;

void Pipeline::destroy()
{
	if (pipeinfo.layout) {
		vulkan->device.destroyPipelineLayout(pipeinfo.layout);
		pipeinfo.layout = nullptr;
		std::cout << "Pipeline Layout destroyed\n";
	}

	if (compinfo.layout) {
		vulkan->device.destroyPipelineLayout(compinfo.layout);
		pipeinfo.layout = nullptr;
		std::cout << "Compute Pipeline Layout destroyed\n";
	}

	if (pipeline) {
		vulkan->device.destroyPipeline(pipeline);
		pipeline = nullptr;
		std::cout << "Pipeline destroyed\n";
	}
}