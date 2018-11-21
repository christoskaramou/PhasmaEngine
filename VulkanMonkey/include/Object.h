#pragma once
#include "VulkanContext.h"
#include "Image.h"
#include "Buffer.h"
#include "Math.h"
#include "Stbi.h"

namespace vm {
	struct Object
	{
		VulkanContext* vulkan;
		Object(VulkanContext* vulkan);

		virtual ~Object() = default;
		bool render = true, cull = false;
		vk::DescriptorSet descriptorSet;
		Image texture = Image(vulkan);
		std::vector<float> vertices{};
		Buffer vertexBuffer = Buffer(vulkan);
		Buffer indexBuffer = Buffer(vulkan);
		Buffer uniformBuffer = Buffer(vulkan);

		virtual void createVertexBuffer();
		virtual void createUniformBuffer(size_t size);
		virtual void loadTexture(const std::string path);
		virtual void createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout);
		virtual void destroy();
	};
}