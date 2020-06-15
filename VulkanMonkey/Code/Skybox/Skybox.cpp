#include "Skybox.h"
#include "../GUI/GUI.h"
#include "tinygltf/stb_image.h"

namespace vm
{
	vk::DescriptorSetLayout SkyBox::descriptorSetLayout = nullptr;

	vk::DescriptorSetLayout SkyBox::getDescriptorSetLayout()
	{
		if (!descriptorSetLayout) {
			const auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, const vk::ShaderStageFlags& stageFlags) {
				return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlags, nullptr };
			};
			std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
				layoutBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment)
			};
			vk::DescriptorSetLayoutCreateInfo descriptorLayout;
			descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayout.pBindings = setLayoutBindings.data();
			descriptorSetLayout = VulkanContext::get()->device.createDescriptorSetLayout(descriptorLayout);
		}
		return descriptorSetLayout;
	}

	void SkyBox::createDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &getDescriptorSetLayout();
		descriptorSet = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo).at(0);

		std::vector<vk::WriteDescriptorSet> textureWriteSets(1);
		// texture sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = texture.sampler.Value();
		dii.imageView = texture.view.Value();
		dii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		textureWriteSets[0].dstSet = descriptorSet;
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[0].pImageInfo = &dii;
		VulkanContext::get()->device.updateDescriptorSets(textureWriteSets, nullptr);
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
		texture.format = vk::Format::eR8G8B8A8Unorm;
		texture.imageCreateFlags = vk::ImageCreateFlagBits::eCubeCompatible;
		texture.createImage(imageSideSize, imageSideSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
		for (uint32_t i = 0; i < texture.arrayLayers.Value(); ++i) {
			// Texture Load
			int texWidth, texHeight, texChannels;
			//stbi_set_flip_vertically_on_load(true);
			stbi_uc* pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			assert(imageSideSize == texWidth && imageSideSize == texHeight);

			const vk::DeviceSize imageSize = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4;
			if (!pixels)
				throw std::runtime_error("No pixel data loaded");

			Buffer staging;
			staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
			staging.map();
			staging.copyData(pixels);
			staging.flush();
			staging.unmap();

			stbi_image_free(pixels);

			texture.copyBufferToImage(staging.buffer.Value(), i);
			staging.destroy();
		}
		texture.transitionImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

		texture.viewType = vk::ImageViewType::eCube;
		texture.createImageView(vk::ImageAspectFlagBits::eColor);

		texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
		texture.createSampler();
	}

	void SkyBox::destroy()
	{
		texture.destroy();
		if (SkyBox::descriptorSetLayout) {
			VulkanContext::get()->device.destroyDescriptorSetLayout(SkyBox::descriptorSetLayout);
			SkyBox::descriptorSetLayout = nullptr;
		}
	}
}
