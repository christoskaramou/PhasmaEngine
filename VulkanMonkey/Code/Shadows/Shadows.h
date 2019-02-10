#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Image/Image.h"
#include "../Buffer/Buffer.h"
#include "../Image/Image.h"
#include "../Pipeline/Pipeline.h"
#include "../Math/Math.h"
#include "../Camera/Camera.h"

namespace vm {
	struct Shadows
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		static uint32_t imageSize;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::RenderPass renderPass;
		std::vector <Image> textures{};
		std::vector <vk::DescriptorSet> descriptorSets{};
		std::vector <vk::Framebuffer> frameBuffers{};
		std::vector <Buffer> uniformBuffers{};
		Pipeline pipeline;

		void update(Camera& camera);
		void createUniformBuffers();
		void createDescriptorSets();
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
