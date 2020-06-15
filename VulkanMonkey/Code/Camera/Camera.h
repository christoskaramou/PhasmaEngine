#pragma once
#include "../Core/Math.h"

namespace vm {
	class Camera
	{
	public:
		enum class RelativeDirection {
			FORWARD,
			BACKWARD,
			LEFT,
			RIGHT
		};

		struct Viewport
		{
			float x;
			float y;
			float width;
			float height;
			float minDepth;
			float maxDepth;
		};

		struct Rect2D
		{
			int32_t x;
			int32_t y;
			uint32_t width;
			uint32_t height;
		};

		struct TargetArea
		{
			Viewport viewport;
			Rect2D scissor;
			void update(const vec2& position, const vec2& size, float minDepth = 0.f, float maxDepth = 1.f);
		};

		struct Plane
		{
			vec3 normal;
			float d;
		};

		mat4 view;
		mat4 previousView;
		mat4 projection;
		mat4 previousProjection;
		mat4 invView;
		mat4 invProjection;
		mat4 invViewProjection;
		quat orientation;
		vec3 position, euler, worldOrientation;
		vec3 front, right, up;
		float nearPlane, farPlane, FOV, speed, rotationSpeed;
		vec2 projOffset, projOffsetPrevious;
		TargetArea renderArea;
		std::vector<Plane> frustum{};

		Camera();
		void update();
		void updatePerspective();
		void updateView();
		vec3 worldFront() const;
		vec3 worldRight() const;
		vec3 worldUp() const;
		void move(RelativeDirection direction, float velocity);
		void rotate(float xoffset, float yoffset);
		void ExtractFrustum();
		bool SphereInFrustum(const vec4& boundingSphere) const;
	};
}
