#pragma once
#include "Vulkan.h"

struct Buffer
{
	vk::Buffer buffer;
	vk::DeviceMemory memory;
	vk::DeviceSize size = 0;
	void *data;

	void createBuffer(vk::Device device, vk::PhysicalDevice gpu, const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties);
	void copyBuffer(vk::Device device, vk::CommandPool commandPool, vk::Queue graphicsQueue, const vk::Buffer srcBuffer, const vk::DeviceSize size);
	void destroy(vk::Device device);
};