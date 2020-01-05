#pragma once
#include "../VulkanContext/VulkanContext.h"

namespace vm {
	struct Buffer
	{
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DeviceSize size = 0;
		std::string name;
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