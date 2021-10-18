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
#include "Skybox.h"
#include "Renderer/Pipeline.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	SkyBox::SkyBox()
	{
		descriptorSet = make_sptr(vk::DescriptorSet());
	}
	
	SkyBox::~SkyBox()
	{
	}
	
	void SkyBox::createDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VULKAN.descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &(vk::DescriptorSetLayout)Pipeline::getDescriptorSetLayoutSkybox();
		descriptorSet = make_sptr(VULKAN.device->allocateDescriptorSets(allocateInfo).at(0));
		VULKAN.SetDebugObjectName(*descriptorSet, "Skybox");
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
		// texture sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = texture.sampler;
		dii.imageView = texture.view;
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		
		textureWriteSets[0].dstSet = *descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[0].pImageInfo = &dii;
		VULKAN.device->updateDescriptorSets(textureWriteSets, nullptr);
	}
	
	void SkyBox::loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show)
	{
		loadTextures(textureNames, imageSideSize);
	}
	
	// images must be squared and the image size must be the real else the assertion will fail
	void SkyBox::loadTextures(const std::array<std::string, 6>& paths, int imageSideSize)
	{
		assert(paths.size() == 6);
		
		texture.arrayLayers = 6;
		texture.format = VK_FORMAT_R8G8B8A8_UNORM;
		texture.imageCreateFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		texture.CreateImage(
			imageSideSize, imageSideSize, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		
		texture.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		for (uint32_t i = 0; i < texture.arrayLayers; ++i)
		{
			// Texture Load
			int texWidth, texHeight, texChannels;
			//stbi_set_flip_vertically_on_load(true);
			stbi_uc* pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			assert(imageSideSize == texWidth && imageSideSize == texHeight);
			
			const vk::DeviceSize imageSize = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4;
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
			
			texture.CopyBufferToImage(staging.get(), i);
			staging->Destroy();
		}
		texture.TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		texture.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		texture.CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
		
		texture.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		texture.CreateSampler();
		static int skyboxIdx = 0;
		texture.SetDebugName("Skybox_TextureArray" + std::to_string(skyboxIdx++));
	}
	
	void SkyBox::destroy()
	{
		texture.Destroy();
		if (Pipeline::getDescriptorSetLayoutSkybox())
		{
			vkDestroyDescriptorSetLayout(*VULKAN.device, Pipeline::getDescriptorSetLayoutSkybox(), nullptr);
			Pipeline::getDescriptorSetLayoutSkybox() = {};
		}
	}
}
