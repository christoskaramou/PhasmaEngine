#include "Pipeline.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	Pipeline::Pipeline()
	{
		pipeline.SetRef(CreateRef<vk::Pipeline>());
		pipeinfo.SetRef(CreateRef<vk::GraphicsPipelineCreateInfo>());
		compinfo.SetRef(CreateRef<vk::ComputePipelineCreateInfo>());
	}

	void Pipeline::destroy()
	{
		if (pipeinfo->layout) {
			VulkanContext::get()->device.destroyPipelineLayout(pipeinfo->layout);
			pipeinfo->layout = nullptr;
		}

		if (compinfo->layout) {
			VulkanContext::get()->device.destroyPipelineLayout(compinfo->layout);
			compinfo->layout = nullptr;
		}

		if (*pipeline) {
			VulkanContext::get()->device.destroyPipeline(*pipeline);
			*pipeline = nullptr;
		}
	}
}