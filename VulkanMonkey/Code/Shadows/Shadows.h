#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Image/Image.h"
#include "../Buffer/Buffer.h"
#include "../Image/Image.h"
#include "../Pipeline/Pipeline.h"
#include "../Math/Math.h"

namespace vm {
	struct Shadows
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		static bool shadowCast;
		static uint32_t imageSize;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::RenderPass renderPass;
		Image texture;
		vk::DescriptorSet descriptorSet;
		std::vector<vk::Framebuffer> frameBuffers;
		Buffer uniformBuffer;
		Pipeline pipeline;

		void createDynamicUniformBuffer(size_t num_of_objects);
		void createDescriptorSet();
		void destroy();
	};


	struct ShadowsUBO
	{
		mat4 projection, view, model;
		float castShadows, dummy[15]; // for 256 bytes align
	};
}
