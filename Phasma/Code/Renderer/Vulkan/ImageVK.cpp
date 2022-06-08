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
#include "Renderer/Command.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"

namespace pe
{
    void GetVkInfoFromState(
        LayoutState state,
        VkPipelineStageFlags &stageFlags,
        VkAccessFlags &accessMask)
    {
        switch (state)
        {
        case LayoutState::Undefined:
            stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        case LayoutState::General:
            stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        case LayoutState::ColorAttachment:
            stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case LayoutState::DepthStencilAttachment:
            stageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case LayoutState::DepthStencilReadOnly:
            stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case LayoutState::ShaderReadOnly:
            stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case LayoutState::TransferSrc:
            stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case LayoutState::TransferDst:
            stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case LayoutState::Preinitialized:
            stageFlags = VK_PIPELINE_STAGE_HOST_BIT;
            accessMask = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case LayoutState::PresentSrc:
            stageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            accessMask = 0;
            break;
        }
    }

    LayoutState GetStateFromVkLayout(uint32_t layout)
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return LayoutState::Undefined;
        case VK_IMAGE_LAYOUT_GENERAL:
            return LayoutState::General;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return LayoutState::ColorAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return LayoutState::DepthStencilAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return LayoutState::DepthStencilReadOnly;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return LayoutState::ShaderReadOnly;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return LayoutState::TransferSrc;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return LayoutState::TransferDst;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return LayoutState::Preinitialized;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return LayoutState::PresentSrc;
        }

        PE_ERROR("Unknown layout");
        return LayoutState::Undefined;
    }

    VkImageLayout GetLayoutFromState(LayoutState state)
    {
        switch (state)
        {
        case LayoutState::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case LayoutState::General:
            return VK_IMAGE_LAYOUT_GENERAL;
        case LayoutState::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case LayoutState::DepthStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case LayoutState::DepthStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case LayoutState::ShaderReadOnly:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case LayoutState::TransferSrc:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case LayoutState::TransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case LayoutState::Preinitialized:
            return VK_IMAGE_LAYOUT_PREINITIALIZED;
        case LayoutState::PresentSrc:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        PE_ERROR("Unknown layout state");
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

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
        initialState = LayoutState::Undefined;
    }

    Image::Image(const ImageCreateInfo &info)
    {
        view = {};
        sampler = {};
        imageInfo = {};
        viewInfo = {};
        samplerInfo = {};
        blendAttachment = {};

        imageInfo = info;

        layoutStates.resize(imageInfo.arrayLayers);
        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            layoutStates[i] = std::vector<LayoutState>(imageInfo.mipLevels);
            for (uint32_t j = 0; j < imageInfo.mipLevels; j++)
                layoutStates[i][j] = info.initialState;
        }

        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), (VkFormat)imageInfo.format, &fProps);

        if (imageInfo.tiling == VK_IMAGE_TILING_OPTIMAL)
        {
            if (!fProps.optimalTilingFeatures)
                PE_ERROR("Image(): no optimal tiling features supported.");
        }
        else if (imageInfo.tiling == VK_IMAGE_TILING_LINEAR)
        {
            if (!fProps.linearTilingFeatures)
                PE_ERROR("Image(): no linear tiling features supported.");
        }
        else
        {
            PE_ERROR("Image(): tiling not supported.");
        }

        width_f = static_cast<float>(imageInfo.width);
        height_f = static_cast<float>(imageInfo.height);

        VkImageCreateInfo imageInfoVK{};
        imageInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfoVK.flags = imageInfo.imageFlags;
        imageInfoVK.imageType = imageInfo.imageType;
        imageInfoVK.format = (VkFormat)imageInfo.format;
        imageInfoVK.extent = VkExtent3D{imageInfo.width, imageInfo.height, imageInfo.depth};
        imageInfoVK.mipLevels = imageInfo.mipLevels;
        imageInfoVK.arrayLayers = imageInfo.arrayLayers;
        imageInfoVK.samples = (VkSampleCountFlagBits)imageInfo.samples;
        imageInfoVK.tiling = (VkImageTiling)imageInfo.tiling;
        imageInfoVK.usage = imageInfo.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // All images can be copied
        imageInfoVK.sharingMode = (VkSharingMode)imageInfo.sharingMode;
        imageInfoVK.initialLayout = GetLayoutFromState(imageInfo.initialState);

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateImage(RHII.GetAllocator(), &imageInfoVK, &allocationCreateInfo, &imageVK, &allocationVK, &allocationInfo));
        m_handle = imageVK;
        allocation = allocationVK;

        Debug::SetObjectName(m_handle, VK_OBJECT_TYPE_IMAGE, info.name);
    }

    Image::~Image()
    {
        if (view)
        {
            vkDestroyImageView(RHII.GetDevice(), view, nullptr);
            view = {};
        }

        if (m_handle)
        {
            vmaDestroyImage(RHII.GetAllocator(), m_handle, allocation);
            m_handle = {};
        }

        if (VkSampler(sampler))
        {
            vkDestroySampler(RHII.GetDevice(), sampler, nullptr);
            sampler = {};
        }
    }

    void Image::TransitionImageLayout(
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
        uint32_t mipLevels)
    {
        // If no cmd exists, start the general cmd
        if (!cmd)
            RHII.GetGeneralCmd()->Begin();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcMask;
        barrier.dstAccessMask = dstMask;
        barrier.image = m_handle;
        barrier.oldLayout = GetLayoutFromState(oldLayout);
        barrier.newLayout = GetLayoutFromState(newLayout);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = viewInfo.aspectMask;
        barrier.subresourceRange.baseMipLevel = baseMipLevel;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
        barrier.subresourceRange.layerCount = arrayLayers;
        if (imageInfo.format == VK_FORMAT_D32_SFLOAT_S8_UINT || imageInfo.format == VK_FORMAT_D24_UNORM_S8_UINT)
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        vkCmdPipelineBarrier(
            cmd ? cmd->Handle() : RHII.GetGeneralCmd()->Handle(),
            oldStageMask,
            newStageMask,
            VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        for (uint32_t i = 0; i < arrayLayers; i++)
            for (uint32_t j = 0; j < mipLevels; j++)
                layoutStates[baseArrayLayer + i][baseMipLevel + j] = newLayout;

        // If a new cmd was created, wait and submit it
        if (!cmd)
        {
            CommandBuffer *cmdBuffer = RHII.GetGeneralCmd();
            cmdBuffer->End();
            RHII.GetGeneralQueue()->SubmitAndWaitFence(1, &cmdBuffer, nullptr, 0, nullptr, 0, nullptr);;
        }
    }

    void Image::CreateImageView(const ImageViewCreateInfo &info)
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
                info.image->imageInfo.arrayLayers};

        VkImageView vkView;
        PE_CHECK(vkCreateImageView(RHII.GetDevice(), &viewInfoVK, nullptr, &vkView));
        view = vkView;

        Debug::SetObjectName(view, VK_OBJECT_TYPE_IMAGE_VIEW, imageInfo.name);
    }

    void Image::ChangeLayout(CommandBuffer *cmd,
                             LayoutState newState,
                             uint32_t baseArrayLayer,
                             uint32_t arrayLayers,
                             uint32_t baseMipLevel,
                             uint32_t mipLevels)
    {
        LayoutState oldState = layoutStates[baseArrayLayer][baseMipLevel];
        if (newState != oldState)
        {
            VkPipelineStageFlags oldPipelineStage;
            VkAccessFlags oldAccess;
            GetVkInfoFromState(layoutStates[baseArrayLayer][baseMipLevel], oldPipelineStage, oldAccess);

            VkPipelineStageFlags newPipelineStage;
            VkAccessFlags newAccess;
            GetVkInfoFromState(newState, newPipelineStage, newAccess);

            TransitionImageLayout(cmd,
                                  oldState,
                                  newState,
                                  oldPipelineStage,
                                  newPipelineStage,
                                  oldAccess,
                                  newAccess,
                                  baseArrayLayer,
                                  arrayLayers,
                                  baseMipLevel,
                                  mipLevels);
        }
    }

    void Image::CopyBufferToImage(CommandBuffer *cmd,
                                  Buffer *buffer,
                                  uint32_t baseArrayLayer,
                                  uint32_t layerCount,
                                  uint32_t mipLevel)
    {
        Queue *queue;
        CommandBuffer *commandBuffer = cmd;
        // If cmd is nullptr, start a new one
        if (!cmd)
        {
            queue = Queue::GetNext(QueueType::TransferBit, 1);
            commandBuffer = CommandBuffer::GetNext(queue->GetFamilyId());
            commandBuffer->Begin();
        }

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = viewInfo.aspectMask;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount;
        region.imageOffset = VkOffset3D{0, 0, 0};
        region.imageExtent = VkExtent3D{imageInfo.width, imageInfo.height, imageInfo.depth};

        ChangeLayout(commandBuffer, LayoutState::TransferDst, baseArrayLayer, layerCount, mipLevel, imageInfo.mipLevels - mipLevel);
        vkCmdCopyBufferToImage(commandBuffer->Handle(), buffer->Handle(), m_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // If a new cmd was created, wait and submit it
        if (!cmd)
        {
            commandBuffer->End();
            queue->SubmitAndWaitFence(1, &commandBuffer, nullptr, 0, nullptr, 0, nullptr);
            Queue::Return(queue);
            CommandBuffer::Return(commandBuffer);
        }
    }

    void Image::CopyColorAttachment(CommandBuffer *cmd, Image *colorAttachement)
    {
        Debug::InsertCmdLabel(cmd->Handle(), "CopyColorAttachment: " + colorAttachement->imageInfo.name);

        LayoutState previousState = layoutStates[0][0];
        LayoutState caPreviousState = colorAttachement->layoutStates[0][0];

        ChangeLayout(cmd, LayoutState::TransferDst);
        colorAttachement->ChangeLayout(cmd, LayoutState::TransferSrc);

        // copy the image
        VkImageCopy region{};
        region.srcSubresource.aspectMask = viewInfo.aspectMask;
        region.srcSubresource.layerCount = imageInfo.arrayLayers;
        region.dstSubresource.aspectMask = colorAttachement->viewInfo.aspectMask;
        region.dstSubresource.layerCount = colorAttachement->imageInfo.arrayLayers;
        region.extent.width = colorAttachement->imageInfo.width;
        region.extent.height = colorAttachement->imageInfo.height;
        region.extent.depth = colorAttachement->imageInfo.depth;

        vkCmdCopyImage(
            cmd->Handle(),
            colorAttachement->m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
    }

    void Image::GenerateMipMaps()
    {
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), (VkFormat)imageInfo.format, &fProps);

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

        Queue *queue = Queue::GetNext(QueueType::GraphicsBit, 1);
        std::vector<std::vector<CommandBuffer *>> commandBuffers(imageInfo.arrayLayers);
        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            for (uint32_t j = 0; j < imageInfo.mipLevels; j++)
                commandBuffers[i].push_back(CommandBuffer::GetNext(queue->GetFamilyId()));
        }

        auto mipWidth = static_cast<int32_t>(imageInfo.width);
        auto mipHeight = static_cast<int32_t>(imageInfo.height);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkImageBlit blit;
        blit.srcOffsets[0] = VkOffset3D{0, 0, 0};
        blit.srcSubresource.aspectMask = viewInfo.aspectMask;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = VkOffset3D{0, 0, 0};
        blit.dstSubresource.aspectMask = viewInfo.aspectMask;
        blit.dstSubresource.layerCount = 1;

        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            blit.srcSubresource.baseArrayLayer = i;
            blit.dstSubresource.baseArrayLayer = i;
            for (uint32_t j = 1; j < imageInfo.mipLevels; j++)
            {
                commandBuffers[i][j]->Begin();

                ChangeLayout(commandBuffers[i][j], LayoutState::TransferSrc, i, 1, j - 1);
                blit.srcOffsets[1] = VkOffset3D{mipWidth, mipHeight, 1};
                blit.srcSubresource.mipLevel = j - 1;

                ChangeLayout(commandBuffers[i][j], LayoutState::TransferDst, i, 1, j);
                blit.dstOffsets[1] = VkOffset3D{mipWidth / 2, mipHeight / 2, 1};
                blit.dstSubresource.mipLevel = j;

                vkCmdBlitImage(
                    commandBuffers[i][j]->Handle(),
                    m_handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    m_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit,
                    VK_FILTER_LINEAR);

                if (mipWidth > 1)
                    mipWidth /= 2;
                if (mipHeight > 1)
                    mipHeight /= 2;

                commandBuffers[i][j]->End();
                queue->SubmitAndWaitFence(1, &commandBuffers[i][j], nullptr, 0, nullptr, 0, nullptr);
            }
        }

        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            for (uint32_t j = 0; j < imageInfo.mipLevels; j++)
            {
                commandBuffers[i][j]->Begin();
                ChangeLayout(commandBuffers[i][j], LayoutState::ShaderReadOnly, i, 1, j, 1);
                commandBuffers[i][j]->End();
                queue->SubmitAndWaitFence(1, &commandBuffers[i][j], nullptr, 0, nullptr, 0, nullptr);
                CommandBuffer::Return(commandBuffers[i][j]);
            }
        }

        Queue::Return(queue);
    }

    void Image::CreateSampler(const SamplerCreateInfo &info)
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
        PE_CHECK(vkCreateSampler(RHII.GetDevice(), &samplerInfoVK, nullptr, &vkSampler));
        sampler = vkSampler;

        Debug::SetObjectName(sampler, VK_OBJECT_TYPE_SAMPLER, imageInfo.name);
    }
}
#endif
