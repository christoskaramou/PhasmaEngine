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
	vm::vec3 position, front, up, right, worldUp;
	float aspect, nearPlane, farPlane, FOV, yaw, pitch, speed, rotationSpeed;

	Camera();
	vm::mat4 getPerspective();
	vm::mat4 getView();
	void move(RelativeDirection direction, float deltaTime, bool combineDirections = false);
	void rotate(float xoffset, float yoffset);
};
