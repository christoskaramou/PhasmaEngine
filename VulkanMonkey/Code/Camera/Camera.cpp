#include "Camera.h"
#include "../GUI/GUI.h"                                   // for GUI, GUI::winPos, GUI::winSize

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
	position = vec3(0.f, 0.f, -3.f);

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
	front = orientation * worldFront();
	right = orientation * worldRight();
	up = orientation * worldUp();
	previousView = view;
	updatePerspective();
	updateView();
	invView = inverse(view);
	invProjection = inverse(projection);
	invViewProjection = invView * invProjection;
	ExtractFrustum();
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
	projection = mat4(
		m00, 0.f, 0.f, 0.f,
		0.f, m11, 0.f, 0.f,
		0.f, 0.f, m22, m23,
		0.f, 0.f, m32, 0.f
	);
}

void vm::Camera::updateView()
{
	vec3 &f = front;
	vec3 &r = right;
	vec3 &u = up;

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
	if (direction == RelativeDirection::FORWARD)	position += front * (velocity * worldOrientation.z);
	if (direction == RelativeDirection::BACKWARD)	position -= front * (velocity * worldOrientation.z);
	if (direction == RelativeDirection::RIGHT)		position += right * velocity;
	if (direction == RelativeDirection::LEFT)		position -= right * velocity;
}

void Camera::rotate(float xoffset, float yoffset)
{
	yoffset *= rotationSpeed;
	xoffset *= rotationSpeed;
	euler.x += radians(-yoffset) * worldOrientation.y;	// pitch
	euler.y += radians(xoffset) * worldOrientation.x;	// yaw

	orientation = quat(euler);
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

void Camera::ExtractFrustum()
{
	// transpose just to make the calculations look simpler
	mat4 pvm = transpose(projection * view);
	vec4 temp;

	/* Extract the numbers for the RIGHT plane */
	temp = pvm[3] - pvm[0];
	temp /= length(vec3(temp));

	frustum[0].normal = vec3(temp);
	frustum[0].d = temp.w;

	/* Extract the numbers for the LEFT plane */
	temp = pvm[3] + pvm[0];
	temp /= length(vec3(temp));

	frustum[1].normal = vec3(temp);
	frustum[1].d = temp.w;

	/* Extract the BOTTOM plane */
	temp = pvm[3] + pvm[1];
	temp /= length(vec3(temp));

	frustum[2].normal = vec3(temp);
	frustum[2].d = temp.w;

	/* Extract the TOP plane */
	temp = pvm[3] - pvm[1];
	temp /= length(vec3(temp));

	frustum[3].normal = vec3(temp);
	frustum[3].d = temp.w;

	/* Extract the FAR plane */
	temp = pvm[3] - pvm[2];
	temp /= length(vec3(temp));

	frustum[4].normal = vec3(temp);
	frustum[4].d = temp.w;

	/* Extract the NEAR plane */
	temp = pvm[3] + pvm[2];
	temp /= length(vec3(temp));

	frustum[5].normal = vec3(temp);
	frustum[5].d = temp.w;
}

// center x,y,z - radius w 
bool Camera::SphereInFrustum(const vec4& boundingSphere) const
{
	for (unsigned i = 0; i < 6; i++) {
		float dist = dot(frustum[i].normal, vec3(boundingSphere)) + frustum[i].d;

		if (dist < -boundingSphere.w)
			return false;

		if (fabs(dist) < boundingSphere.w)
			return true;
	}
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
