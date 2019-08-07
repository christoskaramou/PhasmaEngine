#pragma once
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"

namespace vm {
	struct Shadows
	{
		static uint32_t imageSize;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();
		vk::RenderPass renderPass;
		std::vector<Image> textures{};
		std::vector<vk::DescriptorSet> descriptorSets{};
		std::vector<vk::Framebuffer> frameBuffers{};
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

	struct ShadowsUBO
	{
		mat4 projection, view;
		float castShadows;
		float maxCascadeDist0;
		float maxCascadeDist1;
		float maxCascadeDist2;
	};
}
