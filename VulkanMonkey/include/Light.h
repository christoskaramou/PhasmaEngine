#pragma once
#include "Math.h"
#include "Buffer.h"

constexpr auto MAX_LIGHTS = 20;

struct Light
{
	Light();
	Light(const vm::vec4& color, const vm::vec4& position, const vm::vec4& attenuation);
	static Light sun();

	vm::vec4 color;
	vm::vec4 position;
	vm::vec4 attenuation;
};

struct LightsUBO
{
	vm::vec4 camPos;
	Light sun = Light::sun();
	Light lights[MAX_LIGHTS];
};

struct LightUniforms : Light
{
	Buffer uniform;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	void createLightUniforms(vk::Device device, vk::PhysicalDevice gpu, vk::DescriptorPool descriptorPool);
};
