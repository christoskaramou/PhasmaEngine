#include "Mesh.h"
#include "../Buffer/Buffer.h"

using namespace vm;

vk::DescriptorSetLayout Mesh::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout Primitive::descriptorSetLayout = nullptr;
std::map<std::string, Image> Mesh::uniqueTextures{};

vk::DescriptorSetLayout* Primitive::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlag) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlag, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment),
			layoutBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment),
			layoutBinding(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment),
			layoutBinding(4, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment),
			layoutBinding(5, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(descriptorLayout);
	}
	return &descriptorSetLayout;
}

vk::DescriptorSetLayout* Mesh::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, vk::ShaderStageFlagBits::eVertex, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eUniformBuffer),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(descriptorLayout);
	}
	return &descriptorSetLayout;
}

void Mesh::createUniformBuffers()
{
	uniformBuffer.createBuffer(sizeof(ubo), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);
	memset(uniformBuffer.data, 0, uniformBuffer.size);
	size_t size = sizeof(mat4);
	for (auto& primitive : primitives) {
		primitive.uniformBuffer.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		primitive.uniformBuffer.data = vulkan->device.mapMemory(primitive.uniformBuffer.memory, 0, primitive.uniformBuffer.size);

		mat4 factors;
		factors[0] = primitive.pbrMaterial.baseColorFactor != vec4(0.f) ? primitive.pbrMaterial.baseColorFactor : vec4(1.f);
		factors[1] = vec4(primitive.pbrMaterial.emissiveFactor, 1.f);
		factors[2] = vec4(primitive.pbrMaterial.metallicFactor, primitive.pbrMaterial.roughnessFactor, primitive.pbrMaterial.alphaCutoff, 0.f);
		factors[3][0] = static_cast<float>(primitive.hasBones);
		memcpy(primitive.uniformBuffer.data, &factors, sizeof(factors));
	}
}

void Primitive::loadTexture(
	TextureType type,
	const std::string& folderPath,
	const Microsoft::glTF::Image* image,
	const Microsoft::glTF::Document* document,
	const Microsoft::glTF::GLTFResourceReader* resourceReader)
{
	std::string path = folderPath;
	if (image)
		path = folderPath + image->uri;

	// get the right texture
	Image* tex = nullptr;
	switch (type)
	{
	case TextureType::BaseColor:
		tex = &pbrMaterial.baseColorTexture;
		if (!image || image->uri.empty())
			path = "objects/white.png";
		break;
	case TextureType::MetallicRoughness:
		tex = &pbrMaterial.metallicRoughnessTexture;
		if (!image || image->uri.empty())
			path = "objects/black.png";
		break;
	case TextureType::Normal:
		tex = &pbrMaterial.normalTexture;
		if (!image || image->uri.empty())
			path = "objects/normal.png";
		break;
	case TextureType::Occlusion:
		tex = &pbrMaterial.occlusionTexture;
		if (!image || image->uri.empty())
			path = "objects/white.png";
		break;
	case TextureType::Emissive:
		tex = &pbrMaterial.emissiveTexture;
		if (!image || image->uri.empty())
			path = "objects/black.png";
		break;
	default:
		throw std::runtime_error("Load texture invalid type");
	}

	// Check if it is already loaded
	if (Mesh::uniqueTextures.find(path) != Mesh::uniqueTextures.end()) {
		*tex = Mesh::uniqueTextures[path];
	}
	else {
		int texWidth, texHeight, texChannels;
		unsigned char* pixels = nullptr;
		//stbi_set_flip_vertically_on_load(true);
		if (!image || image->bufferViewId.empty())
			pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		else
		{
			const auto data = resourceReader->ReadBinaryData(*document, *image);
			pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		}

		if (!pixels)
			throw std::runtime_error("No pixel data loaded");

		vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;

		vulkan->waitAndLockSubmits();

		Buffer staging;
		staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		auto* vulkan = &VulkanContext::get();
		vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &staging.data);
		memcpy(staging.data, pixels, static_cast<size_t>(imageSize));
		vulkan->device.unmapMemory(staging.memory);

		stbi_image_free(pixels);

		tex->name = path;
		tex->format = vk::Format::eR8G8B8A8Unorm;
		tex->mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
		tex->createImage(texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
		tex->transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
		tex->copyBufferToImage(staging.buffer);
		tex->generateMipMaps();
		tex->createImageView(vk::ImageAspectFlagBits::eColor);
		tex->maxLod = static_cast<float>(tex->mipLevels);
		tex->createSampler();

		staging.destroy();

		vulkan->unlockSubmits();

		Mesh::uniqueTextures[path] = *tex;
	}
}

//void Mesh::calculateBoundingSphere()
//{
//	vec3 _max(-FLT_MAX);
//	vec3 _min(FLT_MAX);
//	for (auto& vertex : vertices) {
//		_max = maximum(_max, vertex.position);
//		_min = minimum(_min, vertex.position);
//	}
//	vec3 center = (_max + _min) * .5f;
//	float sphereRadius = length(_max - center);
//	boundingSphere = vec4(center, sphereRadius);
//}

void Mesh::destroy()
{
	uniformBuffer.destroy();
	if (descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(descriptorSetLayout);
		descriptorSetLayout = nullptr;
	}

	for (auto& primitive : primitives) {
		primitive.uniformBuffer.destroy();
	}
	vertices.clear();
	vertices.shrink_to_fit();
	indices.clear();
	indices.shrink_to_fit();
	if (Primitive::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Primitive::descriptorSetLayout);
		Primitive::descriptorSetLayout = nullptr;
	}
}