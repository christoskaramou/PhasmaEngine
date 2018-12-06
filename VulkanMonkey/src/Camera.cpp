#include "../include/Camera.h"
#include <iostream>

using namespace vm;

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

	nearPlane = 500.0f;
	farPlane = 0.005f;
	FOV = 45.0f;
	speed = 0.35f;
	rotationSpeed = 0.05f;

	renderArea.viewport.x = GUI::winPos.x;
	renderArea.viewport.y = GUI::winPos.y;
	renderArea.viewport.width = GUI::winSize.x;
	renderArea.viewport.height = GUI::winSize.y;
	renderArea.viewport.minDepth = 0.f;
	renderArea.viewport.maxDepth = 1.f;
	renderArea.scissor.offset.x = (int32_t)GUI::winPos.x;
	renderArea.scissor.offset.y = (int32_t)GUI::winPos.y;
	renderArea.scissor.extent.width = (int32_t)GUI::winSize.x;
	renderArea.scissor.extent.height = (int32_t)GUI::winSize.y;
}

void Camera::move(RelativeDirection direction, float velocity)
{
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
	float aspect = renderArea.viewport.width / renderArea.viewport.height;
	float tanHalfFovy = tan(vm::radians(FOV) * .5f);
	float m00 = 1.f / (aspect * tanHalfFovy);
	float m11 = 1.f / (tanHalfFovy);
	float m22 = farPlane / (farPlane - nearPlane) * worldOrientation.z;
	float m23 = worldOrientation.z;
	float m32 = -(farPlane * nearPlane) / (farPlane - nearPlane);
	return vm::mat4(
		m00, 0.f, 0.f, 0.f,
		0.f, m11, 0.f, 0.f,
		0.f, 0.f, m22, m23,
		0.f, 0.f, m32, 0.f
	);
}
vm::mat4 Camera::getView() const
{
	vm::vec3 f(front());
	vm::vec3 r(right());
	vm::vec3 u(up());

	float m30 = -dot(r, position);
	float m31 = -dot(u, position);
	float m32 = -dot(f, position);

	return vm::mat4(
		r.x, u.x, f.x, 0.f,
		r.y, u.y, f.y, 0.f,
		r.z, u.z, f.z, 0.f,
		m30, m31, m32, 1.f
	);
}

vm::vec3 Camera::worldRight() const
{
	return vm::vec3(worldOrientation.x, 0.f, 0.f);
}

vm::vec3 Camera::worldUp() const
{
	return vm::vec3(0.f, worldOrientation.y, 0.f);
}

vm::vec3 Camera::worldFront() const
{
	return vm::vec3(0.f, 0.f, worldOrientation.z);
}

vm::vec3 Camera::right() const
{
	return orientation * worldRight();
}

vm::vec3 Camera::up() const
{
	return orientation * worldUp();
}

vm::vec3 Camera::front() const
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

// center x,y,z - radius w 
bool Camera::SphereInFrustum(vm::vec4& boundingSphere) const
{
	for (unsigned i = 0; i < 6; i++)
		if (frustum[i][0] * boundingSphere.x + frustum[i][1] * boundingSphere.y + frustum[i][2] * boundingSphere.z + frustum[i][3] <= -boundingSphere.w)
			return false;
	return true;
}

void vm::Camera::SurfaceTargetArea::update(const vm::vec2& position, const vm::vec2& size, float minDepth, float maxDepth)
{
	viewport.x = position.x;
	viewport.y = position.y;
	viewport.width = size.x;
	viewport.height = size.y;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

	scissor.offset.x = (int32_t)position.x;
	scissor.offset.y = (int32_t)position.y;
	scissor.extent.width = (int32_t)size.x;
	scissor.extent.height = (int32_t)size.y;
}
