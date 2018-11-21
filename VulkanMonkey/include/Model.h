#pragma once
#include "VulkanContext.h"
#include "Buffer.h"
#include "Math.h"
#include "Mesh.h"
#include "Shadows.h"
#include "Pipeline.h"
#include <map>

namespace vm {
	struct Model
	{
		VulkanContext* vulkan;
		Model(VulkanContext* vulkan);

		bool render = true;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::DescriptorSet descriptorSet;
		std::map<std::string, Image> uniqueTextures;
		std::vector<Mesh> meshes;
		std::string name;
		vm::mat4 matrix = vm::mat4(1.0f);
		Buffer vertexBuffer = Buffer(vulkan);
		Buffer indexBuffer = Buffer(vulkan);
		Buffer uniformBuffer = Buffer(vulkan);
		uint32_t numberOfVertices = 0, numberOfIndices = 0;
		float initialBoundingSphereRadius = 0.0f;

		void draw(Pipeline& pipeline, vk::CommandBuffer& cmd, const uint32_t& modelID, bool deferredRenderer, Shadows* shadows = nullptr, vk::DescriptorSet* DSLights = nullptr);
		vm::vec4 getBoundingSphere();
		void loadModel(const std::string path, const std::string modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}