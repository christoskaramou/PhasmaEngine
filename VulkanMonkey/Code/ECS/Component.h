#pragma once
#include "ECSBase.h"

namespace vm
{
	class GameObject;

	class Component : public BaseComponent
	{
	public:
		virtual ~Component() {}
		size_t GetID() { return m_id; }
		GameObject* GetGameObject() { return m_gameObject; }
		void SetGameObject(GameObject* gameObject) { m_gameObject = gameObject; }

	protected:
		friend class GameObject;
		Component();
		Component(GameObject* gameObject, Transform* parent = nullptr, bool enable = true);

	private:
		GameObject* m_gameObject;
		const size_t m_id;
	};
}
