#pragma once

#include "../Renderer/Vertex.h"
#include "Material.h"
#include "../Renderer/Buffer.h"
#include "../../Include/GLTFSDK/GLTF.h"
#include "../../Include/GLTFSDK/Document.h"
#include "../../Include/GLTFSDK/GLTFResourceReader.h"
#include <map>

constexpr auto MAX_NUM_JOINTS = 128u;

namespace vk
{
	class DescriptorSet;
}

namespace pe
{
	class Primitive
	{
	public:
		Primitive();
		
		~Primitive();
		
		std::string name;
		
		Ref<vk::DescriptorSet> descriptorSet;
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
		
		void calculateBoundingSphere()
		{
			const vec3 center = (max + min) * .5f;
			const float sphereRadius = length(max - center);
			boundingSphere = vec4(center, sphereRadius);
		}
		
		void loadTexture(
				MaterialType type,
				const std::string& folderPath,
				const Microsoft::glTF::Image* image = nullptr,
				const Microsoft::glTF::Document* document = nullptr,
				const Microsoft::glTF::GLTFResourceReader* resourceReader = nullptr
		);
	};
	
	class Mesh
	{
	public:
		Mesh();
		
		~Mesh();
		
		bool render = true, cull = false;
		
		struct UBOMesh
		{
			mat4 matrix;
			mat4 previousMatrix;
			mat4 jointMatrix[MAX_NUM_JOINTS];
			float jointcount {0};
			float dummy[3];
		} ubo;
		
		static std::map<std::string, Image> uniqueTextures;
		std::vector<Primitive> primitives {};
		
		Ref<vk::DescriptorSet> descriptorSet;
		Buffer uniformBuffer;
		std::vector<Vertex> vertices {};
		std::vector<uint32_t> indices {};
		uint32_t vertexOffset = 0, indexOffset = 0;
		//vec4 boundingSphere;
		
		void createUniformBuffers();
		
		//void calculateBoundingSphere();
		void destroy();
	};
}