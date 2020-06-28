#include "Component.h"

namespace vm
{
	Component::Component() : m_id(NextID()), m_gameObject(nullptr)
	{
		m_parent = nullptr;
		m_enable = true;
	}

	Component::Component(GameObject* gameObject, Transform* parent, bool enable) : m_id(NextID()), m_gameObject(gameObject)
	{
		m_parent = parent;
		m_enable = enable;
	}
}
