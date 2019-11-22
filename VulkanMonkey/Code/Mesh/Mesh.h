#pragma once
#include "../Vertex/Vertex.h"
#include "../Material/Material.h"
#include "../Buffer/Buffer.h"
#include "../../include/GLTFSDK/GLTF.h"
#include "../../include/GLTFSDK/Document.h"
#include "../../include/GLTFSDK/GLTFResourceReader.h"
#include <map>

#undef min
#undef max

constexpr auto MAX_NUM_JOINTS = 128u;

namespace vm {
	struct Primitive {
		Primitive(): pbrMaterial({}) {}

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout* getDescriptorSetLayout();
		vk::DescriptorSet descriptorSet;
		Buffer uniformBuffer;

		bool render = true, cull = true;
		uint32_t vertexOffset = 0, indexOffset = 0;
		uint32_t verticesSize = 0, indicesSize = 0;
		PBRMaterial pbrMaterial;
		vec3 min;
		vec3 max;
		vec4 boundingSphere;
		vec4 transformedBS;
		bool hasBones = false;
		void calculateBoundingSphere() {
			const vec3 center = (max + min) * .5f;
			const float sphereRadius = length(max - center);
			boundingSphere = vec4(center, sphereRadius);
		}
		void loadTexture(
			MaterialType type,
			const std::string& folderPath,
			const Microsoft::glTF::Image* image = nullptr,
			const Microsoft::glTF::Document* document = nullptr,
			const Microsoft::glTF::GLTFResourceReader* resourceReader = nullptr);
	};

	struct Mesh
	{
		bool render = true, cull = false;

		struct UBOMesh {
			mat4 matrix;
			mat4 previousMatrix;
			mat4 jointMatrix[MAX_NUM_JOINTS];
			float jointcount{ 0 };
			float dummy[3];
		} ubo;

		static std::map<std::string, Image> uniqueTextures;
		std::vector<Primitive> primitives{};

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout* getDescriptorSetLayout();
		vk::DescriptorSet descriptorSet;
		Buffer uniformBuffer;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		uint32_t vertexOffset = 0, indexOffset = 0;
		//vec4 boundingSphere;

		void createUniformBuffers();
		//void calculateBoundingSphere();
		void destroy();
	};
}