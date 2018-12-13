#include "Camera.h"
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
	worldOrientation = vec3(1.f, -1.f, 1.f);

	// total pitch, yaw, roll
	euler = vec3(0.f, 0.f, 0.f);
	orientation = quat(euler);
	position = vec3(0.f, 0.f, 0.f);

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

void vm::Camera::update()
{
	updatePerspective();
	updateView();
	invView = inverse(view);
	invPerspective = inverse(perspective);
	invViewPerspective = invView * invPerspective;
}

void vm::Camera::updatePerspective()
{
	float aspect = renderArea.viewport.width / renderArea.viewport.height;
	float tanHalfFovy = tan(radians(FOV) * .5f);
	float m00 = 1.f / (aspect * tanHalfFovy);
	float m11 = 1.f / (tanHalfFovy);
	float m22 = farPlane / (farPlane - nearPlane) * worldOrientation.z;
	float m23 = worldOrientation.z;
	float m32 = -(farPlane * nearPlane) / (farPlane - nearPlane);
	perspective = mat4(
		m00, 0.f, 0.f, 0.f,
		0.f, m11, 0.f, 0.f,
		0.f, 0.f, m22, m23,
		0.f, 0.f, m32, 0.f
	);
}

void vm::Camera::updateView()
{
	vec3 f(front());
	vec3 r(right());
	vec3 u(up());

	float m30 = -dot(r, position);
	float m31 = -dot(u, position);
	float m32 = -dot(f, position);

	view = mat4(
		r.x, u.x, f.x, 0.f,
		r.y, u.y, f.y, 0.f,
		r.z, u.z, f.z, 0.f,
		m30, m31, m32, 1.f
	);
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
	euler.x += radians(-yoffset) * worldOrientation.y;	// pitch
	euler.y += radians(xoffset) * worldOrientation.x;	// yaw

	orientation = quat(euler);
}

mat4 Camera::getPerspective()
{
	return perspective;
}
mat4 Camera::getView()
{
	return view;
}

mat4 vm::Camera::getInvPerspective()
{
	return invPerspective;
}

mat4 vm::Camera::getInvView()
{
	return invView;
}

mat4 vm::Camera::getInvViewPerspective()
{
	return invViewPerspective;
}

vec3 Camera::worldRight() const
{
	return vec3(worldOrientation.x, 0.f, 0.f);
}

vec3 Camera::worldUp() const
{
	return vec3(0.f, worldOrientation.y, 0.f);
}

vec3 Camera::worldFront() const
{
	return vec3(0.f, 0.f, worldOrientation.z);
}

vec3 Camera::right() const
{
	return orientation * worldRight();
}

vec3 Camera::up() const
{
	return orientation * worldUp();
}

vec3 Camera::front() const
{
	return orientation * worldFront();
}

void Camera::ExtractFrustum(const mat4& model)
{
	mat4 pvm = transpose(perspective * view * model);

	/* Extract the numbers for the RIGHT plane */
	frustum[0] = pvm[3] - pvm[0];
	frustum[0] /= length(vec3(frustum[0]));

	/* Extract the numbers for the LEFT plane */
	frustum[1] = pvm[3] + pvm[0];
	frustum[1] /= length(vec3(frustum[1]));

	/* Extract the BOTTOM plane */
	frustum[2] = pvm[3] + pvm[1];
	frustum[2] /= length(vec3(frustum[2]));

	/* Extract the TOP plane */
	frustum[3] = pvm[3] - pvm[1];
	frustum[3] /= length(vec3(frustum[3]));

	/* Extract the FAR plane */
	frustum[4] = pvm[3] - pvm[2];
	frustum[4] /= length(vec3(frustum[4]));

	/* Extract the NEAR plane */
	frustum[5] = pvm[3] + pvm[2];
	frustum[5] /= length(vec3(frustum[5]));
}

// center x,y,z - radius w 
bool Camera::SphereInFrustum(const vec4& boundingSphere) const
{
	for (unsigned i = 0; i < 6; i++)
		if (dot(vec3(frustum[i]), vec3(boundingSphere)) + frustum[i].w <= -boundingSphere.w)
			return false;
	return true;
}

void Camera::SurfaceTargetArea::update(const vec2& position, const vec2& size, float minDepth, float maxDepth)
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
