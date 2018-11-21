#include "../include/Mesh.h"
#include "../include/Errors.h"
#include "../include/Buffer.h"
#include <iostream>

using namespace vm;

vk::DescriptorSetLayout Mesh::descriptorSetLayout = nullptr;

vm::Mesh::Mesh(VulkanContext * vulkan) : vulkan(vulkan)
{ }


vk::DescriptorSetLayout Mesh::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for mesh texture
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(2) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(3) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		VkCheck(device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

void Mesh::loadTexture(TextureType type, const std::string path)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture: " << path << "\n";
		exit(-19);
	}

	Buffer staging = Buffer(vulkan);
	staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vulkan->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	Image* tex = nullptr;
	switch (type)
	{
	case Mesh::DiffuseMap:
		tex = &texture;
		break;
	case Mesh::NormalMap:
		tex = &normalsTexture;
		break;
	case Mesh::SpecularMap:
		tex = &specularTexture;
		break;
	case Mesh::AlphaMap:
		tex = &alphaTexture;
		hasAlpha = true;
		break;
	default:
		break;
	}

	tex->name = path;
	tex->format = vk::Format::eR8G8B8A8Unorm;
	tex->mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
	tex->createImage(texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	tex->transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	tex->copyBufferToImage(staging.buffer, 0, 0, texWidth, texHeight);
	tex->generateMipMaps(texWidth, texHeight);
	tex->createImageView(vk::ImageAspectFlagBits::eColor);
	tex->createSampler();

	staging.destroy();
}

void Mesh::calculateBoundingSphere()
{
	float maxX = 0, maxY = 0, maxZ = 0, minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
	for (auto& vertex : vertices) {
		if (vertex.x > maxX) maxX = vertex.x;
		if (vertex.y > maxY) maxY = vertex.y;
		if (vertex.z > maxZ) maxZ = vertex.z;
		if (vertex.x < minX) minX = vertex.x;
		if (vertex.y < minY) minY = vertex.y;
		if (vertex.z < minZ) minZ = vertex.z;
	}
	vm::vec3 center = (vm::vec3(maxX, maxY, maxZ) + vm::vec3(minX, minY, minZ)) * .5f;
	float sphereRadius = vm::length(vm::vec3(maxX, maxY, maxZ) - center);
	boundingSphere = vm::vec4(center, sphereRadius);
}

void Mesh::destroy()
{
	texture.destroy();
	normalsTexture.destroy();
	specularTexture.destroy();
	alphaTexture.destroy();
	vertices.clear();
	vertices.shrink_to_fit();
	indices.clear();
	indices.shrink_to_fit();
	if (descriptorSetLayout)
		vulkan->device.destroyDescriptorSetLayout(descriptorSetLayout);
}