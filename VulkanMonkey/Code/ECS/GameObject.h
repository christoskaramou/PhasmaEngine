#pragma once
#include "Component.h"
#include <memory>
#include <map>

namespace vm
{
	class Context;

	class GameObject final : public BaseObject
	{
	public:
		~GameObject() {}
		size_t GetID() { return m_id; }
		Context* GetContext() { return m_context; }
		void SetContext(Context* context) { m_context = context; }

		template<class T> inline bool HasComponent();
		template<class T> inline T* GetComponent();
		template<class T, class... Params> inline T* CreateComponent(Params&&... params);
		template<class T> inline void RemoveComponent();

	private:
		friend class Context;
		GameObject() : m_context(nullptr), m_id(NextID()) { m_parent = nullptr;  m_enable = true; }

		size_t m_id;
		Context* m_context;
		std::map<size_t, std::shared_ptr<BaseComponent>> m_components;
	};

	template<class T>
	inline bool GameObject::HasComponent()
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.find(GetTypeID<T>()) != m_components.end())
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
			if (m_components.find(GetTypeID<T>()) != m_components.end())
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
	inline T* GameObject::CreateComponent(Params&&... params)
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.find(GetTypeID<T>()) == m_components.end())
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
			throw std::runtime_error("GameObject::CreateComponent: Type is not a Component");
		}
	}

	template<class T>
	inline void GameObject::RemoveComponent()
	{
		if constexpr (std::is_base_of<Component, T>::value)
		{
			if (m_components.find(GetTypeID<T>()) != m_components.end())
				m_components.erase(GetTypeID<T>());
		}
		else
		{
			throw std::runtime_error("GameObject::RemoveComponent: Type is not a Component");
		}
	}
}
