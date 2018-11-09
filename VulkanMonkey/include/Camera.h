#pragma once
#include "Math.h"

struct Camera
{
	enum struct RelativeDirection {
		FORWARD,
		BACKWARD,
		LEFT,
		RIGHT
	};
	vm::quat orientation;
	vm::vec3 position, euler;
	float aspect, nearPlane, farPlane, FOV, speed, rotationSpeed;

	Camera();
	vm::vec3 front();
	vm::vec3 right();
	vm::vec3 up();
	vm::mat4 getPerspective();
	vm::mat4 getView();
	void move(RelativeDirection direction, float deltaTime, bool combineDirections = false);
	void rotate(float xoffset, float yoffset);
};
