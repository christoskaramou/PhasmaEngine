#pragma once
#include "ECSBase.h"

namespace vm
{
	class GameObject;

	template<class T>
	class Component : public BaseComponent
	{
	public:
		virtual ~Component() {}
		size_t GetID() { return m_id; }
		static size_t GetTypeID();
		GameObject* GetGameObject() { return m_gameObject; }
		void SetGameObject(GameObject* gameObject) { m_gameObject = gameObject; }

	protected:
		friend class GameObject;
		Component() : m_id(BaseObject::nextID()), m_gameObject(nullptr) { m_parent = nullptr; m_enable = true; }
		Component(GameObject* gameObject, Transform* parent = nullptr, bool enable = true) : m_id(BaseObject::nextID()), m_gameObject(gameObject) { m_parent = parent; m_enable = enable; }

	private:
		GameObject* m_gameObject;
		const size_t m_id;
		static const size_t s_typeID;
	};
}