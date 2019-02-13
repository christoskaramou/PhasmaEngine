#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Math/Math.h"
#include "../Mesh/Mesh.h"
#include "../Pipeline/Pipeline.h"
#include "../Script/Script.h"
#include "../Camera/Camera.h"
#include "../Deferred/Deferred.h"

namespace vm {
	struct Model
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		bool render = true;
		bool isCopy = false;
		static std::vector<Model> models;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		static Pipeline* pipeline;
		vk::DescriptorSet descriptorSet;
		std::vector<Mesh> meshes{};
		std::string name;
		mat4 transform = mat4(1.0f);
		mat4 previousTransform = mat4(1.0f);
		Buffer vertexBuffer;
		Buffer indexBuffer;
		Buffer uniformBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;
		vec4 boundingSphere;
		std::unique_ptr<Script> script;

		static void batchStart(uint32_t imageIndex, Deferred& deferred);
		static void batchEnd();
		void draw();
		void update(Camera& camera, float delta);
		vec4 getBoundingSphere();
		void loadModel(const std::string& folderPath, const std::string& modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}