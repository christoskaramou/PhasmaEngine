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

namespace pe
{
    // PREDEFINED EVENTS ---------------
    extern EventID EventQuit;
    extern EventID EventCustom;
    extern EventID EventSetWindowTitle;
    extern EventID EventCompileShaders;
    extern EventID EventResize;
    extern EventID EventFileWrite;
    // ---------------------------------

    extern bool IsDepthFormatVK(VkFormat format);
    extern bool IsDepthFormat(Format format);
    extern bool HasStencilVK(VkFormat format);
    extern bool HasStencil(Format format);
    extern void GetVkInfoFromLayout(ImageLayout layout, VkPipelineStageFlags &stageFlags, VkAccessFlags &accessMask);
    extern void GetInfoFromLayout(ImageLayout layout, PipelineStageFlags &stageFlags, AccessFlags &accessMask);
    extern ImageLayout GetLayoutFromVK(uint32_t layout);
    extern VkImageLayout GetImageLayoutVK(ImageLayout layout);
    extern VkShaderStageFlags GetShaderStageVK(ShaderStageFlags flags);
    extern VkFilter GetFilterVK(Filter filter);
    extern VkImageAspectFlags GetImageAspectVK(ImageAspectFlags aspect);
    extern VkImageType GetImageTypeVK(ImageType type);
    extern VkImageViewType GetImageViewTypeVK(ImageViewType type);
    extern VkSampleCountFlagBits GetSampleCountVK(SampleCount count);
    extern VkFormat GetFormatVK(Format format);
    extern Format GetFormatFromVK(VkFormat format);
    extern VkImageTiling GetImageTilingVK(ImageTiling tiling);
    extern VkImageUsageFlags GetImageUsageVK(ImageUsageFlags usage);
    extern VkSharingMode GetSharingModeVK(SharingMode mode);
    extern VkImageCreateFlags GetImageCreateFlagsVK(ImageCreateFlags flags);
    extern VkCommandBufferUsageFlags GetCommandBufferUsageFlagsVK(CommandBufferUsageFlags flags);
    extern VkBufferUsageFlags GetBufferUsageFlagsVK(BufferUsageFlags flags);
    extern VkSamplerMipmapMode GetSamplerMipmapModeVK(SamplerMipmapMode mode);
    extern VkSamplerAddressMode GetSamplerAddressMode(SamplerAddressMode mode);
    extern VkCompareOp GetCompareOpVK(CompareOp op);
    extern VkBorderColor GetBorderColorVK(BorderColor color);
    extern VkOffset3D GetOffset3DVK(const Offset3D& offset);
    extern VkColorSpaceKHR GetColorSpaceVK(ColorSpace colorSpace);
    extern ColorSpace GetColorSpaceFromVK(VkColorSpaceKHR colorSpace);
    extern VkPresentModeKHR GetPresentModeVK(PresentMode presentMode);
    extern PresentMode GetPresentModeFromVK(VkPresentModeKHR presentMode);
    extern VkPipelineStageFlags GetPipelineStageFlagsVK(PipelineStageFlags flags);
    extern VkAttachmentLoadOp GetAttachmentLoadOpVK(AttachmentLoadOp loadOp);
    extern VkAttachmentStoreOp GetAttachmentStoreOpVK(AttachmentStoreOp storeOp);
    extern VkAccessFlags GetAccessFlagsVK(AccessFlags flags);
    extern VmaAllocationCreateFlags GetAllocationCreateFlagsVMA(AllocationCreateFlags flags);
    extern VkBlendFactor GetBlendFactorVK(BlendFactor blendFactor);
    extern VkBlendOp GetBlendOpVK(BlendOp blendOp);
    extern VkColorComponentFlags GetColorComponentFlagsVK(ColorComponentFlags flags);
    extern VkDynamicState GetDynamicStateVK(DynamicState state);
    extern VkDescriptorType GetDescriptorTypeVK(DescriptorType type);
    extern VkObjectType GetObjectTypeVK(ObjectType type);
    extern VkVertexInputRate GetVertexInputRateVK(VertexInputRate rate);
    extern VkAttachmentDescriptionFlags GetAttachmentDescriptionVK(AttachmentDescriptionFlags flags);
    extern VkCullModeFlags GetCullModeVK(CullMode mode);
}