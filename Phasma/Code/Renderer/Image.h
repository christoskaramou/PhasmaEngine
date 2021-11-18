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

    enum class LayoutState
    {
        ColorRead,
        ColorWrite,
        DepthRead,
        DepthWrite
    };

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
        //VkComponentMapping         components;
        //VkImageSubresourceRange    subresourceRange;
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
        VkImageType imageType;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        SampleCountFlagBits samples;
        SharingMode sharingMode;
        uint32_t queueFamilyIndexCount;
        const uint32_t *pQueueFamilyIndices;
        ImageLayout initialLayout;
        LayoutState layoutState;
    };

    class Image : public IHandle<Image, ImageHandle>
    {
    public:
        Image()
        {}

        Image(const ImageCreateInfo &info);

        ~Image();

        std::string name{};

        ImageViewHandle view;
        SamplerHandle sampler;
        VmaAllocation allocation{};
        ImageCreateInfo imageInfo;
        ImageViewCreateInfo viewInfo;
        SamplerCreateInfo samplerInfo;

        float width_f{};
        float height_f{};
        PipelineColorBlendAttachmentState blendAttachment;

        void TransitionImageLayout(
                CommandBuffer *cmd,
                ImageLayout oldLayout,
                ImageLayout newLayout,
                PipelineStageFlags oldStageMask,
                PipelineStageFlags newStageMask,
                AccessFlags srcMask,
                AccessFlags dstMask
        );

        void CreateImageView(const ImageViewCreateInfo &info);

        void TransitionImageLayout(ImageLayout oldLayout, ImageLayout newLayout);

        void ChangeLayout(CommandBuffer *cmd, LayoutState state);

        void CopyBufferToImage(Buffer *buffer, uint32_t baseLayer = 0);

        void CopyColorAttachment(CommandBuffer *cmd, Image *renderedImage);

        void GenerateMipMaps();

        void CreateSampler(const SamplerCreateInfo &info);
    };
}