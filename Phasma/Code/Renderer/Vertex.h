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

#include "../Core/Math.h"

namespace vk
{
	struct VertexInputBindingDescription;
	struct VertexInputAttributeDescription;
}

namespace pe
{
	class Vertex
	{
	public:
		Vertex();
		
		Vertex(vec3& pos, vec2& uv, vec3& norm, vec4& color, ivec4& bonesIDs, vec4& weights);
		
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGeneral();
		
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGUI();
		
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionSkyBox();
		
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGeneral();
		
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGUI();
		
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionSkyBox();
		
		vec3 position;
		vec2 uv;
		vec3 normals;
		vec4 color;
		ivec4 bonesIDs;
		vec4 weights;
	};
}