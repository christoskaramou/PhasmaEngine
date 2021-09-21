/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "../Core/Math.h"
#include "Buffer.h"
#include "../Camera/Camera.h"
#include "../GUI/GUI.h"
#include "../ECS/Component.h"

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
		vec4 color;
		vec3 direction;
	};
	
	class PointLight : public IComponent
	{
	public:
		vec4 color;
		vec3 position;
		float radius;
	};
	
	class SpotLight : public IComponent
	{
	public:
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
						{.9765f,               .8431f,               .9098f,               GUI::sun_intensity},
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