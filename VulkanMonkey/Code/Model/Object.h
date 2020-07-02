#pragma once
#include "../Core/Image.h"
#include "../Core/Buffer.h"
#include "../Core/Math.h"

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
}

namespace vm
{
	class Object
	{
	public:
		Object();
		virtual ~Object() = default;
		bool render = true, cull = false;
		Ref<vk::DescriptorSet> descriptorSet;
		Image texture;
		std::vector<float> vertices{};
		Buffer vertexBuffer;
		Buffer indexBuffer;
		Buffer uniformBuffer;

		virtual void createVertexBuffer();
		virtual void createUniformBuffer(size_t size);
		virtual void loadTexture(const std::string& path);
		virtual void createDescriptorSet(const vk::DescriptorSetLayout& descriptorSetLayout);
		virtual void destroy();
	};
}