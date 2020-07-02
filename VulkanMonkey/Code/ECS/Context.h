#pragma once
#include "GameObject.h"
#include "../Core/Base.h"

namespace vm
{
	class Context final
	{
	public:
		Context() {}
		~Context() {}

		void UpdateSystems(double delta);

		template<class T, class... Params> inline T* CreateSystem(Params&&... params);
		template<class T> inline T* GetSystem();
		template<class T> inline bool HasSystem();
		template<class T> inline void RemoveSystem();
		GameObject* CreateGameObject();
		GameObject* GetGameObject(size_t id);
		void RemoveGameObject(size_t id);

	private:
		std::map<size_t, Ref<BaseSystem>> m_systems;
		std::map<size_t, Ref<GameObject>> m_gameObjects;
	};

	template<class T, class... Params>
	inline T* Context::CreateSystem(Params&&... params)
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.find(GetTypeID<T>()) == m_systems.end())
			{
				m_systems[GetTypeID<T>()] = std::make_shared<T>(std::forward<Params>(params)...);

				T* pT = static_cast<T*>(m_systems[GetTypeID<T>()].get());
				pT->SetContext(this);

				return pT;
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			throw std::runtime_error("Context::CreateSystem: Type is not a System");
		}
	}

	template<class T>
	inline T* Context::GetSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.find(GetTypeID<T>()) != m_systems.end())
				return static_cast<T*>(m_systems[GetTypeID<T>()].get());
			else
				return nullptr;
		}
		else
		{
			throw std::runtime_error("Context::GetSystem: Type is not a System");
		}
	}

	template<class T>
	inline bool Context::HasSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.find(GetTypeID<T>()) != m_systems.end())
				return true;
			else
				return false;
		}
		else
		{
			throw std::runtime_error("Context::HasSystem: Type is not a System");
		}
	}

	template<class T>
	inline void Context::RemoveSystem()
	{
		if constexpr (std::is_base_of<System, T>::value)
		{
			if (m_systems.find(GetTypeID<T>()) != m_systems.end())
				m_systems.erase(GetTypeID<T>());
		}
		else
		{
			throw std::runtime_error("Context::RemoveSystem: Type is not a System");
		}
	}
}
