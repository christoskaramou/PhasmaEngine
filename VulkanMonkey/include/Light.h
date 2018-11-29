#pragma once

#include "VulkanContext.h"
#include "Math.h"
#include "Buffer.h"

namespace vm {
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
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer uniform;
		vk::DescriptorSet descriptorSet;
		vk::DescriptorSetLayout descriptorSetLayout;

		void createLightUniforms();
		void destroy();
	};
}