#include "../include/Camera.h"
#include <iostream>

Camera::Camera()
{
	// normalized device coordinates (ndc) of the API and left handed corrdinate system comparison

	//		LEFT HANDED					VULKAN NDC	
	//		+y /|  /|						|  /|				
	//			| /  +z						| /  +z				
	//			|/							|/					
	//  --------|-------->			--------|-------->			
	//		   /|		+x				   /|		+x			
	//		  /	|						  /	|					
	//	     /  |   				     /  |/ +y
	worldOrientation = vm::vec3(1.f, -1.f, 1.f);

	// total pitch, yaw, roll
	euler = vm::vec3(0.f, 0.f, 0.f);
	orientation = vm::quat(euler);
	position = vm::vec3(0.f, 0.f, 0.f);

	aspect = 16.f / 9.f;
	nearPlane = 0.005f;
	farPlane = 50.0f;
	FOV = 45.0f;
	speed = 0.35f;
	rotationSpeed = 0.05f;
}

void Camera::move(RelativeDirection direction, float deltaTime, bool combineDirections)
{
	float velocity = combineDirections ? 0.707f * speed * deltaTime : speed * deltaTime;
	if (direction == RelativeDirection::FORWARD)	position += front() * (velocity * worldOrientation.z);
	if (direction == RelativeDirection::BACKWARD)	position -= front() * (velocity * worldOrientation.z);
	if (direction == RelativeDirection::RIGHT)		position += right() * velocity;
	if (direction == RelativeDirection::LEFT)		position -= right() * velocity;
}

void Camera::rotate(float xoffset, float yoffset)
{
	yoffset *= rotationSpeed;
	xoffset *= rotationSpeed;
	euler.x += vm::radians(-yoffset) * worldOrientation.y;	// pitch
	euler.y += vm::radians(xoffset) * worldOrientation.x;	// yaw

	orientation = vm::quat(euler);
}

vm::mat4 Camera::getPerspective()
{
	cfloat tanHalfFovy = tan(vm::radians(FOV) * .5f);
	cfloat m00 = 1.f / (aspect * tanHalfFovy);
	cfloat m11 = 1.f / (tanHalfFovy);
	cfloat m22 = farPlane / (farPlane - nearPlane) * worldOrientation.z;
	cfloat m23 = worldOrientation.z;
	cfloat m32 = -(farPlane * nearPlane) / (farPlane - nearPlane);
	return vm::mat4(
		m00, 0.f, 0.f, 0.f,
		0.f, m11, 0.f, 0.f,
		0.f, 0.f, m22, m23,
		0.f, 0.f, m32, 0.f
	);
}
vm::mat4 Camera::getView()
{
	const vm::vec3 f(front());
	const vm::vec3 r(right());
	const vm::vec3 u(up());

	cfloat m30 = -dot(r, position);
	cfloat m31 = -dot(u, position);
	cfloat m32 = -dot(f, position);

	return vm::mat4(
		r.x, u.x, f.x, 0.f,
		r.y, u.y, f.y, 0.f,
		r.z, u.z, f.z, 0.f,
		m30, m31, m32, 1.f
	);
}

vm::vec3 Camera::worldRight()
{
	return vm::vec3(worldOrientation.x, 0.f, 0.f);
}

vm::vec3 Camera::worldUp()
{
	return vm::vec3(0.f, worldOrientation.y, 0.f);
}

vm::vec3 Camera::worldFront()
{
	return vm::vec3(0.f, 0.f, worldOrientation.z);
}

vm::vec3 Camera::right()
{
	return orientation * worldRight();
}

vm::vec3 Camera::up()
{
	return orientation * worldUp();
}

vm::vec3 Camera::front()
{
	return orientation * worldFront();
}
