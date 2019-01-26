#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Math/Math.h"
#include "../Mesh/Mesh.h"
#include "../Shadows/Shadows.h"
#include "../Pipeline/Pipeline.h"
#include "../Script/Script.h"
#include <map>
#include <utility>

namespace vm {
	struct Model
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		bool render = true;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		vk::DescriptorSet descriptorSet;
		std::vector<Mesh> meshes{};
		std::string name;
		mat4 transform = mat4(1.0f);
		Buffer vertexBuffer;
		Buffer indexBuffer;
		Buffer uniformBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;
		vec4 boundingSphere;
		std::unique_ptr<Script> script;

		void draw(Pipeline& pipeline, vk::CommandBuffer& cmd, const uint32_t& modelID, bool deferredRenderer, Shadows* shadows = nullptr, vk::DescriptorSet* DSLights = nullptr);
		vec4 getBoundingSphere();
		void loadModel(const std::string& folderPath, const std::string& modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}