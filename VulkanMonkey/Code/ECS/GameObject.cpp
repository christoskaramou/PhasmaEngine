#include "GameObject.h"
#include "Component.h"
#include "Wolrd.h"

namespace vm
{
	template<class T>
	bool GameObject::HasComponent()
	{
		if constexpr (std::is_base_of<Component<T>, T>::value)
		{
			if (m_components.count(T::GetTypeID()))
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
	T* GameObject::GetComponent()
	{
		if constexpr (std::is_base_of<Component<T>, T>::value)
		{
			if (m_components.count(T::GetTypeID()))
				return static_cast<T*>(m_components[T::GetTypeID()].get());
			else
				return nullptr;
		}
		else
		{
			throw std::runtime_error("GameObject::GetComponent: Type is not a Component");
		}
	}

	template<class T, class... Params>
	T* GameObject::AddComponent(Params&&... params)
	{
		if constexpr (std::is_base_of<Component<T>, T>::value)
		{
			if (!m_components.count(T::GetTypeID()))
			{
				T* pT = new T(std::forward<Params>(params)...);
				pT->SetGameObject(this);
				pT->SetParent(&m_transform);

				m_components[T::GetTypeID()] = std::unique_ptr<BaseComponent>(pT);

				return static_cast<T*>(m_components[T::GetTypeID()].get());
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
	void GameObject::RemoveComponent()
	{
		if constexpr (std::is_base_of<Component<T>, T>::value)
		{
			if (m_components.count(T::GetTypeID()))
				m_components.erase(T::GetTypeID());
		}
		else
		{
			throw std::runtime_error("GameObject::RemoveComponent: Type is not a Component");
		}
	}
}