#include "Buffer.h"
#include <iostream>

using namespace vm;

void Buffer::createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties)
{
	this->size = size;
	//create buffer (GPU buffer)
	auto const bufferInfo = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive); // used by graphics queue only
	buffer = vulkan->device.createBuffer(bufferInfo);

	uint32_t memTypeIndex = UINT32_MAX; ///////////////////
	auto const memRequirements = vulkan->device.getBufferMemoryRequirements(buffer);
	auto const memProperties = vulkan->gpu.getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
		if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
	{
		exit(-1);
	}

	if (this->size < memRequirements.size)
		this->size = memRequirements.size;
	//allocate memory of buffer
	auto const allocInfo = vk::MemoryAllocateInfo()
		.setAllocationSize(this->size)
		.setMemoryTypeIndex(memTypeIndex);
	memory = vulkan->device.allocateMemory(allocInfo);

	//binding memory with buffer
	vulkan->device.bindBufferMemory(buffer, memory, 0);
}

void Buffer::copyBuffer(const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
	auto const cbai = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(vulkan->commandPool)
		.setCommandBufferCount(1);
	vk::CommandBuffer copyCmd = vulkan->device.allocateCommandBuffers(cbai)[0];

	copyCmd.begin(vk::CommandBufferBeginInfo());

	vk::BufferCopy bufferCopy{};
	bufferCopy.size = size;

	copyCmd.copyBuffer(srcBuffer, buffer, 1, &bufferCopy);

	copyCmd.end();

	auto const si = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&copyCmd);
	vulkan->graphicsQueue.submit(si, nullptr);

	vulkan->graphicsQueue.waitIdle();

	vulkan->device.freeCommandBuffers(vulkan->commandPool, copyCmd);
}

void Buffer::destroy()
{
	if (buffer)
		vulkan->device.destroyBuffer(buffer);
	if (memory)
		vulkan->device.freeMemory(memory);
	buffer = nullptr;
	memory = nullptr;
}