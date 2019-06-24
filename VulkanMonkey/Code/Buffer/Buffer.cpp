#include "Buffer.h"

using namespace vm;

void Buffer::createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties)
{
	this->size = size;

	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	buffer = vulkan->device.createBuffer(bufferInfo);

	uint32_t memTypeIndex = UINT32_MAX;
	auto const memRequirements = vulkan->device.getBufferMemoryRequirements(buffer);
	auto const memProperties = vulkan->gpu.getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
		if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
		throw std::runtime_error("Could not create buffer, memTypeIndex not valid");

	if (this->size < memRequirements.size)
		this->size = memRequirements.size;
	//allocate memory of buffer
	vk::MemoryAllocateInfo allocInfo;
	allocInfo.allocationSize = this->size;
	allocInfo.memoryTypeIndex = memTypeIndex;
	memory = vulkan->device.allocateMemory(allocInfo);

	//binding memory with buffer
	vulkan->device.bindBufferMemory(buffer, memory, 0);
}

void Buffer::copyBuffer(const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
	vk::CommandBufferAllocateInfo cbai;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandPool = vulkan->commandPool2;
	cbai.commandBufferCount = 1;
	vk::CommandBuffer copyCmd = vulkan->device.allocateCommandBuffers(cbai).at(0);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	copyCmd.begin(beginInfo);

	vk::BufferCopy bufferCopy{};
	bufferCopy.size = size;

	copyCmd.copyBuffer(srcBuffer, buffer, bufferCopy);

	copyCmd.end();

	vulkan->submitAndWaitFence(copyCmd, nullptr, nullptr, nullptr);

	vulkan->device.freeCommandBuffers(vulkan->commandPool2, copyCmd);
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