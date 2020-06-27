#include "System.h"
#include "Component.h"

namespace vm
{
	template<class T>
	const size_t System<T>::s_typeID = BaseObject::nextTypeID();

	template<class T>
	size_t System<T>::GetTypeID() { return System<T>::s_typeID; }

	template<class U>
	void AddComponent(U* component)
	{
		if constexpr (std::is_base_of<Component<U>, U>::value)
		{
			if (!m_components.count(component->GetID()))
				m_components[component->GetID()] = static_cast<BaseComponent*>(component);
		}
		else
		{
			throw std::runtime_error("System::AddComponent: Type is not a Component");
		}
	}
}