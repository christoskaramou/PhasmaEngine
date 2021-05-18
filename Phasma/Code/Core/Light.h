#pragma once

#include "Math.h"
#include "Buffer.h"
#include "../Camera/Camera.h"
#include "../GUI/GUI.h"
#include "../ECS/Component.h"

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
}

namespace pe
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

	class DirectionalLight : public IComponent
	{
	public:
		DirectionalLight();

		vec4 color;
		vec3 direction;
	};

	class PointLight : public IComponent
	{
	public:
		PointLight();

		vec4 color;
		vec3 position;
		float radius;
	};

	class SpotLight : public IComponent
	{
	public:
		SpotLight();

		vec4 color;
		vec4 start;
		vec3 end;
		float radius;
	};

	struct LightsUBO
	{
		vec4 camPos;
		Light sun
				{
						{.9765f, .8431f, .9098f, GUI::sun_intensity},
						{GUI::sun_position[0], GUI::sun_position[1], GUI::sun_position[2], 1.0f}
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
		Ref<vk::DescriptorSet> descriptorSet;
		static Ref<vk::DescriptorSetLayout> descriptorSetLayout;

		static const vk::DescriptorSetLayout& getDescriptorSetLayout();

		void update(const Camera& camera);

		void createLightUniforms();

		void destroy();
	};
}