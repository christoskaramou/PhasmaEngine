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
			void update(const vm::vec2& position, const vm::vec2& size, float minDepth = 0.f, float maxDepth = 1.f);
		};

		vm::quat orientation;
		vm::vec3 position, euler, worldOrientation;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		SurfaceTargetArea renderArea;

		Camera();
		void move(RelativeDirection direction, float velocity);
		void rotate(float xoffset, float yoffset);
		vm::mat4 getPerspective();
		vm::mat4 getView() const;
		vm::vec3 worldFront() const;
		vm::vec3 worldRight() const;
		vm::vec3 worldUp() const;
		vm::vec3 front() const;
		vm::vec3 right() const;
		vm::vec3 up() const;

		float frustum[6][4];
		void ExtractFrustum(vm::mat4& model);
		bool SphereInFrustum(vm::vec4& boundingSphere) const;
	};
}
