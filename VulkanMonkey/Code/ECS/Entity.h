#pragma once
#include <unordered_map>
#include "Component.h"
#include "../Core/Base.h"

namespace vm
{
	class Context;

	class Entity
	{
	public:
		Entity() : m_context(nullptr), m_id(NextID()), m_enabled(false) {}
		virtual ~Entity() {}

		size_t GetID() const { return m_id; }
		Context* GetContext() { return m_context; }
		void SetContext(Context* context) { m_context = context; }
		bool IsEnabled() { return m_enabled; }
		void SetEnabled(bool enabled) { m_enabled = enabled; }

		template<class T> inline bool HasComponent();
		template<class T> inline T* GetComponent();
		template<class T, class... Params> inline T* CreateComponent(Params&&... params);
		template<class T> inline void RemoveComponent();

	private:
		size_t m_id;
		Context* m_context;
		std::unordered_map<size_t, Ref<IComponent>> m_components;
		bool m_enabled;
	};

	template<class T>
	inline bool Entity::HasComponent()
	{
		ValidateBaseClass<IComponent, T>();

		if (m_components.find(GetTypeID<T>()) != m_components.end())
			return true;
		else
			return false;
	}

	template<class T>
	inline T* Entity::GetComponent()
	{
		if (HasComponent<T>())
			return static_cast<T*>(m_components[GetTypeID<T>()].get());
		else
			return nullptr;

	}

	template<class T, class... Params>
	inline T* Entity::CreateComponent(Params&&... params)
	{
		if (!HasComponent<T>())
		{
			size_t id = GetTypeID<T>();
			m_components[id] = std::shared_ptr<T>(std::forward<Params>(params)...);
			GetComponent<T>()->SetEntity(this);			
			GetComponent<T>()->SetEnabled(true);			

			return static_cast<T*>(m_components[id].get());
		}
		else
		{
			return nullptr;
		}
	}

	template<class T>
	inline void Entity::RemoveComponent()
	{
		if (HasComponent<T>())
			m_components.erase(GetTypeID<T>());
	}
}
