#pragma once
#include "Component.h"
#include <map>

namespace vm
{
	class World;

	class System : public BaseSystem
	{
	public:
		System() : m_world(nullptr), m_id(NextID())
		{
			m_parent = nullptr;
			m_enable = true;
		}
		virtual ~System() {}
		void SetWorld(World* world) { m_world = world; }
		size_t GetID() { return m_id; }

		template<class T> inline void AddComponent(T* component);

	private:
		World* m_world;
		std::map<size_t, BaseComponent*> m_components;
		const size_t m_id;
	};

	template<class T>
	inline void System::AddComponent(T* component)
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (!m_components.count(component->GetID()))
				m_components[component->GetID()] = static_cast<BaseComponent*>(component);
		}
		else
		{
			throw std::runtime_error("System::AddComponent: Type is not a Component");
		}
	}
}
