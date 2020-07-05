#pragma once
#include <unordered_map>
#include <vector>
#include "ECSBase.h"

namespace vm
{
	class Context;
	class IComponent;

	class ISystem
	{
	public:
		ISystem() : m_context(nullptr), m_enabled(false) { m_components[-1] = std::vector<IComponent*>(); }
		virtual ~ISystem() {}

		virtual void Init() = 0;
		virtual void Update(double delta) = 0;
		virtual void Destroy() = 0;
		template<class T> inline bool HasComponents()
		{
			ValidateBaseClass<IComponent, T>();

			if (m_components.find(GetTypeID<T>()) != m_components.end())
				return true;
			else
				return false;
		}
		template<class T> inline void AddComponent(T* component)
		{
			size_t id = GetTypeID<T>();
			if (!HasComponents<T>())
				m_components[id] = std::vector<IComponent*>();
			m_components[id].push_back(component);
		}
		template<class T> void RemoveComponent(IComponent* component)
		{
			if (HasComponents<T>())
			{
				size_t id = GetTypeID<T>();
				auto it = std::find(m_components[id].begin(), m_components[id].end(), component);
				if (it != m_components[id].end())
					m_components[id].erase(it);
			}
		}
		template<class T> void RemoveComponents()
		{
			if (HasComponents<T>())
				m_components[GetTypeID<T>()].clear();
		}
		template<class T> void RemoveAllComponents()
		{
			m_components.clear();
		}
		template<class T> std::vector<IComponent*>& GetComponentsOfType()
		{
			if (HasComponents<T>())
				return m_components[GetTypeID<T>()];
			else
				return m_components[-1];
		}
		void SetContext(Context* context) { m_context = context; }
		Context* GetContext() { return m_context; }
		bool IsEnabled() { return m_enabled; }
		void SetEnabled(bool enabled) { m_enabled = enabled; }

	private:
		Context* m_context;
		std::unordered_map<size_t, std::vector<IComponent*>> m_components;
		bool m_enabled;
	};
}
