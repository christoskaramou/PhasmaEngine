#include "../include/Pipeline.h"
#include <iostream>

using namespace vm;

void Pipeline::destroy(vk::Device device)
{
	if (pipeinfo.layout) {
		device.destroyPipelineLayout(this->pipeinfo.layout);
		pipeinfo.layout = nullptr;
		std::cout << "Pipeline Layout destroyed\n";
	}

	if (compinfo.layout) {
		device.destroyPipelineLayout(this->compinfo.layout);
		pipeinfo.layout = nullptr;
		std::cout << "Compute Pipeline Layout destroyed\n";
	}

	if (pipeline) {
		device.destroyPipeline(pipeline);
		pipeline = nullptr;
		std::cout << "Pipeline destroyed\n";
	}
}