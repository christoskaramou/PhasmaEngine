#pragma once
#include "Vulkan.h"

struct Pipeline
{
	vk::Pipeline pipeline;
	vk::GraphicsPipelineCreateInfo pipeinfo;
	vk::ComputePipelineCreateInfo compinfo;

	void destroy(vk::Device device);
};

struct PipelineInfo
{
	std::vector<std::string> shaders{};
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
	std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptions{};
	std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions{};
	vk::CullModeFlagBits cull = vk::CullModeFlagBits::eBack;
	vk::FrontFace face = vk::FrontFace::eClockwise;
	vk::PushConstantRange pushConstantRange;
	bool useBlendState = true;
	std::vector<vk::PipelineColorBlendAttachmentState> blendAttachmentStates{
		vk::PipelineColorBlendAttachmentState{					// const PipelineColorBlendAttachmentState* pAttachments;
			VK_TRUE,												// Bool32 blendEnable;
			vk::BlendFactor::eSrcAlpha,								// BlendFactor srcColorBlendFactor;
			vk::BlendFactor::eOneMinusSrcAlpha,						// BlendFactor dstColorBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp colorBlendOp;
			vk::BlendFactor::eOne,									// BlendFactor srcAlphaBlendFactor;
			vk::BlendFactor::eZero,									// BlendFactor dstAlphaBlendFactor;
			vk::BlendOp::eAdd,										// BlendOp alphaBlendOp;
			vk::ColorComponentFlagBits::eR |						// ColorComponentFlags colorWriteMask;
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
		}
	};
	vk::SpecializationInfo specializationInfo = vk::SpecializationInfo();
	vk::RenderPass renderPass;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
	vk::Extent2D viewportSize;
	std::vector<vk::DynamicState> dynamicStates{};
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo;
	vk::CompareOp compareOp = vk::CompareOp::eLessOrEqual;
	uint32_t subpassTarget = 0;
};