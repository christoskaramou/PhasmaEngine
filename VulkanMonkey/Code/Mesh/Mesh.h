#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Vertex/Vertex.h"
#include "../Image/Image.h"
#include "../Material/Material.h"
#include "../Buffer/Buffer.h"
#include "../../include/tinygltf/stb_image.h"
#include <map>

namespace vm {

	enum TextureType
	{
		DiffuseMap,
		SpecularMap,
		AmbientMap,
		EmissiveMap,
		HeightMap,
		NormalsMap,
		ShininessMap,
		OpacityMap,
		DisplacementMap,
		LightMap,
		ReflectionMap,
		MetallicRoughness
	};
	struct VulkanContext;
	struct Mesh
	{
		VulkanContext* vulkan = &VulkanContext::get();

		static std::map<std::string, Image> uniqueTextures;
		static vk::DescriptorSetLayout descriptorSetLayout;
		vk::DescriptorSet descriptorSet;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		uint32_t vertexOffset = 0, indexOffset = 0;
		Buffer uniformBuffer;

		bool render = true, cull = false, hasAlphaMap = false, hasBones = false, hasPBR = false;
		Mesh* parent = nullptr;
		mat4 transform = mat4::identity();
		vec4 boundingSphere;

		Material material;
		PBRMaterial pbrMaterial;

		static vk::DescriptorSetLayout getDescriptorSetLayout();
		void createUniformBuffer();
		void calculateBoundingSphere();
		vec4 getBoundingSphere() const;
		void loadTexture(TextureType type, const std::string& folderPath, const std::string& texName);
		void destroy();
	};
}