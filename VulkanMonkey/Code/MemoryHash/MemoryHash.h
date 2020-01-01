#pragma once

#include <functional>

template<typename T>
struct MemoryHash
{
	using Type = size_t;
	MemoryHash(const T& object)
	{
		Type* array = reinterpret_cast<Type*>(const_cast<T*>(&object));
		size_t arraySize = sizeof(T) / sizeof(Type);
		size_t bytesToFit = sizeof(T) % sizeof(Type);

		hash = 0;
		for (size_t i = 0; i < arraySize; i++)
		{
			hash ^= std::hash<Type>()(array[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		if (bytesToFit)
		{
			Type lastBytes{};
			memcpy(&lastBytes, &array[arraySize], bytesToFit);

			hash ^= std::hash<Type>()(lastBytes) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
	}
	size_t getHash()
	{
		return hash;
	}
private:
	size_t hash;
};