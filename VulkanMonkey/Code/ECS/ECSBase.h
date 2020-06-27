#pragma once
#include "../Core/Math.h"

namespace vm
{
	class BaseBehaviour
	{
	public:
		virtual ~BaseBehaviour() {}

	protected:
		virtual void Awake() {}
		virtual void Start() {}
		virtual void Update() {}
		virtual void FixedUpdate() {}
		virtual void LateUpdate() {}
		virtual void OnGUI() {}
		virtual void OnEnable() {}
		virtual void OnDisable() {}
	};

	class BaseObject
	{
	public:
		virtual ~BaseObject() {}

		static size_t nextID();
		static size_t nextTypeID();
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
	};
}