#pragma once

#include "Math.h"
#include "Buffer.h"
#include "../Camera/Camera.h"
#include "../GUI/GUI.h"

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
}

namespace vm
{
	#define MAX_LIGHTS 10

	class Light
	{
	public:
		Light();
		Light(const vec4& color, const vec4& position);

		vec4 color;
		vec4 position;
	};

	struct LightsUBO
	{
		vec4 camPos;
		Light sun
		{
			{ .9765f, .8431f, .9098f, GUI::sun_intensity },
			{ GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f }
		};
#ifdef MAX_LIGHTS
	#if MAX_LIGHTS == 0 // to avoid warnings when 0 lights are set
		std::array<Light, MAX_LIGHTS> lights{};
	#else
		Light lights[MAX_LIGHTS];
	#endif
#endif
	};

	class LightUniforms : public Light
	{
	public:
		LightUniforms();
		~LightUniforms();
		LightsUBO lubo;
		Buffer uniform;
		Ref_t<vk::DescriptorSet> descriptorSet;
		static Ref_t<vk::DescriptorSetLayout> descriptorSetLayout;
		static const vk::DescriptorSetLayout& getDescriptorSetLayout();

		void update(const Camera& camera);
		void createLightUniforms();
		void destroy();
	};
}