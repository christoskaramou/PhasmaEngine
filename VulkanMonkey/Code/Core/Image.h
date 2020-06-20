#pragma once
#include "Base.h"

namespace vk
{
	class Image;
	class DeviceMemory;
	class ImageView;
	class Sampler;
	struct Extent2D;
	enum class Format;
	enum class ImageLayout;
	enum class ImageTiling;
	enum class Filter;
	enum class ImageViewType;
	enum class SamplerAddressMode;
	enum class BorderColor;
	enum class CompareOp;
	enum class SamplerMipmapMode;
	struct PipelineColorBlendAttachmentState;
	class CommandBuffer;
	enum class SampleCountFlagBits;
	class Buffer;

	template<class T1, class T2> class Flags;
	enum class ImageCreateFlagBits;
	enum class PipelineStageFlagBits;
	enum class AccessFlagBits;
	enum class ImageAspectFlagBits;
	enum class ImageUsageFlagBits;
	enum class MemoryPropertyFlagBits;
	using ImageCreateFlags = Flags<ImageCreateFlagBits, uint32_t>;
	using PipelineStageFlags = Flags<PipelineStageFlagBits, uint32_t>;
	using AccessFlags = Flags<AccessFlagBits, uint32_t>;
	using ImageAspectFlags = Flags<ImageAspectFlagBits, uint32_t>;
	using ImageUsageFlags = Flags<ImageUsageFlagBits, uint32_t>;
	using MemoryPropertyFlags = Flags<MemoryPropertyFlagBits, uint32_t>;
	using Bool32 = uint32_t;
}

namespace vm
{
	enum class LayoutState
	{
		ColorRead,
		ColorWrite,
		DepthRead,
		DepthWrite
	};
	class Image
	{
	public:
		Image();
		~Image();
		Ref_t<vk::Image> image;
		Ref_t<vk::DeviceMemory> memory;
		Ref_t<vk::ImageView> view;
		Ref_t<vk::Sampler> sampler;
		Ref_t<uint32_t> width;
		Ref_t<uint32_t> height;
		Ref_t<float> width_f;
		Ref_t<float> height_f;
		Ref_t<vk::Extent2D> extent;

		// values
		Ref_t<vk::SampleCountFlagBits> samples;
		Ref_t<LayoutState> layoutState;
		Ref_t<vk::Format> format;
		Ref_t<vk::ImageLayout> initialLayout;
		Ref_t<vk::ImageTiling> tiling;
		Ref_t<uint32_t> mipLevels;
		Ref_t<uint32_t> arrayLayers;
		Ref_t<bool> anisotropyEnabled;
		Ref_t<float> minLod, maxLod, maxAnisotropy;
		Ref_t<vk::Filter> filter;
		Ref_t<vk::ImageCreateFlags> imageCreateFlags;
		Ref_t<vk::ImageViewType> viewType;
		Ref_t<vk::SamplerAddressMode> addressMode;
		Ref_t<vk::BorderColor> borderColor;
		Ref_t<vk::Bool32> samplerCompareEnable;
		Ref_t<vk::CompareOp> compareOp;
		Ref_t<vk::SamplerMipmapMode> samplerMipmapMode;
		Ref_t<vk::PipelineColorBlendAttachmentState> blentAttachment;

		void transitionImageLayout(
			vk::CommandBuffer cmd,
			vk::ImageLayout oldLayout,
			vk::ImageLayout newLayout,
			const vk::PipelineStageFlags& oldStageMask,
			const vk::PipelineStageFlags& newStageMask,
			const vk::AccessFlags& srcMask,
			const vk::AccessFlags& dstMask,
			const vk::ImageAspectFlags& aspectFlags) const;
		void createImage(uint32_t width, uint32_t height, vk::ImageTiling tiling, const vk::ImageUsageFlags& usage, const vk::MemoryPropertyFlags& properties);
		void createImageView(const vk::ImageAspectFlags& aspectFlags);
		void transitionImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout) const;
		void changeLayout(const vk::CommandBuffer& cmd, LayoutState state) const;
		void copyBufferToImage(vk::Buffer buffer, uint32_t baseLayer = 0) const;
		void copyColorAttachment(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		void generateMipMaps() const;
		void createSampler();
		void destroy();
	};
}