#pragma once
#include "Vulkan.h"
#include "Buffer.h"
#include "Math.h"
#include "Mesh.h"
#include "Shadows.h"
#include "Pipeline.h"
#include <map>

struct Model
{
	bool render = true;
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	vk::DescriptorSet descriptorSet;
	std::map<std::string, Image> uniqueTextures{};
	std::vector<Mesh> meshes{};
	std::string name;
	vm::mat4 matrix = vm::mat4(1.0f);
	Buffer vertexBuffer, indexBuffer, uniformBuffer;
	uint32_t numberOfVertices = 0, numberOfIndices = 0;
	float initialBoundingSphereRadius = 0.0f;

	void draw(Pipeline& pipeline, vk::CommandBuffer& cmd, const uint32_t& modelID, bool deferredRenderer, Shadows* shadows = nullptr, vk::DescriptorSet* DSLights = nullptr);
	vm::vec4 getBoundingSphere();
	static Model loadModel(const std::string path, const std::string modelName, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, bool show = true);
	void createVertexBuffer(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue);
	void createIndexBuffer(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue);
	void createUniformBuffers(vk::Device device, vk::PhysicalDevice gpu);
	void createDescriptorSets(vk::Device device, vk::DescriptorPool descriptorPool);
	void destroy(vk::Device device);
};