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
    extern VkImageAspectFlags GetAspectMaskVK(Format format);
    extern VkImageAspectFlags GetAspectMaskVK(VkFormat format);
    extern ImageAspectFlags GetAspectMask(Format format);
    extern bool HasStencilVK(VkFormat format);
    extern bool HasStencil(Format format);
    extern void GetVkInfoFromLayout(ImageLayout layout, VkPipelineStageFlags &stageFlags, VkAccessFlags &accessMask);
    extern void GetInfoFromLayout(ImageLayout layout, PipelineStageFlags &stageFlags, AccessFlags &accessMask);

    extern ThreadPool e_ThreadPool;
}