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
#include "../Node/Node.h"
#include "../Compute/Compute.h"
#include "../../include/GLTFSDK/GLTF.h"
#include "../../include/GLTFSDK/GLTFResourceReader.h"
#include "../../include/GLTFSDK/GLBResourceReader.h"
#include "../../include/GLTFSDK/Deserialize.h"
#include "../../include/GLTFSDK/Document.h"
#include "StreamReader.h"
#include <memory>

namespace vm {
	struct Model
	{
		VulkanContext* vulkan = &VulkanContext::get();

		// Document holds all info about the gltf model
		Microsoft::glTF::Document* document = nullptr;
		Microsoft::glTF::GLTFResourceReader* resourceReader = nullptr;

		static std::vector<Model> models;
		static Pipeline* pipeline;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();
		vk::DescriptorSet descriptorSet;
		Buffer uniformBuffer;
		struct UBOModel {
			mat4 matrix = mat4::identity();
			mat4 view;
			mat4 projection;
			mat4 previousMatrix;
		} ubo;
		vec3 scale = vec3(1.0f);
		vec3 pos = vec3(0.0f);
		vec3 rot = vec3(0.0f); // euler angles
		mat4 transform = mat4::identity();
		vec4 boundingSphere;
		bool render = true;
		bool isCopy = false;

		std::string name;
		std::string fullPathName;
		//std::vector<Pointer<vm::Node>> nodes{};
		std::vector<Pointer<vm::Node>> linearNodes{};
		std::vector<Pointer<Skin>> skins{};
		std::vector<Animation> animations{};
		std::vector<std::string> extensions{};

		int32_t animationIndex = 0;
		float animationTimer = 0.0f;

		Script* script = nullptr;

		Buffer vertexBuffer;
		Buffer indexBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;

		void draw();
		void update(Camera& camera, float delta);
		void updateAnimation(uint32_t index, float time);
		void calculateBoundingSphere();
		void loadNode(Pointer<vm::Node> parent, const Microsoft::glTF::Node& node, const std::string& folderPath);
		void loadAnimations();
		void loadSkins();
		void readGltf(const std::filesystem::path& file);
		void loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show = true);
		void getMesh(Pointer<vm::Node>& node, const std::string& meshID, const std::string& folderPath);
		template <typename T>
		void getVertexData(std::vector<T>& vec, const std::string& accessorName, const Microsoft::glTF::MeshPrimitive& primitive);
		void getIndexData(std::vector<uint32_t>& vec, const Microsoft::glTF::MeshPrimitive& primitive);
		Microsoft::glTF::Image* getImage(const std::string& textureID);
		void loadModel(const std::string& folderPath, const std::string& modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}