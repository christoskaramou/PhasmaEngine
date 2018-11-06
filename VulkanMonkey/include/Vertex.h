#pragma once
#include "Vulkan.h"

struct Vertex
{
	float	x,	y,	z,			// Vertex Position
			nX, nY, nZ,			// Normals x, y, z
			u,	v,				// Texture u, _v
			tX, tY, tZ, tW,		// Tangents
			r,	g,	b,	a;		// color rgba

	bool operator==(const Vertex& other) const;
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGeneral();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGUI();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionSkyBox();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGeneral();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGUI();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionSkyBox();
};