#pragma once
#include <vulkan/vulkan.hpp>
#include "Math.h"

namespace vm
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