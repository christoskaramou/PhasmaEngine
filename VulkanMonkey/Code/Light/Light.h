#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Buffer/Buffer.h"
#include "../Camera/Camera.h"
#include "../GUI/GUI.h"
#include <vector>

namespace vm {
	#define MAX_LIGHTS 50

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
#ifdef MAX_LIGHTS
	#if MAX_LIGHTS == 0 // to avoid warnings when 0 lights are set
		std::array<Light, MAX_LIGHTS> lights{};
	#else
		Light lights[MAX_LIGHTS];
	#endif
#endif
	};

	struct LightUniforms : Light
	{
		VulkanContext* vulkan = &VulkanContext::getVulkanContext();

		Buffer uniform;
		vk::DescriptorSet descriptorSet;
		vk::DescriptorSetLayout descriptorSetLayout;

		void update(Camera& camera);
		void createLightUniforms();
		void destroy();
	};
}