#include "ECSBase.h"

namespace vm
{
	size_t BaseObject::nextID() { static size_t ID = 0; return ID++; };
	size_t BaseObject::nextTypeID() { static size_t typeID = 0; return typeID++; };
}