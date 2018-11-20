#pragma once
#include "Math.h"

namespace vm {
	struct Camera
	{
		enum struct RelativeDirection {
			FORWARD,
			BACKWARD,
			LEFT,
			RIGHT
		};
		vm::quat orientation;
		vm::vec3 position, euler, worldOrientation;
		float aspect, nearPlane, farPlane, FOV, speed, rotationSpeed;

		Camera();
		void move(RelativeDirection direction, float deltaTime, bool combineDirections = false);
		void rotate(float xoffset, float yoffset);
		vm::mat4 getPerspective();
		vm::mat4 getView();
		vm::vec3 worldFront();
		vm::vec3 worldRight();
		vm::vec3 worldUp();
		vm::vec3 front();
		vm::vec3 right();
		vm::vec3 up();

		float frustum[6][4];
		void ExtractFrustum(vm::mat4& model);
		bool SphereInFrustum(vm::vec4& boundingSphere) const;
	};
}
