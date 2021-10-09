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
#include "Renderer/RenderApi.h"

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
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutSkybox();
		descriptorSet = make_sptr(VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0));
		VulkanContext::Get()->SetDebugObjectName(*descriptorSet, "Skybox");
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
		// texture sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = *texture.sampler;
		dii.imageView = *texture.view;
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		
		textureWriteSets[0].dstSet = *descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[0].pImageInfo = &dii;
		VulkanContext::Get()->device->updateDescriptorSets(textureWriteSets, nullptr);
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
		texture.format = make_sptr(vk::Format::eR8G8B8A8Unorm);
		texture.imageCreateFlags = make_sptr<vk::ImageCreateFlags>(vk::ImageCreateFlagBits::eCubeCompatible);
		texture.createImage(
				imageSideSize, imageSideSize, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		
		texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
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
			
			texture.copyBufferToImage(staging->Handle<vk::Buffer>(), i);
			staging->Destroy();
		}
		texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		
		texture.viewType = make_sptr(vk::ImageViewType::eCube);
		texture.createImageView(vk::ImageAspectFlagBits::eColor);
		
		texture.addressMode = make_sptr(vk::SamplerAddressMode::eClampToEdge);
		texture.createSampler();
		static int skyboxIdx = 0;
		texture.SetDebugName("Skybox_TextureArray" + std::to_string(skyboxIdx++));
	}
	
	void SkyBox::destroy()
	{
		texture.destroy();
		if (Pipeline::getDescriptorSetLayoutSkybox())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutSkybox());
			Pipeline::getDescriptorSetLayoutSkybox() = nullptr;
		}
	}
}
