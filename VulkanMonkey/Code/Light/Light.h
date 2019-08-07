#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../Math/Math.h"
#include "../Buffer/Buffer.h"
#include "../Camera/Camera.h"
#include "../GUI/GUI.h"

namespace vm {
	#define MAX_LIGHTS 25

	struct Light
	{
		Light();
		Light(const vec4& color, const vec4& position);

		vec4 color;
		vec4 position;
	};

	struct LightsUBO
	{
		vec4 camPos;
		Light sun = { { .9765f, .8431f, .9098f, 1.f }, { GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f } };
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
		Buffer uniform;
		vk::DescriptorSet descriptorSet;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();

		void update(Camera& camera) const;
		void createLightUniforms();
		void destroy();
	};
}