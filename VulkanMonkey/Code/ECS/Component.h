#pragma once
#include "ECSBase.h"

namespace vm
{
	class Entity;

	class IComponent
	{
	public:
		IComponent() : m_entity(nullptr), m_enabled(false) {}
		virtual ~IComponent() {}

		Entity* GetEntity() { return m_entity; }
		void SetEntity(Entity* entity) { m_entity = entity; }
		bool IsEnabled() { return m_enabled; }
		void SetEnabled(bool enabled) { m_enabled = enabled; }

	private:
		Entity* m_entity;
		bool m_enabled;
	};
}
