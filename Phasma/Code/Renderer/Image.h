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
        Undefined,
        General,
        ColorAttachment,
        DepthStencilAttachment,
        DepthStencilReadOnly,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Preinitialized,
        PresentSrc
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
        VkImageType imageType;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        SampleCountFlagBits samples;
        SharingMode sharingMode;
        uint32_t queueFamilyIndexCount;
        const uint32_t *pQueueFamilyIndices;
        LayoutState initialState;
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

        ImageViewHandle view;
        SamplerHandle sampler;
        AllocationHandle allocation;
        ImageCreateInfo imageInfo;
        ImageViewCreateInfo viewInfo;
        SamplerCreateInfo samplerInfo;

        float width_f{};
        float height_f{};
        PipelineColorBlendAttachmentState blendAttachment;

        void CreateImageView(const ImageViewCreateInfo &info);

        // For arrayLayers and/or mipLevels bigger than size 1, all of their layouts must be the same
        // TODO: Manage transitions for different layouts in arrayLayers and/or mipLevels
        void ChangeLayout(CommandBuffer *cmd,
                          LayoutState newState,
                          uint32_t baseArrayLayer = 0,
                          uint32_t arrayLayers = 1,
                          uint32_t baseMipLevel = 0,
                          uint32_t mipLevels = 1);

        void CopyBufferToImage(CommandBuffer *cmd,
                               Buffer *buffer,
                               uint32_t baseArrayLayer = 0,
                               uint32_t layerCount = 1,
                               uint32_t mipLevel = 0);

        void CopyColorAttachment(CommandBuffer *cmd, Image *renderedImage);

        void GenerateMipMaps();

        void CreateSampler(const SamplerCreateInfo &info);

    private:
        void TransitionImageLayout(
            CommandBuffer *cmd,
            LayoutState oldLayout,
            LayoutState newLayout,
            PipelineStageFlags oldStageMask,
            PipelineStageFlags newStageMask,
            AccessFlags srcMask,
            AccessFlags dstMask,
            uint32_t baseArrayLayer,
            uint32_t arrayLayers,
            uint32_t baseMipLevel,
            uint32_t mipLevels);

        friend class Swapchain;
        std::vector<std::vector<LayoutState>> layoutStates{};
    };
}