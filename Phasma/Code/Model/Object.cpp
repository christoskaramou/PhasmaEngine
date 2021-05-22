#include "PhasmaPch.h"
#include "Object.h"
#include "tinygltf/stb_image.h"
#include "../Renderer/Vulkan/Vulkan.h"

namespace pe
{
	Object::Object()
	{
		descriptorSet = make_ref(vk::DescriptorSet());
	}

	void Object::createVertexBuffer()
	{
		vertexBuffer.createBuffer(
				sizeof(float) * vertices.size(),
				vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// Staging buffer
		Buffer staging;
		staging.createBuffer(
				sizeof(float) * vertices.size(), vk::BufferUsageFlagBits::eTransferSrc,
				vk::MemoryPropertyFlagBits::eHostVisible
		);
		staging.map();
		staging.copyData(vertices.data());
		staging.flush();
		staging.unmap();

		vertexBuffer.copyBuffer(*staging.buffer, staging.size);
		staging.destroy();
	}

	void Object::createUniformBuffer(size_t size)
	{
		uniformBuffer
				.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniformBuffer.map();
		uniformBuffer.zero();
		uniformBuffer.flush();
		uniformBuffer.unmap();
	}

	void Object::loadTexture(const std::string& path)
	{
		// Texture Load
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		const vk::DeviceSize imageSize = texWidth * texHeight * 4;

		if (!pixels)
			throw std::runtime_error("No pixel data loaded");

		Buffer staging;
		staging.createBuffer(
				imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible
		);
		staging.map();
		staging.copyData(pixels);
		staging.flush();
		staging.unmap();

		stbi_image_free(pixels);

		texture.format = make_ref(vk::Format::eR8G8B8A8Unorm);
		texture.mipLevels = 1;
		texture.createImage(
				texWidth, texHeight, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		texture.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
		texture.copyBufferToImage(*staging.buffer);
		texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		texture.createImageView(vk::ImageAspectFlagBits::eColor);
		texture.createSampler();

		staging.destroy();
	}

	void Object::createDescriptorSet(const vk::DescriptorSetLayout& descriptorSetLayout)
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSet = make_ref(VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0));


		std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
		// MVP
		vk::DescriptorBufferInfo dbi;
		dbi.buffer = *uniformBuffer.buffer;
		dbi.offset = 0;
		dbi.range = uniformBuffer.size;

		textureWriteSets[0].dstSet = *descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		textureWriteSets[0].pBufferInfo = &dbi;

		// texture sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = *texture.sampler;
		dii.imageView = *texture.view;
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		textureWriteSets[1].dstSet = *descriptorSet;
		textureWriteSets[1].dstBinding = 1;
		textureWriteSets[1].dstArrayElement = 0;
		textureWriteSets[1].descriptorCount = 1;
		textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[1].pImageInfo = &dii;
		VulkanContext::Get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}

	void Object::destroy()
	{
		texture.destroy();
		vertexBuffer.destroy();
		indexBuffer.destroy();
		uniformBuffer.destroy();
		vertices.clear();
		vertices.shrink_to_fit();
	}
}
