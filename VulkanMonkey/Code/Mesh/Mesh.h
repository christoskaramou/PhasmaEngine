#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Vertex/Vertex.h"
#include "../Image/Image.h"
#include "../../include/Stbi.h"
#include <map>

namespace vm {
	struct Effects {
		vec4 specular, ambient, diffuse;
	};

	struct Mesh
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		bool render = true, cull = false, hasAlpha = false;
		mat4 transform = mat4(1.0f);
		vec4 boundingSphere;
		void calculateBoundingSphere();
		vec4 getBoundingSphere() const;
		uint32_t vertexOffset = 0, indexOffset = 0;
		enum TextureType
		{
			DiffuseMap,
			NormalMap,
			SpecularMap,
			AlphaMap,
			RoughnessMap,
			MetallicMap
		};
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::DescriptorSet descriptorSet;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		Image texture;
		Image normalsTexture;
		Image specularTexture;
		Image alphaTexture;
		Image roughnessTexture;
		Image metallicTexture;
		Effects colorEffects;

		void loadTexture(TextureType type, const std::string& folderPath, const std::string& texName);
		void destroy();

		static std::map<std::string, Image> uniqueTextures;
	};
}