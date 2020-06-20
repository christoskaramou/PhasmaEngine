#pragma once
#include "../Core/Buffer.h"
#include "../Core/Image.h"
#include "../Renderer/Pipeline.h"
#include "../Core/Math.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"

namespace vk
{
	class DescriptorSetLayout;
	class DescriptorSet;
}

namespace vm
{
	struct ShadowsUBO
	{
		mat4 projection, view;
		float castShadows;
		float maxCascadeDist0;
		float maxCascadeDist1;
		float maxCascadeDist2;
	};

	class Shadows
	{
	public:
		Shadows();
		~Shadows();
		ShadowsUBO shadows_UBO[3]{};
		static uint32_t imageSize;
		static Ref_t<vk::DescriptorSetLayout> descriptorSetLayout;
		static const vk::DescriptorSetLayout& getDescriptorSetLayout();
		RenderPass renderPass;
		std::vector<Image> textures{};
		std::vector<vk::DescriptorSet> descriptorSets{};
		std::vector<Framebuffer> framebuffers{};
		std::vector<Buffer> uniformBuffers{};
		Pipeline pipeline;

		void update(Camera& camera);
		void createUniformBuffers();
		void createDescriptorSets();
		void createRenderPass();
		void createFrameBuffers();
		void createPipeline(vk::DescriptorSetLayout mesh, vk::DescriptorSetLayout model);
		void destroy();
	};
}
