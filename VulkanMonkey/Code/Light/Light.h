#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Buffer/Buffer.h"

namespace vm {
	constexpr auto MAX_LIGHTS = 20;

	struct Light
	{
		Light();
		Light(const vec4& color, const vec4& position, const vec4& attenuation);
		static Light sun();

		vec4 color;
		vec4 position;
		vec4 attenuation;
	};

	struct LightsUBO
	{
		vec4 camPos;
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