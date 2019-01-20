#include "Mesh.h"
#include "../Buffer/Buffer.h"
#include <iostream>

using namespace vm;

vk::DescriptorSetLayout Mesh::descriptorSetLayout = nullptr;
std::map<std::string, Image> Mesh::uniqueTextures{};

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

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(4) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(5) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

void Mesh::loadTexture(TextureType type, const std::string& folderPath, const std::string& texName)
{
	std::string path = folderPath + texName;

	// get the right texture
	Image* tex = nullptr;
	switch (type)
	{
	case Mesh::DiffuseMap:
		tex = &material.textureDiffuse;
		if (texName == "")
			path = "objects/default.png";
		break;
	case Mesh::SpecularMap:
		tex = &material.textureSpecular;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::AmbientMap:
		tex = &material.textureAmbient;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::EmissiveMap:
		tex = &material.textureEmissive;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::HeightMap:
		tex = &material.textureHeight;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::NormalsMap:
		tex = &material.textureNormals;
		if (texName == "")
			path = "objects/defaultNormalMap.png";
		break;
	case Mesh::ShininessMap:
		tex = &material.textureShininess;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::OpacityMap:
		tex = &material.textureOpacity;
		if (texName == "")
			path = "objects/default.png";
		else
			hasAlphaMap = true;
		break;
	case Mesh::DisplacementMap:
		tex = &material.textureDisplacement;
		if (texName == "")
			path = "objects/defaultSpecularMap.png";
		break;
	case Mesh::LightMap: //Ambient Occlusion
		tex = &material.textureLight;
		if (texName == "")
			path = "objects/default.png";
		break;
	case Mesh::ReflectionMap:
		tex = &material.textureReflection;
		if (texName == "")
			path = "objects/default.png";
		break;
	case Mesh::MetallicRoughness:
		tex = &gltfMaterial.metallicRoughnessTexture;
		if (texName == "")
			path = "objects/default.png";
		break;
	default:
		exit(-19);
	}

	// Check if it is already loaded
	if (uniqueTextures.find(path) != uniqueTextures.end()) {
		*tex = uniqueTextures[path];
	}
	else {
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;

		if (!pixels) {
			exit(-19);
		}

		Buffer staging;
		staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data;
		vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
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

		uniqueTextures[path] = *tex;
	}
}

void Mesh::calculateBoundingSphere()
{
	vec3 _max(-FLT_MAX);
	vec3 _min(FLT_MAX);
	for (auto& vertex : vertices) {
		_max = maximum(_max, vertex.position);
		_min = minimum(_min, vertex.position);
	}
	vec3 center = (_max + _min) * .5f;
	float sphereRadius = length(_max - center);
	boundingSphere = vec4(center, sphereRadius);
}

vec4 vm::Mesh::getBoundingSphere() const
{
	return boundingSphere;
}

void Mesh::destroy()
{
	material.textureDiffuse.destroy();
	material.textureNormals.destroy();
	material.textureSpecular.destroy();
	material.textureOpacity.destroy();
	vertices.clear();
	vertices.shrink_to_fit();
	indices.clear();
	indices.shrink_to_fit();
	if (descriptorSetLayout)
		vulkan->device.destroyDescriptorSetLayout(descriptorSetLayout);
}