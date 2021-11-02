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

#include "Object.h"
#include "tinygltf/stb_image.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"

namespace pe
{
	Object::Object()
	{
		descriptorSet = {};
	}
	
	void Object::createVertexBuffer()
	{
		vertexBuffer = Buffer::Create(
			sizeof(float) * vertices.size(),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// Staging buffer
		Buffer* staging = Buffer::Create(
				sizeof(float) * vertices.size(),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		staging->Map();
		staging->CopyData(vertices.data());
		staging->Flush();
		staging->Unmap();
		
		vertexBuffer->CopyBuffer(staging, staging->Size());
		staging->Destroy();
	}
	
	void Object::createUniformBuffer(size_t size)
	{
		uniformBuffer = Buffer::Create(
			size,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uniformBuffer->Map();
		uniformBuffer->Zero();
		uniformBuffer->Flush();
		uniformBuffer->Unmap();
	}
	
	void Object::loadTexture(const std::string& path)
	{
		// Texture Load
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4;
		
		if (!pixels)
			throw std::runtime_error("No pixel data loaded");
		
		Buffer* staging = Buffer::Create(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		staging->Map();
		staging->CopyData(pixels);
		staging->Flush();
		staging->Unmap();
		
		stbi_image_free(pixels);
		
		ImageCreateInfo info{};
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.width = texWidth;
		info.height = texHeight;
		info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		texture = Image::Create(info);

		ImageViewCreateInfo viewInfo{};
		viewInfo.image = texture;
		texture->CreateImageView(viewInfo);

		SamplerCreateInfo samplerInfo{};
		texture->CreateSampler(samplerInfo);

		texture->TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		texture->CopyBufferToImage(staging);
		texture->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		staging->Destroy();
	}
	
	void Object::createDescriptorSet(DescriptorSetLayoutHandle descriptorSetLayout)
	{
		VkDescriptorSetLayout layout = descriptorSetLayout;
		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = RHII.descriptorPool->Handle();
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &layout;

		VkDescriptorSet dset;
		vkAllocateDescriptorSets(RHII.device, &allocateInfo, &dset);
		descriptorSet = dset;
		
		std::vector<VkWriteDescriptorSet> textureWriteSets(2);
		// MVP
		VkDescriptorBufferInfo dbi{};
		dbi.buffer = uniformBuffer->Handle();
		dbi.offset = 0;
		dbi.range = uniformBuffer->Size();
		
		textureWriteSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		textureWriteSets[0].dstSet = descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		textureWriteSets[0].pBufferInfo = &dbi;
		
		// texture sampler
		VkDescriptorImageInfo dii;
		dii.sampler = texture->sampler;
		dii.imageView = texture->view;
		dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		textureWriteSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		textureWriteSets[1].dstSet = descriptorSet;
		textureWriteSets[1].dstBinding = 1;
		textureWriteSets[1].dstArrayElement = 0;
		textureWriteSets[1].descriptorCount = 1;
		textureWriteSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureWriteSets[1].pImageInfo = &dii;

		vkUpdateDescriptorSets(RHII.device, static_cast<uint32_t>(textureWriteSets.size()), textureWriteSets.data(), 0, nullptr);
	}
	
	void Object::destroy()
	{
		texture->Destroy();
		vertexBuffer->Destroy();
		indexBuffer->Destroy();
		uniformBuffer->Destroy();
		vertices.clear();
		vertices.shrink_to_fit();
	}
}
