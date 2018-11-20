#pragma once
#include "Vulkan.h"
#include "Image.h"
#include "Buffer.h"
#include "Image.h"
#include "Pipeline.h"
#include "Math.h"

namespace vm {
	struct Shadows
	{
		static bool shadowCast;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		static vk::RenderPass renderPass;
		static vk::RenderPass getRenderPass(vk::Device device, Image& depth);
		static uint32_t imageSize;
		Image texture;
		vk::DescriptorSet descriptorSet;
		std::vector<vk::Framebuffer> frameBuffer;
		Buffer uniformBuffer;
		Pipeline pipeline;

		void createFrameBuffers(vk::Device device, vk::PhysicalDevice gpu, Image& depth, uint32_t bufferCount);
		void createDynamicUniformBuffer(vk::Device device, vk::PhysicalDevice gpu, size_t num_of_objects);
		void createDescriptorSet(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout & descriptorSetLayout);
		void destroy(vk::Device device);
	};


	struct ShadowsUBO
	{
		vm::mat4 projection, view, model;
		float castShadows, dummy[15]; // for 256 bytes align
	};
}
