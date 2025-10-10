#pragma once

namespace pe
{
    // PREDEFINED EVENTS ---------------
    extern size_t EventQuit;
    extern size_t EventCustom;
    extern size_t EventSetWindowTitle;
    extern size_t EventCompileShaders;
    extern size_t EventCompileScripts;
    extern size_t EventResize;
    extern size_t EventFileWrite;
    extern size_t EventPresentMode;
    extern size_t EventAfterCommandWait;
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
