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

#include "../ECS/Component.h"
#include "../ECS/System.h"
#include "../Core/Math.h"
#include "../Renderer/Compute.h"

namespace pe
{
	class Camera;
	
	class CameraSystem : public ISystem
	{
	public:
		CameraSystem() = default;
		
		~CameraSystem() override = default;
		
		Camera* GetCamera(size_t index);
		
		// Inherited via ISystem
		void Init() override;
		
		void Update(double delta) override;
		
		void Destroy() override;
	};
	
	class Camera : public IComponent
	{
	public:
		enum class RelativeDirection
		{
			FORWARD,
			BACKWARD,
			LEFT,
			RIGHT
		};
		
		struct Viewport
		{
			float x;
			float y;
			float width;
			float height;
			float minDepth;
			float maxDepth;
		};
		
		struct TargetArea
		{
			Viewport viewport;
			Rect2D scissor;
			
			void Update(const vec2& position, const vec2& size, float minDepth = 0.f, float maxDepth = 1.f);
		};
		
		struct Plane
		{
			vec3 normal;
			float d;
		};
		
		mat4 view, previousView;
		mat4 projection, previousProjection;
		mat4 viewProjection, previousViewProjection;
		mat4 invView, invProjection, invViewProjection;
		quat orientation;
		vec3 position, euler, worldOrientation;
		vec3 front, right, up;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		vec2 projOffset, projOffsetPrevious;
		TargetArea renderArea;
		std::vector<Plane> frustum {};
		Compute frustumCompute;
		
		Camera();
		
		void Update();
		
		void UpdatePerspective();
		
		void UpdateView();
		
		vec3 WorldFront() const;
		
		vec3 WorldRight() const;
		
		vec3 WorldUp() const;
		
		void Move(RelativeDirection direction, float velocity);
		
		void Rotate(float xoffset, float yoffset);
		
		void ExtractFrustum();
		
		bool SphereInFrustum(const vec4& boundingSphere) const;
		
		void ReCreateComputePipelines();
	};
}
