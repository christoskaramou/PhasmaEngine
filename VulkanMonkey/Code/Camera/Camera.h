#pragma once
#include "../Math/Math.h"  // for vec3, mat4, vec2, vec4, quat
#include "../../include/Vulkan.h"              // for Rect2D, Viewport

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

		mat4 view;
		mat4 projection;
		mat4 invView;
		mat4 invProjection;
		mat4 invViewProjection;
		quat orientation;
		vec3 position, euler, worldOrientation;
		vec3 front, right, up;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		SurfaceTargetArea renderArea;
		vec4 frustum[6];

		Camera();
		void update();
		void updatePerspective();
		void updateView();
		vec3 worldFront() const;
		vec3 worldRight() const;
		vec3 worldUp() const;
		void move(RelativeDirection direction, float velocity);
		void rotate(float xoffset, float yoffset);
		void ExtractFrustum(const mat4& model);
		bool SphereInFrustum(const vec4& boundingSphere) const;
	};
}
