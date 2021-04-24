#pragma once

namespace vm
{

	struct MemoryRange
	{
		void* data; // source data
		size_t size; // source data size in bytes
		size_t offset; // offset to destination data in bytes
	};

	class MemoryHash
	{
	public:
		using Type = size_t;

		bool operator==(MemoryHash memoryHash)
		{
			return hash == memoryHash.hash;
		}

		MemoryHash(const void* data, size_t size)
		{
			Type* array = reinterpret_cast<Type*>(const_cast<void*>(data));
			size_t arraySize = size / sizeof(Type);
			size_t bytesToFit = size % sizeof(Type);

			hash = 0;
			for (size_t i = 0; i < arraySize; i++)
			{
				hash ^= std::hash<Type>()(array[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			}
			if (bytesToFit)
			{
				Type lastBytes;
				memset(&lastBytes, 0, sizeof(Type));
				memcpy(&lastBytes, &array[arraySize], bytesToFit);

				hash ^= std::hash<Type>()(lastBytes) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			}
		}

		template <typename T>
		MemoryHash(const T& object) : MemoryHash(&object, sizeof(T))
		{ }

		size_t getHash()
		{
			return hash;
		}

	private:
		size_t hash;
	};
}