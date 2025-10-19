#pragma once

namespace pe
{
    // PREDEFINED EVENTS ---------------
    // ---------------------------------

    extern bool IsDepthStencil(vk::Format format);
    extern bool IsDepthOnly(vk::Format format);
    extern bool IsStencilOnly(vk::Format format);
    extern bool HasDepth(vk::Format format);
    extern bool HasStencil(vk::Format format);
    extern vk::ImageAspectFlags GetAspectMask(vk::Format format);
    extern const char *PresentModeToString(vk::PresentModeKHR presentMode);

    extern ThreadPool e_ThreadPool;
    extern ThreadPool e_Update_ThreadPool;
    extern ThreadPool e_Render_ThreadPool;
    extern ThreadPool e_FW_ThreadPool;
    extern ThreadPool e_GUI_ThreadPool;

    // Main thread id
    extern std::thread::id e_MainThreadID;

    // global RHI instance
    class RHI;
    extern RHI &RHII;
}
