#pragma once
#include "../../include/Vulkan.h"
#include "../../include/imgui/imgui.h"
#include "../Math/Math.h"

namespace vm {
	struct Vertex
	{
		Vertex();
		Vertex(vec3& pos, vec2& uv, vec3& norm, vec3& tang, vec3& bitang, vec4& color);

		bool operator==(const Vertex& other) const;
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGeneral();
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGUI();
		static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionSkyBox();
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGeneral();
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGUI();
		static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionSkyBox();

		vec3 position;			// Position
		vec2 uv;				// UVs
		vec3 normals;			// Normals
		vec3 tangents;			// Tangents
		vec3 bitangents;		// Bitangents
		vec4 color;				// Color
	};
}