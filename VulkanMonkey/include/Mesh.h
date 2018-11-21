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
		VulkanContext* vulkan;
		Mesh(VulkanContext* vulkan);

		bool render = true, cull = false, hasAlpha = false;
		vm::vec4 boundingSphere;
		void calculateBoundingSphere();
		uint32_t vertexOffset = 0, indexOffset = 0;
		enum TextureType
		{
			DiffuseMap,
			NormalMap,
			SpecularMap,
			AlphaMap
		};
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::DescriptorSet descriptorSet;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		Image texture = Image(vulkan);
		Image normalsTexture = Image(vulkan);
		Image specularTexture = Image(vulkan);
		Image alphaTexture = Image(vulkan);
		Effects colorEffects;

		void loadTexture(TextureType type, const std::string path);
		void destroy();
	};
}