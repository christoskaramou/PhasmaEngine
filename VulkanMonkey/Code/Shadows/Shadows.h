#pragma once
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"

namespace vm {

	struct ShadowsUBO
	{
		mat4 projection, view;
		float castShadows;
		float maxCascadeDist0;
		float maxCascadeDist1;
		float maxCascadeDist2;
	};

	struct Shadows
	{
		ShadowsUBO shadows_UBO[3]{};
		static uint32_t imageSize;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();
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
