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
	float aspect = 16.f / 9.f,
		nearPlane = 0.005f,
		farPlane = 50.0f,
		FOV = 45.0f,
		yaw,
		pitch,
		speed,
		rotationSpeed;

	Camera();
	vm::mat4 getPerspective();
	vm::mat4 getView();
	void move(RelativeDirection direction, float deltaTime, bool combineDirections = false);
	void rotate(float xoffset, float yoffset);
};
