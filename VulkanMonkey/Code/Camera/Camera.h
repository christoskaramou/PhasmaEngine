#pragma once
#include "../ECS/Component.h"
#include "../ECS/System.h"
#include "../Core/Math.h"
#include "../Compute/Compute.h"

namespace vm {

	class Camera;

	class CameraSystem : public ISystem
	{
	public:
		CameraSystem() {}
		~CameraSystem() {}

		Camera& GetCamera(size_t index);

		// Inherited via ISystem
		void Init() override;
		void Update(double delta) override;
		void Destroy() override;
	};

	class Camera : public IComponent
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

		struct TargetArea
		{
			Viewport viewport;
			Rect2D scissor;
			void Update(const vec2& position, const vec2& size, float minDepth = 0.f, float maxDepth = 1.f);
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
        mat4 viewProjection;
        mat4 previousViewProjection;
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
        Compute frustumCompute;

		Camera();
		void Update();
		void UpdatePerspective();
		void UpdateView();
		vec3 WorldFront() const;
		vec3 WorldRight() const;
		vec3 WorldUp() const;
		void Move(RelativeDirection direction, float velocity);
		void Rotate(float xoffset, float yoffset);
		void ExtractFrustum();
		bool SphereInFrustum(const vec4& boundingSphere) const;
		void ReCreateComputePipelines();
	};
}
