#pragma once

#include "../ECS/Entity.h"
#include "../ECS/System.h"
#include "../Core/Base.h"
#include <vector>

namespace vm
{
	class VulkanContext;

	class Context final
	{
	public:
		Context();

		~Context();

		void InitSystems();

		void UpdateSystems(double delta);

		template <class T, class... Params>
		inline T* CreateSystem(Params&& ... params);

		template <class T>
		inline T* GetSystem();

		template <class T>
		inline bool HasSystem();

		template <class T>
		inline void RemoveSystem();

		Entity* CreateEntity();

		Entity* GetEntity(size_t id);

		void RemoveEntity(size_t id);

		VulkanContext* GetVKContext();

	private:
		std::unordered_map<size_t, Ref<ISystem>> m_systems;
		std::unordered_map<size_t, Ref<Entity>> m_entities;
		Ref<VulkanContext> vulkanContext;
	};

	template <class T>
	inline bool Context::HasSystem()
	{
		ValidateBaseClass<ISystem, T>();

		if (m_systems.find(GetTypeID<T>()) != m_systems.end())
			return true;
		else
			return false;
	}

	template <class T, class... Params>
	inline T* Context::CreateSystem(Params&& ... params)
	{
		if (!HasSystem<T>())
		{
			size_t id = GetTypeID<T>();
			m_systems[id] = std::make_shared<T>(std::forward<Params>(params)...);
			GetSystem<T>()->SetContext(this);
			GetSystem<T>()->SetEnabled(true);

			return static_cast<T*>(m_systems[id].get());
		}
		else
		{
			return nullptr;
		}
	}

	template <class T>
	inline T* Context::GetSystem()
	{
		if (HasSystem<T>())
			return static_cast<T*>(m_systems[GetTypeID<T>()].get());
		else
			return nullptr;
	}

	template <class T>
	inline void Context::RemoveSystem()
	{
		if (HasSystem<T>())
			m_systems.erase(GetTypeID<T>());
	}
}
