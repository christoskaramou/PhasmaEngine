#pragma once
#include "Base.h"

namespace vk
{
	class Buffer;
	class DeviceMemory;

	template<class T1, class T2> class Flags;
	enum class BufferUsageFlagBits;
	enum class MemoryPropertyFlagBits;
	using BufferUsageFlags = Flags<BufferUsageFlagBits, uint32_t>;
	using MemoryPropertyFlags = Flags<MemoryPropertyFlagBits, uint32_t>;
}

namespace vm 
{
	class Buffer
	{
	public:
		Buffer();
		Ref<vk::Buffer> buffer;
		Ref<vk::DeviceMemory> memory;
		size_t size;
		void *data = nullptr;

		void createBuffer(size_t size, const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& properties);
		void map(size_t offset = 0);
		void unmap();
		void zero();
		void copyData(const void* srcData, size_t srcSize = 0, size_t offset = 0);
		void copyBuffer(vk::Buffer srcBuffer, size_t size) const;
		void flush(size_t size = 0);
		void destroy();
	};
}