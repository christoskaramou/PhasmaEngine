#pragma once
#include "../VulkanContext/VulkanContext.h"

namespace vm {
	struct Buffer
	{
		VulkanContext* vulkan = &VulkanContext::get();

		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DeviceSize size = 0;
		void *data = nullptr;

		void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
		void copyBuffer(vk::Buffer srcBuffer, vk::DeviceSize size);
		void destroy();
	};
}