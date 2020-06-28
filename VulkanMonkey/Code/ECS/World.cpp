#include "ECSBase.h"
#include "World.h"
#include "System.h"
#include "GameObject.h"
#include <utility>

namespace vm
{
	GameObject* World::CreateGameObject()
	{
		GameObject* gameObject = new GameObject();
		gameObject->SetWorld(this);

		size_t key = gameObject->GetID();
		m_gameObjects[key] = std::shared_ptr<GameObject>(gameObject);

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
}
