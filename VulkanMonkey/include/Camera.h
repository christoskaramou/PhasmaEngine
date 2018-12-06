#pragma once
#include "Math.h"
#include "GUI.h"
#include "Vulkan.h"

namespace vm {
	struct Camera
	{
		enum struct RelativeDirection {
			FORWARD,
			BACKWARD,
			LEFT,
			RIGHT
		};
		struct SurfaceTargetArea {
			vk::Viewport viewport;
			vk::Rect2D scissor;
			void update(const vec2& position, const vec2& size, float minDepth = 0.f, float maxDepth = 1.f);
		};

		quat orientation;
		vec3 position, euler, worldOrientation;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		SurfaceTargetArea renderArea;

		Camera();
		void move(RelativeDirection direction, float velocity);
		void rotate(float xoffset, float yoffset);
		mat4 getPerspective();
		mat4 getView() const;
		vec3 worldFront() const;
		vec3 worldRight() const;
		vec3 worldUp() const;
		vec3 front() const;
		vec3 right() const;
		vec3 up() const;

		float frustum[6][4];
		void ExtractFrustum(mat4& model);
		bool SphereInFrustum(vec4& boundingSphere) const;
	};
}
