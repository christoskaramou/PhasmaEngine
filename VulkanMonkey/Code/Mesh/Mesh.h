#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Vertex/Vertex.h"
#include "../Image/Image.h"
#include "../Material/Material.h"
#include "../../include/Stbi.h"
#include <map>

namespace vm {
	struct Mesh
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		bool render = true, cull = false, hasAlphaMap = false;
		Mesh* parent = nullptr;
		mat4 transform = mat4(1.0f);
		vec4 boundingSphere;
		void calculateBoundingSphere();
		vec4 getBoundingSphere() const;
		uint32_t vertexOffset = 0, indexOffset = 0;
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
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::DescriptorSet descriptorSet;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};

		Material material;
		GltfMaterial gltfMaterial;

		void loadTexture(TextureType type, const std::string& folderPath, const std::string& texName);
		void destroy();

		static std::map<std::string, Image> uniqueTextures;
	};
}