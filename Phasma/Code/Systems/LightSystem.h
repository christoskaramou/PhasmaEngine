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

#include "GUI/GUI.h"
#include "ECS/System.h"
#include "Renderer/Light.h"

namespace pe
{
	constexpr uint32_t MAX_POINT_LIGHTS = 10;
	constexpr uint32_t MAX_SPOT_LIGHTS = 10;

	struct LightsUBO
	{
		vec4 camPos;
		DirectionalLight sun;
		PointLight pointLights[MAX_POINT_LIGHTS];
		SpotLight spotLights[MAX_SPOT_LIGHTS];
	};

	class Descriptor;
	class Buffer;

	class LightSystem : public ISystem
	{
	public:
		LightSystem();

		~LightSystem();

		void Init() override;

		void Update(double delta) override;

		void Destroy() override;

		Buffer& GetUniform() { return *uniform; }

	private:
		LightsUBO lubo;
		Buffer* uniform;
		Descriptor* descriptorSet;
	};
}