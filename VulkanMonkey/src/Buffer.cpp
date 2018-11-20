#include "../include/Buffer.h"
#include "../include/Errors.h"

using namespace vm;

void Buffer::createBuffer(vk::Device device, vk::PhysicalDevice gpu, const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties)
{
	this->size = size;
	//create buffer (GPU buffer)
	auto const bufferInfo = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive); // used by graphics queue only
	VkCheck(device.createBuffer(&bufferInfo, nullptr, &buffer));

	uint32_t memTypeIndex = UINT32_MAX; ///////////////////
	auto const memRequirements = device.getBufferMemoryRequirements(buffer);
	auto const memProperties = gpu.getMemoryProperties();
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
	VkCheck(device.allocateMemory(&allocInfo, nullptr, &memory));

	//binding memory with buffer
	VkCheck(device.bindBufferMemory(buffer, memory, 0));
	std::cout << "Buffer and memory created and binded\n";
}

void Buffer::copyBuffer(vk::Device device, vk::CommandPool commandPool, vk::Queue graphicsQueue, const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
	vk::CommandBuffer copyCmd;

	auto const cbai = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(commandPool)
		.setCommandBufferCount(1);
	VkCheck(device.allocateCommandBuffers(&cbai, &copyCmd));

	auto const cbbi = vk::CommandBufferBeginInfo();
	VkCheck(copyCmd.begin(&cbbi));

	vk::BufferCopy bufferCopy{};
	bufferCopy.size = size;

	copyCmd.copyBuffer(srcBuffer, buffer, 1, &bufferCopy);

	VkCheck(copyCmd.end());

	auto const si = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&copyCmd);
	VkCheck(graphicsQueue.submit(1, &si, nullptr));

	VkCheck(graphicsQueue.waitIdle());

	device.freeCommandBuffers(commandPool, 1, &copyCmd);
}

void Buffer::destroy(vk::Device device)
{
	if (buffer)
		device.destroyBuffer(buffer);
	if (memory)
		device.freeMemory(memory);
	buffer = nullptr;
	memory = nullptr;
	std::cout << "Buffer and memory destroyed\n";
}