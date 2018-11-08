#include "../include/Camera.h"
#include <iostream>

Camera::Camera()
{
	aspect = 16.f / 9.f;
	nearPlane = 0.005f;
	farPlane = 50.0f;
	FOV = 45.0f;
	worldUp = vm::vec3(0.0f, -1.0f, 0.0f);
	yaw = 90.f;
	pitch = 0.0f;
	speed = 0.35f;
	rotationSpeed = 0.05f;
	rotate(0.f, 0.f);
}

void Camera::move(RelativeDirection direction, float deltaTime, bool combineDirections)
{
	float velocity = speed * deltaTime;
	if (combineDirections) velocity *= 0.707f;

	if (direction == RelativeDirection::FORWARD)
		position += front * velocity;
	if (direction == RelativeDirection::BACKWARD)
		position -= front * velocity;
	if (direction == RelativeDirection::RIGHT)
		position += right * velocity;
	if (direction == RelativeDirection::LEFT)
		position -= right * velocity;
}

void Camera::rotate(float xoffset, float yoffset)
{
	xoffset *= rotationSpeed;
	yoffset *= rotationSpeed;

	yaw += xoffset;
	pitch -= yoffset;

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	// update the vectors
	front = vm::normalize(
		vm::vec3(
			cos(vm::radians(pitch)) * cos(vm::radians(yaw)),
			sin(vm::radians(pitch)),
			cos(vm::radians(pitch)) * sin(vm::radians(yaw))));
	right = vm::normalize(vm::cross(worldUp, front));
	//std::cout << "position: " << position.x << ", " << position.y << ", " << position.z << "\n";
	//std::cout << "front: " << front.x << ", " << front.y << ", " << front.z << "\n";
	//std::cout << "right: " << right.x << ", " << right.y << ", " << right.z << "\n";
	//std::cout << "worldUp: " << worldUp.x << ", " << worldUp.y << ", " << worldUp.z << "\n\n";
}

vm::mat4 Camera::getPerspective()
{
	static const vm::mat4 perspective = vm::perspective(vm::radians(FOV), aspect, nearPlane, farPlane);
	return perspective;
}
vm::mat4 Camera::getView()
{
	return vm::lookAt(position, position + front, worldUp);
}