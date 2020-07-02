#include "Context.h"
#include "ECSBase.h"
#include "System.h"
#include "GameObject.h"
#include <utility>

namespace vm
{
	void Context::UpdateSystems(double delta)
	{
		for (auto& system : m_systems)
		{
			system.second->Update(delta);
		}
	}

	GameObject* Context::CreateGameObject()
	{
		GameObject gameObject;
		gameObject.SetContext(this);

		size_t key = gameObject.GetID();
		m_gameObjects[key] = std::make_shared<GameObject>(gameObject);

		return m_gameObjects[key].get();
	}

	GameObject* Context::GetGameObject(size_t id)
	{
		if (m_gameObjects.count(id))
			return m_gameObjects[id].get();

		return nullptr;
	}

	void Context::RemoveGameObject(size_t id)
	{
		if (m_gameObjects.count(id))
			m_gameObjects.erase(id);
	}
}
