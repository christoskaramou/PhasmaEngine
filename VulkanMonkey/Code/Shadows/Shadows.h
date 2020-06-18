#pragma once
#include "../Core/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Core/Math.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>

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
