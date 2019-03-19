#include "Vertex.h"

using namespace vm;

Vertex::Vertex() :
	position(),
	uv(),
	normals(),
	color(),
	bonesIDs(),
	weights()
{ }

Vertex::Vertex(vec3& pos, vec2& uv, vec3& norm, vec4& color, ivec4& bonesIDs, vec4& weights) :
	position(pos),
	uv(uv),
	normals(norm),
	color(color),
	bonesIDs(bonesIDs),
	weights(weights)
{ }

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGeneral()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = sizeof(Vertex);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGUI()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = sizeof(ImDrawVert); //5 * sizeof(float);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionSkyBox()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = 4 * sizeof(float);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGeneral()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(6);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(0);
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(3 * sizeof(float));
	vInputAttrDesc[2] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(2)
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(5 * sizeof(float));
	vInputAttrDesc[3] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(3)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)	//vec4
		.setOffset(8 * sizeof(float));
	vInputAttrDesc[4] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(4)
		.setFormat(vk::Format::eR32G32B32A32Sint)	//ivec4
		.setOffset(12 * sizeof(float));
	vInputAttrDesc[5] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(5)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)	//vec4
		.setOffset(16 * sizeof(float));

	return vInputAttrDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGUI()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(3);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(IM_OFFSETOF(ImDrawVert, pos));
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(IM_OFFSETOF(ImDrawVert, uv));
	vInputAttrDesc[2] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(2)
		.setFormat(vk::Format::eR8G8B8A8Unorm)		//unsigned integer
		.setOffset(IM_OFFSETOF(ImDrawVert, col));

	return vInputAttrDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionSkyBox()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(1);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32A32Sfloat)	//vec4
		.setOffset(0);

	return vInputAttrDesc;
}
