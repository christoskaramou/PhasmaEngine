#pragma once
#include "Math.h"
#include "GUI.h"
#include "Mesh.h"
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

		mat4 view;
		mat4 perspective;
		quat orientation;
		vec3 position, euler, worldOrientation;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		SurfaceTargetArea renderArea;

		Camera();
		void update();
		void updatePerspective();
		void updateView();
		void move(RelativeDirection direction, float velocity);
		void rotate(float xoffset, float yoffset);
		mat4 getPerspective();
		mat4 getView();
		vec3 worldFront() const;
		vec3 worldRight() const;
		vec3 worldUp() const;
		vec3 front() const;
		vec3 right() const;
		vec3 up() const;

		vec4 frustum[6];
		void ExtractFrustum(const mat4& model);
		bool SphereInFrustum(const vec4& boundingSphere) const;
	};
}
