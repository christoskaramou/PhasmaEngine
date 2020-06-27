#include "Component.h"

namespace vm
{
	template<class T>
	const size_t Component<T>::s_typeID = BaseObject::nextTypeID();

	template<class T>
	size_t Component<T>::GetTypeID() { return Component<T>::s_typeID; }
}