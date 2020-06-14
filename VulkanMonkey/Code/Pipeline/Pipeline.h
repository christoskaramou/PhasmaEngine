#pragma once
#include "../Core/Base.h"

namespace vk
{
	class Pipeline;
	struct GraphicsPipelineCreateInfo;
	struct ComputePipelineCreateInfo;
}

namespace vm
{
	class Pipeline
	{
	public:
		Pipeline();
		Ref_t<vk::Pipeline> pipeline;
		Ref_t<vk::GraphicsPipelineCreateInfo> pipeinfo;
		Ref_t<vk::ComputePipelineCreateInfo> compinfo;

		void destroy();
	};
}