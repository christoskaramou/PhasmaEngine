#include "../include/Camera.h"
#include <iostream>

Camera::Camera()
{
	// normalized device coordinates (ndc) of the API and left handed corrdinate system comparison

	//	LEFT HANDED		     	VULKAN NDC	
	//	+y /|  /|				|  /|				
	//	    | /  +z				| /  +z				
	//	    |/					|/					
	//  --------|-------->			--------|-------->			
	//         /|	    +x			       /|	+x			
	//	  / |				      /	|					
	//	 /  |   			     /  |/ +y
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

void Camera::ExtractFrustum(vm::mat4& model)
{
	vm::mat4 pvm = getPerspective() * getView() * model;
	const float* clip = pvm.ptr();
	float t;

	/* Extract the numbers for the RIGHT plane */
	frustum[0][0] = clip[3] - clip[0];
	frustum[0][1] = clip[7] - clip[4];
	frustum[0][2] = clip[11] - clip[8];
	frustum[0][3] = clip[15] - clip[12];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[0][0] * frustum[0][0] + frustum[0][1] * frustum[0][1] + frustum[0][2] * frustum[0][2]);
	frustum[0][0] *= t;
	frustum[0][1] *= t;
	frustum[0][2] *= t;
	frustum[0][3] *= t;
	/* Extract the numbers for the LEFT plane */
	frustum[1][0] = clip[3] + clip[0];
	frustum[1][1] = clip[7] + clip[4];
	frustum[1][2] = clip[11] + clip[8];
	frustum[1][3] = clip[15] + clip[12];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[1][0] * frustum[1][0] + frustum[1][1] * frustum[1][1] + frustum[1][2] * frustum[1][2]);
	frustum[1][0] *= t;
	frustum[1][1] *= t;
	frustum[1][2] *= t;
	frustum[1][3] *= t;
	/* Extract the BOTTOM plane */
	frustum[2][0] = clip[3] + clip[1];
	frustum[2][1] = clip[7] + clip[5];
	frustum[2][2] = clip[11] + clip[9];
	frustum[2][3] = clip[15] + clip[13];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[2][0] * frustum[2][0] + frustum[2][1] * frustum[2][1] + frustum[2][2] * frustum[2][2]);
	frustum[2][0] *= t;
	frustum[2][1] *= t;
	frustum[2][2] *= t;
	frustum[2][3] *= t;
	/* Extract the TOP plane */
	frustum[3][0] = clip[3] - clip[1];
	frustum[3][1] = clip[7] - clip[5];
	frustum[3][2] = clip[11] - clip[9];
	frustum[3][3] = clip[15] - clip[13];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[3][0] * frustum[3][0] + frustum[3][1] * frustum[3][1] + frustum[3][2] * frustum[3][2]);
	frustum[3][0] *= t;
	frustum[3][1] *= t;
	frustum[3][2] *= t;
	frustum[3][3] *= t;
	/* Extract the FAR plane */
	frustum[4][0] = clip[3] - clip[2];
	frustum[4][1] = clip[7] - clip[6];
	frustum[4][2] = clip[11] - clip[10];
	frustum[4][3] = clip[15] - clip[14];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[4][0] * frustum[4][0] + frustum[4][1] * frustum[4][1] + frustum[4][2] * frustum[4][2]);
	frustum[4][0] *= t;
	frustum[4][1] *= t;
	frustum[4][2] *= t;
	frustum[4][3] *= t;
	/* Extract the NEAR plane */
	frustum[5][0] = clip[3] + clip[2];
	frustum[5][1] = clip[7] + clip[6];
	frustum[5][2] = clip[11] + clip[10];
	frustum[5][3] = clip[15] + clip[14];
	/* Normalize the result */
	t = 1.f / sqrt(frustum[5][0] * frustum[5][0] + frustum[5][1] * frustum[5][1] + frustum[5][2] * frustum[5][2]);
	frustum[5][0] *= t;
	frustum[5][1] *= t;
	frustum[5][2] *= t;
	frustum[5][3] *= t;
}

bool Camera::SphereInFrustum(vm::vec4& boundingSphere) const
{
	for (unsigned i = 0; i < 6; i++)
		if (frustum[i][0] * boundingSphere.x + frustum[i][1] * boundingSphere.y + frustum[i][2] * boundingSphere.z + frustum[i][3] <= -boundingSphere.w)
			return false;
	return true;
}
