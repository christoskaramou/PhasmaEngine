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
		Microsoft::glTF::Document document;
		Microsoft::glTF::GLTFResourceReader* resourceReader;

		static std::vector<Model> models;
		static Pipeline* pipeline;

		mat4 transform = mat4::identity();
		vec4 boundingSphere;
		bool render = true;
		bool isCopy = false;

		std::string name;
		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;
		std::vector<Skin*> skins;
		std::vector<Animation> animations;
		std::vector<std::string> extensions;

		int32_t animationIndex = 0;
		float animationTimer = 0.0f;

		std::unique_ptr<Script> script;

		Buffer vertexBuffer;
		Buffer indexBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;

		static void batchStart(uint32_t imageIndex, Deferred& deferred);
		static void batchEnd();
		void draw();
		void update(Camera& camera, float delta);
		void updateAnimation(uint32_t index, float time);
		vec4 getBoundingSphere();
		void loadNode(vm::Node* parent, const Microsoft::glTF::Node& node, const std::string& folderPath);
		void loadAnimations();
		void loadSkins();
		void readGltf(const std::filesystem::path& file);
		void loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show = true);
		void getMesh(vm::Node* node, const std::string& meshID, const std::string& folderPath);
		Microsoft::glTF::Image* getImage(const std::string& textureID);
		void loadModel(const std::string& folderPath, const std::string& modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}