/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
	struct MemoryRange
	{
		MemoryRange() : data(nullptr), size(0), offset(0) {}
		MemoryRange(void* data, size_t size, size_t offset) : data(data), size(size), offset(offset) {}

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
		
		template<typename T>
		MemoryHash(const T& object) : MemoryHash(&object, sizeof(T))
		{}
		
		size_t getHash()
		{
			return hash;
		}
	
	private:
		size_t hash;
	};
}