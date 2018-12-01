#include "../include/Buffer.h"
#include "../include/Errors.h"
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
	VkCheck(vulkan->device.createBuffer(&bufferInfo, nullptr, &buffer));

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
		std::cout << "No suitable memory type found\n";
		exit(-1);
	}

	if (this->size < memRequirements.size)
		this->size = memRequirements.size;
	//allocate memory of buffer
	auto const allocInfo = vk::MemoryAllocateInfo()
		.setAllocationSize(this->size)
		.setMemoryTypeIndex(memTypeIndex);
	VkCheck(vulkan->device.allocateMemory(&allocInfo, nullptr, &memory));

	//binding memory with buffer
	VkCheck(vulkan->device.bindBufferMemory(buffer, memory, 0));
	std::cout << "Buffer and memory created and binded\n";
}

void Buffer::copyBuffer(const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
	vk::CommandBuffer copyCmd;

	auto const cbai = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(vulkan->commandPool)
		.setCommandBufferCount(1);
	VkCheck(vulkan->device.allocateCommandBuffers(&cbai, &copyCmd));

	auto const cbbi = vk::CommandBufferBeginInfo();
	VkCheck(copyCmd.begin(&cbbi));

	vk::BufferCopy bufferCopy{};
	bufferCopy.size = size;

	copyCmd.copyBuffer(srcBuffer, buffer, 1, &bufferCopy);

	VkCheck(copyCmd.end());

	auto const si = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&copyCmd);
	VkCheck(vulkan->graphicsQueue.submit(1, &si, nullptr));

	VkCheck(vulkan->graphicsQueue.waitIdle());

	vulkan->device.freeCommandBuffers(vulkan->commandPool, 1, &copyCmd);
}

void Buffer::destroy()
{
	if (buffer)
		vulkan->device.destroyBuffer(buffer);
	if (memory)
		vulkan->device.freeMemory(memory);
	buffer = nullptr;
	memory = nullptr;
	std::cout << "Buffer and memory destroyed\n";
}