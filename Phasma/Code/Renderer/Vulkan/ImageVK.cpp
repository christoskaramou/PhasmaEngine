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

#if PE_VULKAN
#include "Renderer/Image.h"
#include "Renderer/RHI.h"
#include "ECS/Context.h"
#include "Renderer/Command.h"
#include "Renderer/Buffer.h"

namespace pe
{
    SamplerCreateInfo::SamplerCreateInfo()
    {
        magFilter = VK_FILTER_LINEAR;
        minFilter = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        mipLodBias = 0.f;
        anisotropyEnable = VK_TRUE;
        maxAnisotropy = 16.0f;
        compareEnable = VK_FALSE;
        compareOp = VK_COMPARE_OP_LESS;
        minLod = 0.f;
        maxLod = 1.f;
        borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        unnormalizedCoordinates = VK_FALSE;
    }

    ImageViewCreateInfo::ImageViewCreateInfo()
    {
        image = nullptr;
        viewType = VK_IMAGE_VIEW_TYPE_2D;
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    ImageCreateInfo::ImageCreateInfo()
    {
        width = 0;
        height = 0;
        depth = 1;
        tiling = VK_IMAGE_TILING_OPTIMAL;
        usage = 0;
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        format = VK_FORMAT_UNDEFINED;
        imageFlags = 0;
        imageType = VK_IMAGE_TYPE_2D;
        mipLevels = 1;
        arrayLayers = 1;
        samples = 1;
        sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        queueFamilyIndexCount = 0;
        pQueueFamilyIndices = nullptr;
        initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        layoutState = LayoutState::ColorRead;
    }

    Image::Image(const ImageCreateInfo& info)
    {
        view = {};
        sampler = {};

        imageInfo = {};
        viewInfo = {};
        samplerInfo = {};

        blendAttachment = {};

        imageInfo = info;

        VkImageUsageFlags _usage = imageInfo.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // All images can be copied

        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.gpu, (VkFormat)imageInfo.format, &fProps);

        if (imageInfo.tiling == VK_IMAGE_TILING_OPTIMAL)
        {
            if (!fProps.optimalTilingFeatures)
                PE_ERROR("createImage(): wrong format error, no optimal tiling features supported.");
        }
        else if (imageInfo.tiling == VK_IMAGE_TILING_LINEAR)
        {
            if (!fProps.linearTilingFeatures)
                PE_ERROR("createImage(): wrong format error, no linear tiling features supported.");
        }
        else
        {
            PE_ERROR("createImage(): wrong format error.");
        }

        VkImageFormatProperties ifProps;
        vkGetPhysicalDeviceImageFormatProperties(RHII.gpu, (VkFormat)imageInfo.format, imageInfo.imageType, (VkImageTiling)imageInfo.tiling, _usage, VkImageCreateFlags(), &ifProps);

        if (ifProps.maxArrayLayers < imageInfo.arrayLayers ||
            ifProps.maxExtent.width < imageInfo.width ||
            ifProps.maxExtent.height < imageInfo.height ||
            ifProps.maxMipLevels < imageInfo.mipLevels ||
            !(ifProps.sampleCounts & imageInfo.samples))
            PE_ERROR("createImage(): image format properties error!");


        imageInfo.width = imageInfo.width % 2 != 0 ? imageInfo.width - 1 : imageInfo.width;
        imageInfo.height = imageInfo.height % 2 != 0 ? imageInfo.height - 1 : imageInfo.height;
        width_f = static_cast<float>(imageInfo.width);
        height_f = static_cast<float>(imageInfo.height);

        VkImageCreateInfo imageInfoVK{};
        imageInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfoVK.flags = imageInfo.imageFlags;
        imageInfoVK.imageType = imageInfo.imageType;
        imageInfoVK.format = (VkFormat)imageInfo.format;
        imageInfoVK.extent = VkExtent3D{ imageInfo.width, imageInfo.height, imageInfo.depth };
        imageInfoVK.mipLevels = imageInfo.mipLevels;
        imageInfoVK.arrayLayers = imageInfo.arrayLayers;
        imageInfoVK.samples = (VkSampleCountFlagBits)imageInfo.samples;
        imageInfoVK.tiling = (VkImageTiling)imageInfo.tiling;
        imageInfoVK.usage = _usage;
        imageInfoVK.sharingMode = (VkSharingMode)imageInfo.sharingMode;
        imageInfoVK.initialLayout = (VkImageLayout)imageInfo.initialLayout;

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VmaAllocationInfo allocationInfo;
        VkImage vkImage;
        vmaCreateImage(RHII.allocator, &imageInfoVK, &allocationCreateInfo, &vkImage, &allocation, &allocationInfo);
        m_handle = vkImage;
    }


    Image::~Image()
    {
        if (view)
        {
            vkDestroyImageView(RHII.device, view, nullptr);
            view = {};
        }

        if (m_handle)
        {
            vmaDestroyImage(RHII.allocator, m_handle, allocation);
            m_handle = {};
        }

        if (VkSampler(sampler))
        {
            vkDestroySampler(RHII.device, sampler, nullptr);
            sampler = {};
        }
    }

