#include "Light.h"

using namespace vm;

Light::Light() :
	color(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.f),
	position(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 1.f)
{ }

Light::Light(const vec4& color, const vec4& position) :
	color(color),
	position(position)
{ }

void LightUniforms::createLightUniforms()
{
	uniform.createBuffer(sizeof(LightsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniform.data = vulkan->device.mapMemory(uniform.memory, 0, uniform.size);
	LightsUBO lubo;
	memcpy(uniform.data, &lubo, uniform.size);

	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	vk::DescriptorBufferInfo dbi;
	dbi.buffer = uniform.buffer;
	dbi.offset = 0;
	dbi.range = uniform.size;

	vk::WriteDescriptorSet writeSet;
	writeSet.dstSet = descriptorSet;
	writeSet.dstBinding = 0;
	writeSet.dstArrayElement = 0;
	writeSet.descriptorCount = 1;
	writeSet.descriptorType = vk::DescriptorType::eUniformBuffer;
	writeSet.pBufferInfo = &dbi;							// DeviceSize range;
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
		memcpy(uniform.data, &lubo, sizeof(lubo));
	}
	else {
		vec4 values[3] = {
			{ camera.position, 1.0f },
			{ .9765f, .8431f, .9098f, 1.f },
			{ GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f }
		};
		values[1] *= GUI::sun_intensity;
		memcpy(uniform.data, values, sizeof(values));
	}
}
