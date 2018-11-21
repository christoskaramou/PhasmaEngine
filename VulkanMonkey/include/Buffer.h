#pragma once
#include "VulkanContext.h"

namespace vm {
	struct Buffer
	{
		VulkanContext* vulkan;
		Buffer(VulkanContext* vulkan);

		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DeviceSize size = 0;
		void *data;

		void createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties);
		void copyBuffer(const vk::Buffer srcBuffer, const vk::DeviceSize size);
		void destroy();
	};
}