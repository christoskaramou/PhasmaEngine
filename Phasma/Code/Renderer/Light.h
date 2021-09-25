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
#define MAX_POINT_LIGHTS 10
#define MAX_SPOT_LIGHTS 10
	
	struct Light
	{
	};
	
	struct DirectionalLight : public Light
	{
		vec4 color; // .w = intensity
		vec4 direction;
	};
	
	struct PointLight : public Light
	{
		vec4 color; // .w = intensity
		vec4 position;
	};
	
	struct SpotLight : public Light
	{
		vec4 color; // .w = intensity
		vec4 start; // .w = radius;
		vec4 end;
	};
	
	struct LightsUBO
	{
		vec4 camPos;
		DirectionalLight sun;
		PointLight pointLights[MAX_POINT_LIGHTS];
		SpotLight spotLights[MAX_SPOT_LIGHTS];
	};

	class LightSystem : public ISystem
	{
	public:
		LightSystem();

		~LightSystem();

		void Init() override;

		void Update(double delta) override;

		void Destroy() override;

		Buffer& GetUniform() { return uniform; }

	private:
		LightsUBO lubo;
		Buffer uniform;
		Ref<vk::DescriptorSet> descriptorSet;
	};
}