#pragma once

#include "API/RHITypes.h"

#include "SDL.h"

#include <cstdint>
#include <string>

namespace pe
{
    struct RuntimeWindowDesc
    {
        const char *title = "Phasma";
        PeGraphicsApi api = PE_GRAPHICS_API_VULKAN;
        int displayIndex = 0;
        uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        bool showAfterCreate = false;
        bool maximizeAfterCreate = true;
        bool pumpEventsAfterCreate = true;
        bool logDisplaySelection = false;
    };

    [[nodiscard]] bool TryParseRuntimeDisplayIndex(const char *value, int &displayIndex);
    [[nodiscard]] bool TryParseRuntimeDisplayIndexArg(int argc,
                                                      char *argv[],
                                                      int &displayIndex,
                                                      std::string *error = nullptr);

    class RuntimeSdlSession
    {
    public:
        explicit RuntimeSdlSession(uint32_t initFlags = SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        ~RuntimeSdlSession();

        RuntimeSdlSession(const RuntimeSdlSession &) = delete;
        RuntimeSdlSession &operator=(const RuntimeSdlSession &) = delete;
        RuntimeSdlSession(RuntimeSdlSession &&) = delete;
        RuntimeSdlSession &operator=(RuntimeSdlSession &&) = delete;

    private:
        bool m_initialized = false;
    };

    class RuntimeWindow
    {
    public:
        explicit RuntimeWindow(const RuntimeWindowDesc &desc);
        ~RuntimeWindow();

        RuntimeWindow(const RuntimeWindow &) = delete;
        RuntimeWindow &operator=(const RuntimeWindow &) = delete;
        RuntimeWindow(RuntimeWindow &&) = delete;
        RuntimeWindow &operator=(RuntimeWindow &&) = delete;

        [[nodiscard]] SDL_Window *Get() const { return m_window; }

    private:
        SDL_Window *m_window = nullptr;
    };

    class RuntimeRhiSession
    {
    public:
        RuntimeRhiSession(SDL_Window *window, PeGraphicsApi api, bool initializeSwapchain);
        ~RuntimeRhiSession();

        RuntimeRhiSession(const RuntimeRhiSession &) = delete;
        RuntimeRhiSession &operator=(const RuntimeRhiSession &) = delete;
        RuntimeRhiSession(RuntimeRhiSession &&) = delete;
        RuntimeRhiSession &operator=(RuntimeRhiSession &&) = delete;

    private:
        bool m_initialized = false;
    };
} // namespace pe
