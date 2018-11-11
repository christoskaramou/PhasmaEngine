#pragma once
#include "Vulkan.h"
#include "Math.h"

struct Vertex
{
	Vertex();
	Vertex(cfloat x, cfloat y, cfloat z,
		cfloat nX, cfloat nY, cfloat nZ,
		cfloat u, cfloat v,
		cfloat tX, cfloat tY, cfloat tZ, cfloat tW,
		cfloat r, cfloat g, cfloat b, cfloat a);
	Vertex(vm::vec3 pos, vm::vec3 norm, vm::vec2 uv, vm::vec4 tang, vm::vec4 color);

	bool operator==(const Vertex& other) const;
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGeneral();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGUI();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionSkyBox();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGeneral();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGUI();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionSkyBox();

	float 
		x, y, z,			// Vertex Position
		nX, nY, nZ,			// Normals x, y, z
		u, v,				// Texture u, _v
		tX, tY, tZ, tW,		// Tangents
		r, g, b, a;			// color rgba

};