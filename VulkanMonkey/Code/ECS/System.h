#pragma once
#include "ECSBase.h"
#include <map>

namespace vm
{
	class World;

	template<class T>
	class System : public BaseSystem
	{
	public:
		System() : m_world(nullptr), m_id(BaseComponent::nextID()) { m_parent = nullptr;  m_enable = true; }
		virtual ~System() {}
		void SetWorld(World* world) { m_world = world; }
		size_t GetID() { return m_id; }
		static size_t GetTypeID();

		template<class U> void AddComponent(U* component);

	private:
		World* m_world;
		std::map<size_t, BaseComponent*> m_components;
		const size_t m_id;
		static const size_t s_typeID;
	};
}