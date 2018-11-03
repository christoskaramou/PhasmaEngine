#pragma once
#include "Vulkan.h"
#include "Math.h"
#include "Vertex.h"
#include "Image.h"
#include "Stbi.h"

struct Effects {
	vm::vec4 specular, ambient, diffuse;
};

struct Mesh
{
	bool render = true, cull = false;
	vm::vec4 boundingSphere;
	void calculateBoundingSphere();
	uint32_t vertexOffset, indexOffset;
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
	Image texture, normalsTexture, specularTexture, alphaTexture;
	Effects colorEffects;

	void loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, TextureType type, const std::string path);
	void destroy(vk::Device device);
};