#pragma once
#include "GameObject.h"

namespace vm
{
	class World final
	{
	public:
		World() {}
		~World() {}

		template<class T, class... Params> inline T* AddSystem(Params&&... params);
		template<class T> inline T* GetSystem();
		template<class T> inline bool HasSystem();
		template<class T> inline void RemoveSystem();
		GameObject* CreateGameObject();
		GameObject* GetGameObject(size_t id);
		void RemoveGameObject(size_t id);

	private:
		std::map<size_t, std::shared_ptr<BaseSystem>> m_systems;
		std::map<size_t, std::shared_ptr<GameObject>> m_gameObjects;
	};

	template<class T, class... Params>
	inline T* World::AddSystem(Params&&... params)
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (!m_systems.count(GetTypeID<T>()))
			{
				m_systems[GetTypeID<T>()] = std::make_shared<T>(std::forward<Params>(params)...);

				T* pT = static_cast<T*>(m_systems[GetTypeID<T>()].get());
				pT->SetWorld(this);

				return pT;
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			throw std::runtime_error("World::AddSystem: Type is not a System");
		}
	}

	template<class T>
	inline T* World::GetSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.count(GetTypeID<T>()))
				return static_cast<T*>(m_systems[GetTypeID<T>()].get());
			else
				return nullptr;
		}
		else
		{
			throw std::runtime_error("World::GetSystem: Type is not a System");
		}
	}

	template<class T>
	inline bool World::HasSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.count(GetTypeID<T>()))
				return true;
			else
				return false;
		}
		else
		{
			throw std::runtime_error("World::HasSystem: Type is not a System");
		}
	}

	template<class T>
	inline void World::RemoveSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.count(GetTypeID<T>()))
				m_systems.erase(GetTypeID<T>());
		}
		else
		{
			throw std::runtime_error("World::RemoveSystem: Type is not a System");
		}
	}
}
