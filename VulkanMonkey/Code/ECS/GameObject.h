#pragma once
#include "ECSBase.h"
#include <memory>
#include <map>

namespace vm
{
	class World;

	class GameObject : public BaseObject
	{
	public:
		~GameObject() {}
		size_t GetID() { return m_id; }
		World* GetWorld() { return m_world; }
		void SetWorld(World* world) { m_world = world; }

		template<class T> bool HasComponent();
		template<class T> T* GetComponent();
		template<class T, class... Params> T* AddComponent(Params&&... params);
		template<class T> void RemoveComponent();

	private:
		friend class World;
		GameObject() : m_world(nullptr), m_id(BaseObject::nextID()) { m_parent = nullptr;  m_enable = true; }

		const size_t m_id;
		World* m_world;
		std::map<size_t, std::unique_ptr<BaseComponent>> m_components;
	};
}