    void Image::TransitionImageLayout(
        CommandBuffer* cmd,
        ImageLayout oldLayout,
        ImageLayout newLayout,
        PipelineStageFlags oldStageMask,
        PipelineStageFlags newStageMask,
        AccessFlags srcMask,
        AccessFlags dstMask
    )
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcMask;
        barrier.dstAccessMask = dstMask;
        barrier.image = m_handle;
        barrier.oldLayout = (VkImageLayout)oldLayout;
        barrier.newLayout = (VkImageLayout)newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = viewInfo.aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = imageInfo.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = imageInfo.arrayLayers;
        if (imageInfo.format == VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT || imageInfo.format == VK_FORMAT_D24_UNORM_S8_UINT)
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        vkCmdPipelineBarrier(
            cmd->Handle(),
            oldStageMask,
            newStageMask,
            VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    }

    void Image::CreateImageView(const ImageViewCreateInfo& info)
    {
        viewInfo = info;
        VkImageViewCreateInfo viewInfoVK{};
        viewInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfoVK.image = info.image->m_handle;
        viewInfoVK.viewType = (VkImageViewType)viewInfo.viewType;
        viewInfoVK.format = (VkFormat)imageInfo.format;
        viewInfoVK.subresourceRange =
        {
            (VkImageAspectFlags)viewInfo.aspectMask,
            0,
            info.image->imageInfo.mipLevels,
            0,
            info.image->imageInfo.arrayLayers
        };

        VkImageView vkView;
        vkCreateImageView(RHII.device, &viewInfoVK, nullptr, &vkView);
        view = vkView;
    }

