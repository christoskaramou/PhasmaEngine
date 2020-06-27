#include "World.h"
#include "GameObject.h"

namespace vm
{
	template<class T, class... Params>
	T* World::AddSystem(Params&&... params)
	{
		if constexpr (std::is_base_of<System<T>, T>::value)
		{
			if (!m_systems.count(T::GetTypeID()))
			{
				m_systems[T::GetTypeID()] = std::make_unique<T>(std::forward<Params>(params)...);

				T* pT = static_cast<T*>(m_systems[T::GetTypeID()].get());
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
	T* World::GetSystem()
	{
		if constexpr (std::is_base_of<System<T>, T>::value)
		{
			if (m_systems.count(T::GetTypeID()))
				return static_cast<T*>(m_systems[T::GetTypeID()].get());
			else
				return nullptr;
		}
		else
		{
			throw std::runtime_error("World::GetSystem: Type is not a System");
		}
	}

	template<class T>
	bool World::HasSystem()
	{
		if constexpr (std::is_base_of<System<T>, T>::value)
		{
			if (m_systems.count(T::GetTypeID()))
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
	void World::RemoveSystem()
	{
		if constexpr (std::is_base_of<System<T>, T>::value)
		{
			if (m_systems.count(T::GetTypeID()))
				m_systems.erase(T::GetTypeID());
		}
		else
		{
			throw std::runtime_error("World::RemoveSystem: Type is not a System");
		}
	}

	GameObject* World::CreateGameObject()
	{
		GameObject* gameObject = new GameObject();
		gameObject->SetWorld(this);

		size_t key = gameObject->GetID();
		m_gameObjects[key] = std::unique_ptr<GameObject>(gameObject);

		return m_gameObjects[key].get();
	}

	GameObject* World::GetGameObject(size_t id)
	{
		if (m_gameObjects.count(id))
			return m_gameObjects[id].get();

		return nullptr;
	}

	void World::RemoveGameObject(size_t id)
	{
		if (m_gameObjects.count(id))
			m_gameObjects.erase(id);
	}

	void World::Update()
	{
		for (auto& system : m_systems)
		{
			//if (system.second->IsEnabled())
			//	system.second->Update();
		}
	}
}