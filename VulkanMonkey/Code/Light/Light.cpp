#include "Light.h"

using namespace vm;

Light::Light() :
	color(rand(0.f, 1.f), rand(0.0f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f)),
	position(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 1.f),
	attenuation(1.05f, 1.f, 1.f, 1.f)
{ }

Light::Light(const vec4& color, const vec4& position, const vec4& attenuation) :
	color(color),
	position(position),
	attenuation(attenuation)
{ }

Light Light::sun()
{
	return Light(
		vec4(1.f, 1.f, 1.f, .5f),
		vec4(0.f, 300.f, 10.f, 1.f),
		vec4(0.f, 0.f, 1.f, 1.f)
	);
}

void LightUniforms::createLightUniforms()
{
	uniform.createBuffer(sizeof(LightsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniform.data = vulkan->device.mapMemory(uniform.memory, 0, uniform.size);
	LightsUBO lubo;
	//lubo.lights.resize(MAX_LIGHTS);
	memcpy(uniform.data, &lubo, uniform.size);

	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);
	auto writeSet = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)								// DescriptorSet dstSet;
		.setDstBinding(0)										// uint32_t dstBinding;
		.setDstArrayElement(0)									// uint32_t dstArrayElement;
		.setDescriptorCount(1)									// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniform.buffer)							// Buffer buffer;
			.setOffset(0)											// DeviceSize offset;
			.setRange(uniform.size));							// DeviceSize range;
	vulkan->device.updateDescriptorSets(writeSet, nullptr);
}

void LightUniforms::destroy()
{
	uniform.destroy();
	if (descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(descriptorSetLayout);
		descriptorSetLayout = nullptr;
	}
}

void LightUniforms::update(Camera& camera)
{
	if (GUI::randomize_lights) {
		GUI::randomize_lights = false;
		LightsUBO lubo;
		lubo.camPos = vec4(camera.position, 1.0f);
		//lubo.lights.resize(MAX_LIGHTS);
		memcpy(uniform.data, &lubo, sizeof(lubo));
	}
	else {
		vec4 camPos(camera.position, 1.0f);
		memcpy(uniform.data, &camPos, sizeof(camPos));
	}
}
