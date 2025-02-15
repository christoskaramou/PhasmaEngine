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
    // ---------------------------------

    extern bool IsDepthStencil(Format format);
    extern bool IsDepthOnly(Format format);
    extern bool IsStencilOnly(Format format);
    extern bool HasDepth(Format format);
    extern bool HasStencil(Format format);
    extern ImageAspectFlags GetAspectMask(Format format);
    extern std::string PresentModeToString(PresentMode presentMode);

    extern ThreadPool e_ThreadPool;
    extern ThreadPool e_Update_ThreadPool;
    extern ThreadPool e_Render_ThreadPool;
    extern ThreadPool e_FW_ThreadPool;
    extern ThreadPool e_GUI_ThreadPool;

    // Main thread id
    extern std::thread::id e_MainThreadID;
}
