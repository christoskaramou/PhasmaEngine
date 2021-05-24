/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PhasmaPch.h"
#include "Object.h"
#include "tinygltf/stb_image.h"
#include "../Renderer/RenderApi.h"

namespace pe
{
	Object::Object()
	{
		descriptorSet = make_ref(vk::DescriptorSet());
	}
	
	void Object::createVertexBuffer()
	{
		vertexBuffer.CreateBuffer(
				sizeof(float) * vertices.size(),
				BufferUsage::TransferDst | BufferUsage::VertexBuffer,
				MemoryProperty::DeviceLocal
		);
		
		// Staging buffer
		Buffer staging;
		staging.CreateBuffer(
				sizeof(float) * vertices.size(), BufferUsage::TransferSrc,
				MemoryProperty::HostVisible
		);
		staging.Map();
		staging.CopyData(vertices.data());
		staging.Flush();
		staging.Unmap();
		
		vertexBuffer.CopyBuffer(&staging, staging.Size());
		staging.Destroy();
	}
	
	void Object::createUniformBuffer(size_t size)
	{
		uniformBuffer.CreateBuffer(size, BufferUsage::UniformBuffer, MemoryProperty::HostVisible);
		uniformBuffer.Map();
		uniformBuffer.Zero();
		uniformBuffer.Flush();
		uniformBuffer.Unmap();
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
		staging.CreateBuffer(
				imageSize, BufferUsage::TransferSrc, MemoryProperty::HostVisible
		);
		staging.Map();
		staging.CopyData(pixels);
		staging.Flush();
		staging.Unmap();
		
		stbi_image_free(pixels);
		
		texture.format = make_ref(vk::Format::eR8G8B8A8Unorm);
		texture.mipLevels = 1;
		texture.createImage(
				texWidth, texHeight, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		texture.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
		texture.copyBufferToImage(*staging.GetBufferVK());
		texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		texture.createImageView(vk::ImageAspectFlagBits::eColor);
		texture.createSampler();
		
		staging.Destroy();
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
		dbi.buffer = *uniformBuffer.GetBufferVK();
		dbi.offset = 0;
		dbi.range = uniformBuffer.Size();
		
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
		vertexBuffer.Destroy();
		indexBuffer.Destroy();
		uniformBuffer.Destroy();
		vertices.clear();
		vertices.shrink_to_fit();
	}
}
