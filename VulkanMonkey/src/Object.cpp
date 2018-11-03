#include "../include/Object.h"
#include "../include/Errors.h"

void Object::createVertexBuffer(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue)
{
	vertexBuffer.createBuffer(device, gpu, sizeof(float)*vertices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(device, gpu, sizeof(float)*vertices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	VkCheck(device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, vertices.data(), sizeof(float)*vertices.size());
	device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(device, commandPool, graphicsQueue, staging.buffer, staging.size);
	staging.destroy(device);
}

void Object::createUniformBuffer(vk::Device device, vk::PhysicalDevice gpu)
{
	uniformBuffer.createBuffer(device, gpu, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Object::loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::string path)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture: " << path << "\n";
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(device, gpu, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.mipLevels = 1;
	texture.createImage(device, gpu, texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	texture.copyBufferToImage(device, commandPool, graphicsQueue, staging.buffer, 0, 0, texWidth, texHeight);
	texture.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	texture.createImageView(device, vk::ImageAspectFlagBits::eColor);
	texture.createSampler(device);

	device.destroyBuffer(staging.buffer);
	device.freeMemory(staging.memory);
}

void Object::createDescriptorSet(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout& descriptorSetLayout)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VkCheck(device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


	vk::WriteDescriptorSet textureWriteSets[2];
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	        // DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(uniformBuffer.size));									// DeviceSize range;
	// texture sampler
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;
	device.updateDescriptorSets(2, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void Object::destroy(vk::Device device)
{
	texture.destroy(device);
	vertexBuffer.destroy(device);
	indexBuffer.destroy(device);
	uniformBuffer.destroy(device);
	vertices.clear();
	vertices.shrink_to_fit();
}