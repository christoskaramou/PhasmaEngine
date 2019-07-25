#pragma once
#include "../VulkanContext/VulkanContext.h"

namespace vm {
	enum struct LayoutState {
		ColorRead,
		ColorWrite,
		DepthRead,
		DepthWrite
	};
	struct VulkanContext;
	struct Image
	{
		const VulkanContext* vulkan = &VulkanContext::get();

		vk::Image image;
		vk::DeviceMemory memory;
		vk::ImageView view;
		vk::Sampler sampler;
		std::string name;
		uint32_t width;
		uint32_t height;
		float width_f;
		float height_f;
		vk::Extent2D extent;

		// values
		LayoutState layoutState = LayoutState::ColorWrite;
		vk::Format format{};
		vk::ImageLayout initialLayout = vk::ImageLayout::ePreinitialized;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
		bool anisotropyEnabled = VK_TRUE;
		float minLod = 0.f, maxLod = 1.f, maxAnisotropy = 16.f;
		vk::Filter filter = vk::Filter::eLinear;
		vk::ImageCreateFlags imageCreateFlags = vk::ImageCreateFlags();
		vk::ImageViewType viewType = vk::ImageViewType::e2D;
		vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
		vk::BorderColor borderColor = vk::BorderColor::eFloatOpaqueBlack;
		vk::Bool32 samplerCompareEnable = VK_FALSE;
		vk::CompareOp compareOp = vk::CompareOp::eLess;
		vk::SamplerMipmapMode samplerMipmapMode = vk::SamplerMipmapMode::eLinear;
		vk::PipelineColorBlendAttachmentState blentAttachment;

		void transitionImageLayout(
			vk::CommandBuffer cmd,
			vk::ImageLayout oldLayout,
			vk::ImageLayout newLayout,
			vk::PipelineStageFlags oldStageMask,
			vk::PipelineStageFlags newStageMask,
			vk::AccessFlags srcMask,
			vk::AccessFlags dstMask,
			vk::ImageAspectFlags aspectFlags);
		void createImage(uint32_t width, uint32_t height, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
		void createImageView(vk::ImageAspectFlags aspectFlags);
		void transitionImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
		void copyBufferToImage(vk::Buffer buffer, uint32_t baseLayer = 0);
		void generateMipMaps();
		void createSampler();
		void destroy();
	};
}