#include "../include/Vertex.h"
#include "../include/imgui/imgui.h"

Vertex::Vertex() :
	x(0.f), y(0.f), z(0.f),
	nX(0.f), nY(0.f), nZ(0.f),
	u(0.f), v(0.f),
	tX(0.f), tY(0.f), tZ(0.f), tW(0.f),
	r(0.f), g(0.f), b(0.f), a(0.f)
{ }

Vertex::Vertex(cfloat x, cfloat y, cfloat z, cfloat nX, cfloat nY, cfloat nZ, cfloat u, cfloat v, cfloat tX, cfloat tY, cfloat tZ, cfloat tW, cfloat r, cfloat g, cfloat b, cfloat a) :
	x(x), y(y), z(z),
	nX(nX), nY(nY), nZ(nZ),
	u(u), v(v),
	tX(tX), tY(tY), tZ(tZ), tW(tW),
	r(r), g(g), b(b), a(a)
{ }

Vertex::Vertex(vm::vec3 pos, vm::vec3 norm, vm::vec2 uv, vm::vec4 tang, vm::vec4 color) :
	x(pos.x), y(pos.y), z(pos.z),
	nX(norm.x), nY(norm.y), nZ(norm.z),
	u(uv.x), v(uv.y),
	tX(tang.x), tY(tang.y), tZ(tang.z), tW(tang.w),
	r(color.x), g(color.y), b(color.z), a(color.w)
{ }

bool Vertex::operator==(const Vertex& other) const
{
	return x == other.x && y == other.y && z == other.z
		&& nX == other.nX && nY == other.nY && nZ == other.nZ
		&& u == other.u && v == other.v
		&& tX == other.tX && tY == other.tY && tZ == other.tZ && tW == other.tW
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
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(5);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(0);
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(3 * sizeof(float));
	vInputAttrDesc[2] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(2)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(6 * sizeof(float));
	vInputAttrDesc[3] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(3)
		.setFormat(vk::Format::eR32G32B32A32Sfloat) //vec4
		.setOffset(8 * sizeof(float));
	vInputAttrDesc[4] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(4)
		.setFormat(vk::Format::eR32G32B32A32Sfloat) //vec4
		.setOffset(12 * sizeof(float));

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