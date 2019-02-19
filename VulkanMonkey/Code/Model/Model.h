#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Buffer/Buffer.h"
#include "../Math/Math.h"
#include "../Mesh/Mesh.h"
#include "../Pipeline/Pipeline.h"
#include "../Script/Script.h"
#include "../Camera/Camera.h"
#include "../Deferred/Deferred.h"
#include "../Model/Animation.h"

namespace vm {
	struct Model
	{
		VulkanContext* vulkan = &VulkanContext::get();

		static std::vector<Model> models;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();
		static Pipeline* pipeline;

		vk::DescriptorSet descriptorSet;
		Buffer vertexBuffer;
		Buffer indexBuffer;
		Buffer uniformBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;

		// Animation ------------------
		Animation animation;
		// Store reference to the ASSIMP scene for accessing properties of it during animation
		std::vector<Assimp::Importer*> importers;
		const aiScene* scene;
		// ----------------------------


		mat4 transform = mat4::identity();
		mat4 previousTransform = mat4::identity();
		vec4 boundingSphere;
		bool render = true;
		bool isCopy = false;

		std::string name;
		std::vector<Mesh> meshes{};
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