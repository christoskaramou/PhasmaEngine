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

    extern bool IsDepthStencil(Format format);
    extern bool IsDepthOnly(Format format);
    extern bool IsStencilOnly(Format format);
    extern bool HasDepth(Format format);
    extern bool HasStencil(Format format);
    extern ImageAspectFlags GetAspectMask(Format format);

    extern ThreadPool e_ThreadPool;
    extern ThreadPool e_Update_ThreadPool;
    extern ThreadPool e_Render_ThreadPool;
    extern ThreadPool e_FW_ThreadPool;
    extern ThreadPool e_GUI_ThreadPool;

    // Main thread id
    extern std::thread::id e_MainThreadID;
}
