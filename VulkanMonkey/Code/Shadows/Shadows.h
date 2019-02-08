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
		Image texture;
		vk::DescriptorSet descriptorSet;
		std::vector<vk::Framebuffer> frameBuffers;
		Buffer uniformBuffer;
		Pipeline pipeline;

		void update(Camera& camera);
		void createUniformBuffer();
		void createDescriptorSet();
		void destroy();
	};

	struct ShadowsUBO
	{
		mat4 projection, view;
		float castShadows, dummy[15];
	};
}
