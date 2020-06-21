#pragma once
#include "../Core/Buffer.h"
#include "../Core/Math.h"
#include "../Script/Script.h"
#include "../Camera/Camera.h"
#include "../Model/Animation.h"
#include "../Core/Node.h"
#include "../../include/GLTFSDK/GLTF.h"
#include "../../include/GLTFSDK/GLTFResourceReader.h"
#include "../../include/GLTFSDK/Document.h"
#include "StreamReader.h"

namespace vk
{
	class CommandBuffer;
	class DescriptorSet;
}

namespace vm
{
	class Pipeline;

	class Model
	{
	public:
		Model();
		~Model();
		// Document holds all info about the gltf model
		Microsoft::glTF::Document* document = nullptr;
		Microsoft::glTF::GLTFResourceReader* resourceReader = nullptr;

		static std::vector<Model> models;
		static Pipeline* pipeline;
		static Ref_t<vk::CommandBuffer> commandBuffer;
		Ref_t<vk::DescriptorSet> descriptorSet;
		Buffer uniformBuffer;
		struct UBOModel {
			mat4 matrix = mat4::identity();
			mat4 view;
			mat4 projection;
			mat4 previousMatrix;
			mat4 previousView;
		} ubo;
		vec3 scale = vec3(1.0f);
		vec3 pos = vec3(0.0f);
		vec3 rot = vec3(0.0f); // euler angles
		mat4 transform = mat4::identity();
		vec4 boundingSphere;
		bool render = true;

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
		void update(Camera& camera, double delta);
		void updateAnimation(uint32_t index, float time);
		void calculateBoundingSphere();
		void loadNode(Pointer<vm::Node> parent, const Microsoft::glTF::Node& node, const std::string& folderPath);
		void loadAnimations();
		void loadSkins();
		void readGltf(const std::filesystem::path& file);
		void loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show = true);
		void getMesh(Pointer<vm::Node>& node, const std::string& meshID, const std::string& folderPath);
		template <typename T> void getVertexData(std::vector<T>& vec, const std::string& accessorName, const Microsoft::glTF::MeshPrimitive& primitive) const;
		void getIndexData(std::vector<uint32_t>& vec, const Microsoft::glTF::MeshPrimitive& primitive) const;
		Microsoft::glTF::Image* getImage(const std::string& textureID) const;
		void loadModel(const std::string& folderPath, const std::string& modelName, bool show = true);
		void createVertexBuffer();
		void createIndexBuffer();
		void createUniformBuffers();
		void createDescriptorSets();
		void destroy();
	};
}