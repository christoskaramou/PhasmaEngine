#pragma once
#include "../VulkanContext/VulkanContext.h"

namespace vm {
	struct Buffer
	{
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DeviceSize size = 0;
		void *data = nullptr;

		void createBuffer(vk::DeviceSize size, const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& properties);
		void copyBuffer(vk::Buffer srcBuffer, vk::DeviceSize size) const;
		void destroy();
	};
}