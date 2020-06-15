#include "Camera.h"
#include "../GUI/GUI.h"

namespace vm
{
	Camera::Camera()
	{
		// gltf is right handed, reversing the x orientation makes the models left handed
		// reversing the y axis to match the vulkan y axis too
		worldOrientation = vec3(-1.f, -1.f, 1.f);

		// total pitch, yaw, roll
		euler = vec3(0.f, radians(180.f), 0.f);
		orientation = quat(euler);
		position = vec3(0.f, 0.f, 0.f);

		nearPlane = 500.0f;
		farPlane = 0.005f;
		FOV = 45.0f;
		speed = 0.35f;
		rotationSpeed = 0.05f;

		frustum.resize(6);

		renderArea.update(vec2(GUI::winPos.x, GUI::winPos.y), vec2(GUI::winSize.x, GUI::winSize.y));

	}

	void vm::Camera::update()
	{
		renderArea.update(vec2(GUI::winPos.x, GUI::winPos.y), vec2(GUI::winSize.x, GUI::winSize.y));
		front = orientation * worldFront();
		right = orientation * worldRight();
		up = orientation * worldUp();
		previousView = view;
		previousProjection = projection;
		projOffsetPrevious = projOffset;
		if (GUI::use_TAA) {
			// has the aspect ratio of the render area because the projection matrix has the same aspect ratio too,
			// doesn't matter if it renders in bigger image size,
			// it will be scaled down to render area size before GUI pass
			//const int i = static_cast<int>(floor(rand(0.0f, 15.99f)));
			//projOffset = vec2(&halton16[i * 2]);
			projOffset = halton_2_3_next(16);
			projOffset *= vec2(2.0f);
			projOffset -= vec2(1.0f);
			projOffset /= vec2(renderArea.viewport.width, renderArea.viewport.height);
			projOffset *= GUI::renderTargetsScale;
			projOffset *= GUI::TAA_jitter_scale;
		}
		else {
			projOffset = { 0.0f, 0.0f };
		}
		updatePerspective();
		updateView();
		invView = inverse(view);
		invProjection = inverse(projection);
		invViewProjection = invView * invProjection;
		ExtractFrustum();
	}

	void vm::Camera::updatePerspective()
	{
		const float aspect = renderArea.viewport.width / renderArea.viewport.height;
		const float tanHalfFovy = tan(radians(FOV) * .5f);

		const float m00 = 1.f / (aspect * tanHalfFovy);
		const float m11 = 1.f / (tanHalfFovy);
		const float m22 = farPlane / (farPlane - nearPlane) * worldOrientation.z;
		const float m23 = worldOrientation.z;
		const float m32 = -(farPlane * nearPlane) / (farPlane - nearPlane);
		const float m20 = projOffset.x;
		const float m21 = projOffset.y;

		projection = mat4(
			m00, 0.f, 0.f, 0.f,
			0.f, m11, 0.f, 0.f,
			m20, m21, m22, m23,
			0.f, 0.f, m32, 0.f
		);
	}

	void vm::Camera::updateView()
	{
		const vec3& r = right;
		const vec3& u = up;
		const vec3& f = front;

		const float m30 = -dot(r, position);
		const float m31 = -dot(u, position);
		const float m32 = -dot(f, position);

		view = mat4(
			r.x, u.x, f.x, 0.f,
			r.y, u.y, f.y, 0.f,
			r.z, u.z, f.z, 0.f,
			m30, m31, m32, 1.f
		);
	}

	void Camera::move(RelativeDirection direction, float velocity)
	{
		// prediction of where the submit happens
		//const float prediction = (Timer::cleanDelta - Timer::waitingTime) / Timer::cleanDelta;
		//velocity -= velocity * prediction;

		if (direction == RelativeDirection::FORWARD)	position += front * (velocity * worldOrientation.z);
		if (direction == RelativeDirection::BACKWARD)	position -= front * (velocity * worldOrientation.z);
		if (direction == RelativeDirection::RIGHT)		position += right * velocity;
		if (direction == RelativeDirection::LEFT)		position -= right * velocity;
	}

	void Camera::rotate(float xoffset, float yoffset)
	{
		// prediction of where the submit happens
		//const float prediction = (Timer::cleanDelta - Timer::waitingTime) / Timer::cleanDelta;
		const float x = radians(-yoffset * rotationSpeed) * worldOrientation.y;	// pitch
		const float y = radians(xoffset * rotationSpeed) * worldOrientation.x;	// yaw

		euler.x += x;
		euler.y += y;

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

		/* Extract the numbers for the RIGHT plane */
		vec4 temp = pvm[3] - pvm[0];
		temp /= length(vec3(temp));

		frustum[0].normal = vec3(temp);
		frustum[0].d = temp.w;

		/* Extract the numbers for the LEFT plane */
		temp = pvm[3] + pvm[0];
		temp /= length(vec3(temp));

		frustum[1].normal = vec3(temp);
		frustum[1].d = temp.w;

		/* Extract the BOTTOM plane */
		temp = pvm[3] - pvm[1];
		temp /= length(vec3(temp));

		frustum[2].normal = vec3(temp);
		frustum[2].d = temp.w;

		/* Extract the TOP plane */
		temp = pvm[3] + pvm[1];
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
		for (auto& plane : frustum) {
			const float dist = dot(plane.normal, vec3(boundingSphere)) + plane.d;

			if (dist < -boundingSphere.w)
				return false;

			if (fabs(dist) < boundingSphere.w)
				return true;
		}
		return true;
	}

	void Camera::TargetArea::update(const vec2& position, const vec2& size, float minDepth, float maxDepth)
	{
		viewport.x = position.x;
		viewport.y = position.y;
		viewport.width = size.x;
		viewport.height = size.y;
		viewport.minDepth = minDepth;
		viewport.maxDepth = maxDepth;

		scissor.x = static_cast<int32_t>(position.x);
		scissor.y = static_cast<int32_t>(position.y);
		scissor.width = static_cast<uint32_t>(size.x);
		scissor.height = static_cast<uint32_t>(size.y);
	}
}
