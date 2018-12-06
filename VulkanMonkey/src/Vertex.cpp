#include "../include/Vertex.h"
#include "../include/imgui/imgui.h"

using namespace vm;

Vertex::Vertex() :
	x(0.f), y(0.f), z(0.f),
	u(0.f), v(0.f),
	nX(0.f), nY(0.f), nZ(0.f),
	tX(0.f), tY(0.f), tZ(0.f),
	bX(0.f), bY(0.f), bZ(0.f),
	r(0.f), g(0.f), b(0.f), a(0.f)
{ }

Vertex::Vertex(cfloat x, cfloat y, cfloat z, cfloat u, cfloat v, cfloat nX, cfloat nY, cfloat nZ, cfloat tX, cfloat tY, cfloat tZ, cfloat bX, cfloat bY, cfloat bZ, cfloat r, cfloat g, cfloat b, cfloat a) :
	x(x), y(y), z(z),
	u(u), v(v),
	nX(nX), nY(nY), nZ(nZ),
	tX(tX), tY(tY), tZ(tZ),
	bX(bX), bY(bY), bZ(bZ),
	r(r), g(g), b(b), a(a)
{ }

Vertex::Vertex(vec3 pos, vec2 uv, vec3 norm, vec3 tang, vec3 bitang, vec4 color) :
	x(pos.x), y(pos.y), z(pos.z),
	u(uv.x), v(uv.y),
	nX(norm.x), nY(norm.y), nZ(norm.z),
	tX(tang.x), tY(tang.y), tZ(tang.z),
	bX(tang.x), bY(tang.y), bZ(tang.z),
	r(color.x), g(color.y), b(color.z), a(color.w)
{ }

bool Vertex::operator==(const Vertex& other) const
{
	return x == other.x && y == other.y && z == other.z
		&& u == other.u && v == other.v
		&& nX == other.nX && nY == other.nY && nZ == other.nZ
		&& tX == other.tX && tY == other.tY && tZ == other.tZ
		&& bX == other.bX && bY == other.bY && bZ == other.bZ
		&& r == other.r && g == other.g && b == other.b && a == other.a;
}

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
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(8 * sizeof(float));
	vInputAttrDesc[4] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(4)
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(11 * sizeof(float));
	vInputAttrDesc[5] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(5)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)	//vec4
		.setOffset(14 * sizeof(float));

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
