#include "Context.h"
#include "ECSBase.h"
#include "System.h"
#include "GameObject.h"
#include <utility>

namespace vm
{
	GameObject* Context::CreateGameObject()
	{
		GameObject* gameObject = new GameObject();
		gameObject->SetContext(this);

		size_t key = gameObject->GetID();
		m_gameObjects[key] = std::shared_ptr<GameObject>(gameObject);

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
