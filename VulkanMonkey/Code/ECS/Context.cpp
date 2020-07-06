#include "Context.h"
#include "ECSBase.h"
#include "Entity.h"
#include <stdexcept>
#include <iostream>

namespace vm
{
	void Context::InitSystems()
	{
		for (auto& system : m_systems)
		{
			if (system.second->IsEnabled())
				system.second->Init();
		}
	}

	void Context::UpdateSystems(double delta)
	{
		for (auto& system : m_systems)
		{
			if (system.second->IsEnabled())
				system.second->Update(delta);
		}
	}

	Entity* Context::CreateEntity()
	{
		Entity* entity = new Entity();
		entity->SetContext(this);
		entity->SetEnabled(true);

		size_t key = entity->GetID();
		m_entities[key] = std::shared_ptr<Entity>(entity);

		return m_entities[key].get();
	}

	Entity* Context::GetEntity(size_t id)
	{
		if (m_entities.find(id) != m_entities.end())
			return m_entities[id].get();
		else
			return nullptr;
	}

	void Context::RemoveEntity(size_t id)
	{
		if (m_entities.find(id) != m_entities.end())
			m_entities.erase(id);
	}
}