    void Image::TransitionImageLayout(ImageLayout oldLayout, ImageLayout newLayout)
    {
        std::array<CommandBuffer*, 1> cmd{};
        cmd[0] = CommandBuffer::Create(RHII.commandPool2);
        cmd[0]->Begin();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = (VkImageLayout)oldLayout;
        barrier.newLayout = (VkImageLayout)newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_handle;

        VkPipelineStageFlags srcStage;
        VkPipelineStageFlags dstStage;

        // Subresource aspectMask
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, imageInfo.mipLevels, 0, imageInfo.arrayLayers };
            if (imageInfo.format == VK_FORMAT_D32_SFLOAT_S8_UINT || imageInfo.format == VK_FORMAT_D24_UNORM_S8_UINT)
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, imageInfo.mipLevels, 0, imageInfo.arrayLayers };

        // Src, Dst AccessMasks and Pipeline Stages for pipelineBarrier
        if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else
        {
            PE_ERROR("Transition image layout invalid combination of layouts");
        }

        vkCmdPipelineBarrier(
            cmd[0]->Handle(),
            srcStage,
            dstStage,
            VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        cmd[0]->End();
        RHII.SubmitAndWaitFence(1, cmd.data(), nullptr, 0, nullptr, 0, nullptr);
        cmd[0]->Destroy();
    }

    void Image::ChangeLayout(CommandBuffer* cmd, LayoutState state)
    {
        if (state != imageInfo.layoutState)
        {
            if (state == LayoutState::ColorRead)
            {
                TransitionImageLayout(
                    cmd,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT
                );
            }
            else if (state == LayoutState::ColorWrite)
            {
                TransitionImageLayout(
                    cmd,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                );
            }
            else if (state == LayoutState::DepthRead)
            {
                TransitionImageLayout(
                    cmd,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT
                );
            }
            else if (state == LayoutState::DepthWrite)
            {
                TransitionImageLayout(
                    cmd,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                );
            }
            imageInfo.layoutState = state;
        }
    }

    void Image::CopyBufferToImage(Buffer* buffer, uint32_t baseLayer)
    {
        std::array<CommandBuffer*, 1> cmd{};
        cmd[0] = CommandBuffer::Create(RHII.commandPool2);
        cmd[0]->Begin();

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = viewInfo.aspectMask;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = baseLayer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = VkOffset3D{ 0, 0, 0 };
        region.imageExtent = VkExtent3D{ imageInfo.width, imageInfo.height, imageInfo.depth };

        vkCmdCopyBufferToImage(cmd[0]->Handle(), buffer->Handle(), m_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        cmd[0]->End();
        RHII.SubmitAndWaitFence(1, cmd.data(), nullptr, 0, nullptr, 0, nullptr);
        cmd[0]->Destroy();
    }

    void Image::CopyColorAttachment(CommandBuffer* cmd, Image* renderedImage)
    {
        TransitionImageLayout(
            cmd,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT
        );
        renderedImage->TransitionImageLayout(
            cmd,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT
        );

        // copy the image
        VkImageCopy region{};
        region.srcSubresource.aspectMask = viewInfo.aspectMask;
        region.srcSubresource.layerCount = imageInfo.arrayLayers;
        region.dstSubresource.aspectMask = renderedImage->viewInfo.aspectMask;
        region.dstSubresource.layerCount = renderedImage->imageInfo.arrayLayers;
        region.extent.width = renderedImage->imageInfo.width;
        region.extent.height = renderedImage->imageInfo.height;
        region.extent.depth = renderedImage->imageInfo.depth;

        vkCmdCopyImage(
            cmd->Handle(),
            renderedImage->m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);

        TransitionImageLayout(
            cmd,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
        );
        renderedImage->TransitionImageLayout(
            cmd,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        );
    }

    void Image::GenerateMipMaps()
    {
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.gpu, (VkFormat)imageInfo.format, &fProps);

        if (imageInfo.tiling == VK_IMAGE_TILING_OPTIMAL)
        {
            if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
                PE_ERROR("generateMipMaps(): Image tiling error, linear filter is not supported.");
        }
        else if (imageInfo.tiling == VK_IMAGE_TILING_LINEAR)
        {
            if (!(fProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
                PE_ERROR("generateMipMaps(): Image tiling error, linear filter is not supported.");
        }
        else
        {
            PE_ERROR("generateMipMaps(): Image tiling error.");
        }

        std::vector<CommandBuffer*> commandBuffers(imageInfo.mipLevels);
        for (uint32_t i = 0; i < imageInfo.mipLevels; i++)
            commandBuffers[i] = CommandBuffer::Create(RHII.commandPool2);

        auto mipWidth = static_cast<int32_t>(imageInfo.width);
        auto mipHeight = static_cast<int32_t>(imageInfo.height);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_handle;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        VkImageBlit blit;
        blit.srcOffsets[0] = VkOffset3D{ 0, 0, 0 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = VkOffset3D{ 0, 0, 0 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        for (uint32_t i = 1; i < imageInfo.mipLevels; i++)
        {
            commandBuffers[i]->Begin();

            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffers[i]->Handle(),
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkDependencyFlagBits(),
                0, nullptr,
                0, nullptr,
                1, &barrier);

            blit.srcOffsets[1] = VkOffset3D{ mipWidth, mipHeight, 1 };
            blit.dstOffsets[1] = VkOffset3D{ mipWidth / 2, mipHeight / 2, 1 };
            blit.srcSubresource.mipLevel = i - 1;
            blit.dstSubresource.mipLevel = i;

            vkCmdBlitImage(
                commandBuffers[i]->Handle(),
                m_handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffers[i]->Handle(),
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_DEPENDENCY_BY_REGION_BIT,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;

            commandBuffers[i]->End();
            RHII.SubmitAndWaitFence(1, &commandBuffers[i], nullptr, 0, nullptr, 0, nullptr);
        }

        commandBuffers[0]->Begin();

        barrier.subresourceRange.baseMipLevel = imageInfo.mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffers[0]->Handle(),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        commandBuffers[0]->End();
        RHII.SubmitAndWaitFence(1, &commandBuffers[0], nullptr, 0, nullptr, 0, nullptr);

        for (uint32_t i = 0; i < imageInfo.mipLevels; i++)
            commandBuffers[i]->Destroy();
    }

    void Image::CreateSampler(const SamplerCreateInfo& info)
    {
        samplerInfo = info;

        VkSamplerCreateInfo samplerInfoVK{};
        samplerInfoVK.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfoVK.pNext = nullptr;
        samplerInfoVK.flags = 0;
        samplerInfoVK.magFilter = (VkFilter)samplerInfo.magFilter;
        samplerInfoVK.minFilter = (VkFilter)samplerInfo.minFilter;
        samplerInfoVK.mipmapMode = (VkSamplerMipmapMode)samplerInfo.mipmapMode;
        samplerInfoVK.addressModeU = (VkSamplerAddressMode)samplerInfo.addressModeU;
        samplerInfoVK.addressModeV = (VkSamplerAddressMode)samplerInfo.addressModeV;
        samplerInfoVK.addressModeW = (VkSamplerAddressMode)samplerInfo.addressModeW;
        samplerInfoVK.mipLodBias = 0.f;
        samplerInfoVK.anisotropyEnable = samplerInfo.anisotropyEnable;
        samplerInfoVK.maxAnisotropy = samplerInfo.maxAnisotropy;
        samplerInfoVK.compareEnable = samplerInfo.compareEnable;
        samplerInfoVK.compareOp = (VkCompareOp)samplerInfo.compareOp;
        samplerInfoVK.minLod = samplerInfo.minLod;
        samplerInfoVK.maxLod = samplerInfo.maxLod;
        samplerInfoVK.borderColor = (VkBorderColor)samplerInfo.borderColor;
        samplerInfoVK.unnormalizedCoordinates = samplerInfo.unnormalizedCoordinates;

        VkSampler vkSampler;
        vkCreateSampler(RHII.device, &samplerInfoVK, nullptr, &vkSampler);
        sampler = vkSampler;
    }
}
#endif
