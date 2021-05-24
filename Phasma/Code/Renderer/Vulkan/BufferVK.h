#pragma once

#include "../../Core/Base.h"

namespace vk
{
	class Buffer;
	
	template<class T1>
	class Flags;
	
	enum class BufferUsageFlagBits : uint32_t;
	enum class MemoryPropertyFlagBits : uint32_t;
	using BufferUsageFlags = Flags<BufferUsageFlagBits>;
	using MemoryPropertyFlags = Flags<MemoryPropertyFlagBits>;
}

namespace pe
{
	class BufferVK
	{
	public:
		BufferVK();
		
		Ref<vk::Buffer> buffer;
		VmaAllocation allocation;
		size_t size{};
		// sizeRequested is the size asked from user during the creation, afterwards the size can be
		// changed dynamically based on the system's minimum alignment on the specific buffer type
		// and it will always be less or equal than the actual size of the created buffer
		size_t sizeRequested {};
		void* data = nullptr;
		
		void CreateBuffer(size_t size, BufferUsageFlags usage, MemoryPropertyFlags properties);
		
		void Map(size_t mapSize = 0, size_t offset = 0);
		
		void Unmap();
		
		void Zero() const;
		
		void CopyData(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		
		void CopyBuffer(vk::Buffer srcBuffer, size_t srcSize = 0) const;
		
		void Flush(size_t offset = 0, size_t flushSize = 0) const;
		
		void Destroy() const;
	};
}