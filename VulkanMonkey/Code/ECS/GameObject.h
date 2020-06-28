#pragma once
#include "Component.h"
#include <memory>
#include <map>

namespace vm
{
	class World;

	class GameObject final : public BaseObject
	{
	public:
		~GameObject() {}
		size_t GetID() { return m_id; }
		World* GetWorld() { return m_world; }
		void SetWorld(World* world) { m_world = world; }

		template<class T> inline bool HasComponent();
		template<class T> inline T* GetComponent();
		template<class T, class... Params> inline T* AddComponent(Params&&... params);
		template<class T> inline void RemoveComponent();

	private:
		friend class World;
		GameObject() : m_world(nullptr), m_id(NextID()) { m_parent = nullptr;  m_enable = true; }

		const size_t m_id;
		World* m_world;
		std::map<size_t, std::shared_ptr<BaseComponent>> m_components;
	};

	template<class T>
	inline bool GameObject::HasComponent()
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.count(GetTypeID<T>()))
				return true;
			else
				return false;
		}
		else
		{
			throw std::runtime_error("GameObject::HasComponent: Type is not a Component");
		}
	}

	template<class T>
	inline T* GameObject::GetComponent()
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.count(GetTypeID<T>()))
				return static_cast<T*>(m_components[GetTypeID<T>()].get());
			else
				return nullptr;
		}
		else
		{
			throw std::runtime_error("GameObject::GetComponent: Type is not a Component");
		}
	}

	template<class T, class... Params>
	inline T* GameObject::AddComponent(Params&&... params)
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (!m_components.count(GetTypeID<T>()))
			{
				T* pT = new T(std::forward<Params>(params)...);
				pT->SetGameObject(this);
				pT->SetParent(&m_transform);

				m_components[GetTypeID<T>()] = std::shared_ptr<BaseComponent>(pT);

				return static_cast<T*>(m_components[GetTypeID<T>()].get());
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			throw std::runtime_error("GameObject::AddComponent: Type is not a Component");
		}
	}

	template<class T>
	inline void GameObject::RemoveComponent()
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.count(GetTypeID<T>()))
				m_components.erase(GetTypeID<T>());
		}
		else
		{
			throw std::runtime_error("GameObject::RemoveComponent: Type is not a Component");
		}
	}
}
