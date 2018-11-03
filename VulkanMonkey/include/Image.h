#pragma once
#include "Vulkan.h"

struct Image
{
	vk::Image image;
	vk::DeviceMemory memory;
	vk::ImageView view;
	vk::Sampler sampler;
	std::string name;

	// values
	vk::Format format;
	vk::ImageLayout initialLayout = vk::ImageLayout::ePreinitialized;
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
	float minLod = 0.f, maxLod = 1.f, maxAnisotropy = 16.f;
	vk::ImageCreateFlags imageCreateFlags = vk::ImageCreateFlags();
	vk::ImageViewType viewType = vk::ImageViewType::e2D;
	vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
	vk::BorderColor borderColor = vk::BorderColor::eFloatOpaqueBlack;
	vk::Bool32 samplerCompareEnable = VK_FALSE;

	void createImage(vk::Device device, vk::PhysicalDevice gpu, const uint32_t width, const uint32_t height, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
	void createImageView(vk::Device device, const vk::ImageAspectFlags aspectFlags);
	void transitionImageLayout(vk::Device device, vk::CommandPool commandPool, vk::Queue graphicsQueue, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout);
	void copyBufferToImage(vk::Device device, vk::CommandPool commandPool, vk::Queue graphicsQueue, const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer = 0);
	void generateMipMaps(vk::Device device, vk::CommandPool commandPool, vk::Queue graphicsQueue, const int32_t texWidth, const int32_t texHeight);
	void createSampler(vk::Device device);
	void destroy(vk::Device device);
};