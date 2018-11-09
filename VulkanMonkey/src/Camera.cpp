#include "../include/Camera.h"
#include <iostream>

Camera::Camera()
{
	orientation = vm::quat::identity();
	position = vm::vec3(0.f, 0.f, 0.f);
	euler = vm::vec3(0.f, 0.f, 0.f);

	aspect = 16.f / 9.f;
	nearPlane = 0.005f;
	farPlane = 50.0f;
	FOV = 45.0f;
	speed = 0.35f;
	rotationSpeed = 0.05f;
}

vm::vec3 Camera::front()
{
	return orientation * vm::vec3(0.f, 0.f, 1.f);
}

vm::vec3 Camera::right()
{
	return orientation * vm::vec3(1.f, 0.f, 0.f);
}

vm::vec3 Camera::up()
{
	return orientation * vm::vec3(.0f, -1.f, 0.f);
}

void Camera::move(RelativeDirection direction, float deltaTime, bool combineDirections)
{
	float velocity = speed * deltaTime;
	if (combineDirections) velocity *= 0.707f;
	if (direction == RelativeDirection::FORWARD)	position += front() * velocity;
	if (direction == RelativeDirection::BACKWARD)	position -= front() * velocity;
	if (direction == RelativeDirection::RIGHT)		position += right() * velocity;
	if (direction == RelativeDirection::LEFT)		position -= right() * velocity;
}

void Camera::rotate(float xoffset, float yoffset)
{
	xoffset *= rotationSpeed;
	yoffset *= rotationSpeed;

	euler.x += vm::radians(yoffset); // pitch
	euler.y += vm::radians(xoffset); // yaw

	orientation = vm::quat(euler);
}

vm::mat4 Camera::getPerspective()
{
	static const vm::mat4 perspective = vm::perspective(vm::radians(FOV), aspect, nearPlane, farPlane);
	return perspective;
}
vm::mat4 Camera::getView()
{
	const vm::vec3 f(front());
	const vm::vec3 r(right());
	const vm::vec3 u(up());

	const float m30 = -dot(r, position);
	const float m31 = -dot(u, position);
	const float m32 = -dot(f, position);

	return vm::mat4(
		r.x, u.x, f.x, 0.f,
		r.y, u.y, f.y, 0.f,
		r.z, u.z, f.z, 0.f,
		m30, m31, m32, 1.f
	);
}