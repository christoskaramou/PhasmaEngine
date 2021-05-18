#pragma once

#include "../Core/Buffer.h"
#include "../Core/Math.h"
#include "../Script/Script.h"
#include "../Camera/Camera.h"
#include "../Model/Animation.h"
#include "../Core/Node.h"
#include "../../Include/GLTFSDK/GLTF.h"
#include "../../Include/GLTFSDK/GLTFResourceReader.h"
#include "../../Include/GLTFSDK/Document.h"
#include "StreamReader.h"

namespace vk
{
	class CommandBuffer;
	class DescriptorSet;
}

namespace pe
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
		static Ref<vk::CommandBuffer> commandBuffer;
		Ref<vk::DescriptorSet> descriptorSet;
		Buffer uniformBuffer;
		struct UBOModel
		{
			mat4 matrix = mat4::identity();
			mat4 mvp = mat4::identity();
			mat4 previousMvp = mat4::identity();
		} ubo;
		vec3 scale = vec3(1.0f);
		vec3 pos = vec3(0.0f);
		vec3 rot = vec3(0.0f); // euler angles
		mat4 transform = mat4::identity();
		vec4 boundingSphere;
		bool render = true;

		std::string name;
		std::string fullPathName;
		std::vector<pe::Node*> linearNodes {};
		std::vector<Skin*> skins {};
		std::vector<Animation> animations {};
		std::vector<std::string> extensions {};

		int32_t animationIndex = 0;
		float animationTimer = 0.0f;
#if 0
		Script* script = nullptr;
#else
		void* script = nullptr;
#endif

		Buffer vertexBuffer;
		Buffer indexBuffer;
		uint32_t numberOfVertices = 0, numberOfIndices = 0;

		void draw(uint16_t renderQueue);

		void update(Camera& camera, double delta);

		void updateAnimation(uint32_t index, float time);

		void calculateBoundingSphere();

		void loadNode(pe::Node* parent, const Microsoft::glTF::Node& node, const std::string& folderPath);

		void loadAnimations();

		void loadSkins();

		void readGltf(const std::filesystem::path& file);

		void loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show = true);

		void getMesh(pe::Node* node, const std::string& meshID, const std::string& folderPath) const;

		template <typename T>
		void getVertexData(
				std::vector<T>& vec, const std::string& accessorName, const Microsoft::glTF::MeshPrimitive& primitive
		) const;

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