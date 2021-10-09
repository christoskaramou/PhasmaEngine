/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Renderer/Vertex.h"
#include "Material.h"
#include "Renderer/Buffer.h"
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/Document.h"
#include "GLTFSDK/GLTFResourceReader.h"
#include <map>

constexpr auto MAX_NUM_JOINTS = 128u;

namespace pe
{
	class Primitive
	{
	public:
		Primitive();
		
		~Primitive();
		
		std::string name;
		
		SPtr<vk::DescriptorSet> descriptorSet;
		SPtr<Buffer> uniformBuffer;
		
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
		std::string name;
		
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
		
		SPtr<vk::DescriptorSet> descriptorSet;
		SPtr<Buffer> uniformBuffer;
		std::vector<Vertex> vertices {};
		std::vector<uint32_t> indices {};
		uint32_t vertexOffset = 0, indexOffset = 0;
		//vec4 boundingSphere;
		
		void createUniformBuffers();
		
		//void calculateBoundingSphere();
		void destroy();
	};
}