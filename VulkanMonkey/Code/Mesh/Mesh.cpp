#include "Mesh.h"
#include "../Buffer/Buffer.h"

using namespace vm;

vk::DescriptorSetLayout Mesh::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout Primitive::descriptorSetLayout = nullptr;
std::map<std::string, Image> Mesh::uniqueTextures{};

vk::DescriptorSetLayout Primitive::getDescriptorSetLayout()
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

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(4) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		// binding for mesh factors and values
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(5) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

vk::DescriptorSetLayout vm::Mesh::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

void Mesh::createUniformBuffers()
{
	uniformBuffer.createBuffer(sizeof(ubo), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);
	size_t size = sizeof(mat4);
	for (auto& primitive : primitives) {
		primitive.uniformBuffer.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		primitive.uniformBuffer.data = vulkan->device.mapMemory(primitive.uniformBuffer.memory, 0, primitive.uniformBuffer.size);

		mat4 factors;
		factors[0] = primitive.pbrMaterial.baseColorFactor != vec4(0.f) ? primitive.pbrMaterial.baseColorFactor : vec4(1.f);
		factors[1] = vec4(primitive.pbrMaterial.emissiveFactor, 1.f);
		factors[2] = vec4(primitive.pbrMaterial.metallicFactor, primitive.pbrMaterial.roughnessFactor, primitive.pbrMaterial.alphaCutoff, 0.f);
		factors[3][0] = (float)primitive.hasBones;
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
		exit(-19);
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
			pixels = stbi_load_from_memory(data.data(), (int)data.size(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		}

		if (!pixels) {
			exit(-19);
		}

		vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;

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
		tex->copyBufferToImage(staging.buffer, 0, 0, texWidth, texHeight);
		tex->generateMipMaps(texWidth, texHeight);
		tex->createImageView(vk::ImageAspectFlagBits::eColor);
		tex->maxLod = (float)tex->mipLevels;
		tex->createSampler();

		staging.destroy();

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