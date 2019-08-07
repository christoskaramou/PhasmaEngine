#include "Pipeline.h"

using namespace vm;

void Pipeline::destroy()
{
	if (pipeinfo.layout) {
		VulkanContext::get()->device.destroyPipelineLayout(pipeinfo.layout);
		pipeinfo.layout = nullptr;
	}

	if (compinfo.layout) {
		VulkanContext::get()->device.destroyPipelineLayout(compinfo.layout);
		pipeinfo.layout = nullptr;
	}

	if (pipeline) {
		VulkanContext::get()->device.destroyPipeline(pipeline);
		pipeline = nullptr;
	}
}