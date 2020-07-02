#pragma once
#include "../Core/Math.h"

namespace vm
{

	inline size_t NextID() { static size_t ID = 0; return ID++; }

	template<class T>
	inline size_t GetTypeID() { static size_t typeID = NextID(); return typeID; }

	class BaseBehaviour
	{
	public:
		virtual ~BaseBehaviour() {}

		virtual void Init() {}
		virtual void Update(double delta) {}
		virtual void FixedUpdate() {}
		virtual void OnGUI() {}
		virtual void OnEnable() {}
		virtual void OnDisable() {}
		virtual void Destroy() {}
	};

	class BaseObject
	{
	public:
		BaseObject() : m_parent(nullptr), m_enable(true) {}
		virtual ~BaseObject() {}

		Transform& GetTransform() { return m_transform; }
		void SetTransform(const Transform& transform) { m_transform = transform; }
		Transform* GetParent() { return m_parent; }
		void SetParent(Transform* parent) { m_parent = parent; }
		bool IsEnabled() { return m_enable; }
		void Enable() { m_enable = true; }
		void Disable() { m_enable = false; }

	protected:
		Transform m_transform;
		Transform* m_parent;
		bool m_enable;
	};

	class BaseComponent : public BaseObject, public BaseBehaviour
	{
	public:
		virtual ~BaseComponent() {}
	};

	class BaseSystem : public BaseObject, public BaseBehaviour
	{
	public:
		virtual ~BaseSystem() {}
		virtual void Init() override = 0;
		virtual void Update(double delta) override = 0;
		virtual void Destroy() override = 0;
	};
}
