/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Renderer/Pipeline.h"

namespace pe
{
    class Context;
    class CommandBuffer;
    class Buffer;
    class Image;
    struct ImageBlit;

    class SamplerCreateInfo
    {
    public:
        SamplerCreateInfo();

        Filter magFilter;
        Filter minFilter;
        SamplerMipmapMode mipmapMode;
        SamplerAddressMode addressModeU;
        SamplerAddressMode addressModeV;
        SamplerAddressMode addressModeW;
        float mipLodBias;
        uint32_t anisotropyEnable;
        float maxAnisotropy;
        uint32_t compareEnable;
        CompareOp compareOp;
        float minLod;
        float maxLod;
        BorderColor borderColor;
        uint32_t unnormalizedCoordinates;
    };

    class ImageViewCreateInfo
    {
    public:
        ImageViewCreateInfo();

        Image *image;
        ImageViewType viewType;
        // VkComponentMapping         components;
        // VkImageSubresourceRange    subresourceRange;
        ImageAspectFlags aspectMask;
    };

    class ImageCreateInfo
    {
    public:
        ImageCreateInfo();

        uint32_t width;
        uint32_t height;
        uint32_t depth;
        ImageTiling tiling;
        ImageUsageFlags usage;
        MemoryPropertyFlags properties;
        Format format;
        ImageCreateFlags imageFlags;
        ImageType imageType;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        SampleCount samples;
        SharingMode sharingMode;
        uint32_t queueFamilyIndexCount;
        const uint32_t *pQueueFamilyIndices;
        ImageLayout initialLayout;
        std::string name;
    };

    class Image : public IHandle<Image, ImageHandle>
    {
    public:
        Image()
        {
        }

        Image(const ImageCreateInfo &info);

        ~Image();

        void CreateImageView(const ImageViewCreateInfo &info);

        void CreateSampler(const SamplerCreateInfo &info);

    private:
        friend class CommandBuffer;

        // For arrayLayers and/or mipLevels bigger than size 1, all of their layouts must be the same
        // TODO: Manage transitions for different layouts in arrayLayers and/or mipLevels
        void Barrier(CommandBuffer *cmd,
                     ImageLayout newLayout,
                     uint32_t baseArrayLayer = 0,
                     uint32_t arrayLayers = 1,
                     uint32_t baseMipLevel = 0,
                     uint32_t mipLevels = 1);

        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size = 0,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 1,
                                   uint32_t mipLevel = 0);

        void CopyImage(CommandBuffer *cmd, Image *src);

        void GenerateMipMaps(CommandBuffer *cmd);

        void BlitImage(CommandBuffer *cmd, Image *src, ImageBlit *region, Filter filter);

        void TransitionImageLayout(CommandBuffer *cmd,
                                   ImageLayout oldLayout,
                                   ImageLayout newLayout,
                                   PipelineStageFlags oldStageMask,
                                   PipelineStageFlags newStageMask,
                                   AccessFlags srcMask,
                                   AccessFlags dstMask,
                                   uint32_t baseArrayLayer,
                                   uint32_t arrayLayers,
                                   uint32_t baseMipLevel,
                                   uint32_t mipLevels);

        ImageLayout GetLayout(uint32_t layer = 0, uint32_t mip = 0) { return m_layouts[layer][mip]; }

    public:
        ImageViewHandle view;
        SamplerHandle sampler;
        AllocationHandle allocation;
        ImageCreateInfo imageInfo;
        ImageViewCreateInfo viewInfo;
        SamplerCreateInfo samplerInfo;

        float width_f{};
        float height_f{};
        PipelineColorBlendAttachmentState blendAttachment;

    private:
        friend class Swapchain;
        std::vector<std::vector<ImageLayout>> m_layouts{};
    };
}