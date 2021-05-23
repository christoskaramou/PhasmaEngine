#pragma once

#include "../Renderer/Image.h"
#include "../Renderer/Buffer.h"
#include "../Core/Math.h"

namespace vk
{
	class DescriptorSet;
	
	class DescriptorSetLayout;
}

namespace pe
{
	class Object
	{
	public:
		Object();
		
		virtual ~Object() = default;
		
		bool render = true, cull = false;
		Ref<vk::DescriptorSet> descriptorSet;
		Image texture;
		std::vector<float> vertices {};
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