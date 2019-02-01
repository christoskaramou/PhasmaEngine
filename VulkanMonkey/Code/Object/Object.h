#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Image/Image.h"
#include "../Buffer/Buffer.h"
#include "../Math/Math.h"
#include "../../include/Stbi.h"

namespace vm {
	struct Object
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		virtual ~Object() = default;
		bool render = true, cull = false;
		vk::DescriptorSet descriptorSet;
		Image texture;
		std::vector<float> vertices{};
		Buffer vertexBuffer;
		Buffer indexBuffer;
		Buffer uniformBuffer;

		virtual void createVertexBuffer();
		virtual void createUniformBuffer(size_t size);
		virtual void loadTexture(const std::string& path);
		virtual void createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout);
		virtual void destroy();
	};
}