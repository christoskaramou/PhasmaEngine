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
#include "Renderer/RenderApi.h"

namespace pe
{
	Object::Object()
	{
		descriptorSet = make_sptr(vk::DescriptorSet());
	}
	
	void Object::createVertexBuffer()
	{
		vertexBuffer = Buffer::Create(
			sizeof(float) * vertices.size(),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eTransferDst | (BufferUsageFlags)vk::BufferUsageFlagBits::eVertexBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eDeviceLocal);
		vertexBuffer->SetDebugName("Object_Vertex");

		// Staging buffer
		SPtr<Buffer> staging = Buffer::Create(
				sizeof(float) * vertices.size(),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		staging->Map();
		staging->CopyData(vertices.data());
		staging->Flush();
		staging->Unmap();
		staging->SetDebugName("Staging");
		
		vertexBuffer->CopyBuffer(staging.get(), staging->Size());
		staging->Destroy();
	}
	
	void Object::createUniformBuffer(size_t size)
	{
		uniformBuffer = Buffer::Create(
			size,
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniformBuffer->Map();
		uniformBuffer->Zero();
		uniformBuffer->Flush();
		uniformBuffer->Unmap();
		uniformBuffer->SetDebugName("Object_UB");
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
		
		SPtr<Buffer> staging = Buffer::Create(
			imageSize,
			(BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		staging->Map();
		staging->CopyData(pixels);
		staging->Flush();
		staging->Unmap();
		staging->SetDebugName("Staging");
		
		stbi_image_free(pixels);
		
		texture.format = VK_FORMAT_R8G8B8A8_UNORM;
		texture.mipLevels = 1;
		texture.CreateImage(
			texWidth, texHeight, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		texture.TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		texture.CopyBufferToImage(staging.get());
		texture.TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		texture.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
		texture.CreateSampler();
		texture.SetDebugName(path.c_str());
		
		staging->Destroy();
	}
	
	void Object::createDescriptorSet(const vk::DescriptorSetLayout& descriptorSetLayout)
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo).at(0));
		VULKAN.SetDebugObjectName(*descriptorSet, "Object");
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
		// MVP
		vk::DescriptorBufferInfo dbi;
		dbi.buffer = uniformBuffer->Handle<vk::Buffer>();
		dbi.offset = 0;
		dbi.range = uniformBuffer->Size();
		
		textureWriteSets[0].dstSet = *descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		textureWriteSets[0].pBufferInfo = &dbi;
		
		// texture sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = texture.sampler;
		dii.imageView = texture.view;
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		
		textureWriteSets[1].dstSet = *descriptorSet;
		textureWriteSets[1].dstBinding = 1;
		textureWriteSets[1].dstArrayElement = 0;
		textureWriteSets[1].descriptorCount = 1;
		textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[1].pImageInfo = &dii;
		VULKAN.device->updateDescriptorSets(textureWriteSets, nullptr);
	}
	
	void Object::destroy()
	{
		texture.Destroy();
		vertexBuffer->Destroy();
		indexBuffer->Destroy();
		uniformBuffer->Destroy();
		vertices.clear();
		vertices.shrink_to_fit();
	}
}
