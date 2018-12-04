#pragma once
#include "VulkanContext.h"
#include "Math.h"
#include "Vertex.h"
#include "Image.h"
#include "Stbi.h"

namespace vm {
	struct Effects {
		vm::vec4 specular, ambient, diffuse;
	};

	struct Mesh
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		bool render = true, cull = false, hasAlpha = false;
		vm::vec4 boundingSphere;
		void calculateBoundingSphere();
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