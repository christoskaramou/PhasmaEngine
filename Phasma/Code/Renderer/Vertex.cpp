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

#include "PhasmaPch.h"
#include "Vertex.h"
#include "imgui/imgui.h"

namespace pe
{
#define VertexOffset(x) offsetof(Vertex, x)
	
	Vertex::Vertex()
	{}
	
	Vertex::Vertex(vec3& pos, vec2& uv, vec3& norm, vec4& color, ivec4& bonesIDs, vec4& weights) :
			position(pos),
			uv(uv),
			normals(norm),
			color(color),
			bonesIDs(bonesIDs),
			weights(weights)
	{}
	
	std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGeneral()
	{
		return {{0, sizeof(Vertex), vk::VertexInputRate::eVertex}};
	}
	
	std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGUI()
	{
		return {{0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex}};
	}
	
	std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionSkyBox()
	{
		return {{0, sizeof(vec4), vk::VertexInputRate::eVertex}};
	}
	
	std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGeneral()
	{
		return {
				{0, 0, vk::Format::eR32G32B32Sfloat,    VertexOffset(position)},    // vec3
				{1, 0, vk::Format::eR32G32Sfloat,       VertexOffset(uv)},            // vec2
				{2, 0, vk::Format::eR32G32B32Sfloat,    VertexOffset(normals)},    // vec3
				{3, 0, vk::Format::eR32G32B32A32Sfloat, VertexOffset(color)},        // vec4
				{4, 0, vk::Format::eR32G32B32A32Sint,   VertexOffset(bonesIDs)},    // ivec4
				{5, 0, vk::Format::eR32G32B32A32Sfloat, VertexOffset(weights)}        // vec4
		};
	}
	
	std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGUI()
	{
		return {
				{0, 0, vk::Format::eR32G32Sfloat,  IM_OFFSETOF(ImDrawVert, pos)},                // vec2
				{1, 0, vk::Format::eR32G32Sfloat,  IM_OFFSETOF(ImDrawVert, uv)},                // vec2
				{2, 0, vk::Format::eR8G8B8A8Unorm, IM_OFFSETOF(ImDrawVert, ImDrawVert::col)}    // color packed to uint
		};
	}
	
	std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionSkyBox()
	{
		return {
				{0, 0, vk::Format::eR32G32B32A32Sfloat, 0}    // vec4
		};
	}
}
