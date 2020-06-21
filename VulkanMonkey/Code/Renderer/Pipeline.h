#pragma once
#include "../Core/Base.h"
#include "../Shader/Shader.h"
#include "../Renderer/RenderPass.h"

namespace vk
{
	class Pipeline;
	struct VertexInputBindingDescription;
	struct VertexInputAttributeDescription;
	struct PipelineColorBlendAttachmentState;
	enum class DynamicState;
	class DescriptorSetLayout;
	class PipelineLayout;
	struct GraphicsPipelineCreateInfo;
	struct ComputePipelineCreateInfo;
}

namespace vm
{
	enum class CullMode
	{
		None = 0,
		Front = 1,
		Back = 2
	};

	enum class PushConstantStage
	{
		Vertex = 1,
		Fragment = 16,
		Compute = 32
	};

	class PipelineCreateInfo
	{
	public:
		PipelineCreateInfo();
		~PipelineCreateInfo();
		Shader* pVertShader;
		Shader* pFragShader;
		Ref_t<std::vector<vk::VertexInputBindingDescription>> vertexInputBindingDescriptions;
		Ref_t<std::vector<vk::VertexInputAttributeDescription>> vertexInputAttributeDescriptions;
		float width;
		float height;
		CullMode cullMode;
		Ref_t<std::vector<vk::PipelineColorBlendAttachmentState>> colorBlendAttachments;
		Ref_t<std::vector<vk::DynamicState>> dynamicStates;
		PushConstantStage pushConstantStage;
		uint32_t pushConstantSize;
		Ref_t<std::vector<vk::DescriptorSetLayout>> descriptorSetLayouts;
		RenderPass renderPass;
	};

	class Pipeline
	{
	public:
		Pipeline();
		~Pipeline();

		PipelineCreateInfo info;
		Ref_t<vk::Pipeline> pipeline;
		Ref_t<vk::PipelineLayout> pipelineLayout;
		Ref_t<vk::ComputePipelineCreateInfo> compinfo;

		void createGraphicsPipeline();
		void destroy();

		static vk::DescriptorSetLayout& getDescriptorSetLayoutComposition();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutBrightFilter();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutGaussianBlurH();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutGaussianBlurV();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutCombine();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutDOF();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutFXAA();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutMotionBlur();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutSSAO();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutSSAOBlur();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutSSR();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutTAA();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutTAASharpen();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutShadows();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutMesh();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutPrimitive();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutModel();
		static vk::DescriptorSetLayout& getDescriptorSetLayoutSkybox();
	};
}