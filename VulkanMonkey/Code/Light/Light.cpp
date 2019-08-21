#include "Light.h"

using namespace vm;

vk::DescriptorSetLayout LightUniforms::descriptorSetLayout = nullptr;

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
	getDescriptorSetLayout();

	LightsUBO lubo;
	uniform.createBuffer(sizeof(LightsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniform.map();
	uniform.copyData(&lubo);

	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;
	descriptorSet = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo).at(0);

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
	writeSet.pBufferInfo = &dbi;
	VulkanContext::get()->device.updateDescriptorSets(writeSet, nullptr);
}

void LightUniforms::destroy()
{
	uniform.destroy();
	if (descriptorSetLayout) {
		VulkanContext::get()->device.destroyDescriptorSetLayout(descriptorSetLayout);
		descriptorSetLayout = nullptr;
	}
}

void LightUniforms::update(const Camera& camera) const
{
	if (GUI::randomize_lights) {
		GUI::randomize_lights = false;
		LightsUBO lubo;
		lubo.camPos = vec4(camera.position, 1.0f);
		memcpy(uniform.data, &lubo, sizeof(lubo));
	}
	const vec3 sunPos(GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2]);
	const float angle = dot(normalize(sunPos), vec3(0.f, 1.f, 0.f));
	vec4 values[3] = { // = {camPos, suncolor, sunposition}
		{ camera.position, 1.0f },
		{ .9765f * GUI::sun_intensity, .8431f * GUI::sun_intensity, .9098f * GUI::sun_intensity, GUI::shadow_cast ? angle * 0.5f : angle * 0.25f},
		{ sunPos.x, sunPos.y, sunPos.z, 1.0f }
	};
	memcpy(uniform.data, values, sizeof(values));
}

vk::DescriptorSetLayout LightUniforms::getDescriptorSetLayout()
{
	// binding for model mvp matrix
	if (!descriptorSetLayout) {
		vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding;
		descriptorSetLayoutBinding.binding = 0;
		descriptorSetLayoutBinding.descriptorCount = 1;
		descriptorSetLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorSetLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &descriptorSetLayoutBinding;

		descriptorSetLayout = VulkanContext::get()->device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}
