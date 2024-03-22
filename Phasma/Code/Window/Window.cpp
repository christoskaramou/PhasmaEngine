#include "Window.h"
#include "Renderer/RHI.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/CameraSystem.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    Window::Window(int x, int y, int w, int h, uint32_t flags)
    {
        m_handle = SDL_CreateWindow("", x, y, w, h, flags);
        if (!m_handle)
        {
            std::cout << SDL_GetError();
            return;
        }

        auto setTitle = [this](const std::any &title)
        {
            SDL_SetWindowTitle(m_handle, std::any_cast<std::string>(title).c_str());
        };

        EventSystem::RegisterCallback(EventSetWindowTitle, setTitle);
    }

    Window::~Window()
    {
        SDL_DestroyWindow(m_handle);
        SDL_Quit();
    }
    
    inline bool IsButtonDown(int *x, int *y, uint32_t button)
    {
        return SDL_GetMouseState(x, y) & button;
    }

    inline void SetRelativeMouseMode(bool enable)
    {
        SDL_SetRelativeMouseMode(enable ? SDL_TRUE : SDL_FALSE);
    }

    inline bool IsRelativeMouseModeOn()
    {
        return SDL_GetRelativeMouseMode() == SDL_TRUE;
    }

    void Window::SmoothMouseRotation(Camera *camera, uint32_t triggerButton)
    {
        PE_ERROR_IF(!camera, "Camera is nullptr");

        int x, y;
        static bool skipNextRotation = false;

        if (IsButtonDown(&x, &y, triggerButton))
        {
            if (!IsRelativeMouseModeOn())
            {
                SetRelativeMouseMode(true);
                skipNextRotation = true;
            }

            SDL_GetRelativeMouseState(&x, &y);

            if (!skipNextRotation)
            {
                camera->Rotate(static_cast<float>(x), static_cast<float>(y));
            }
            else
            {
                skipNextRotation = false;
            }
        }
        else
        {
            if (IsRelativeMouseModeOn())
            {
                SetRelativeMouseMode(false);
                WrapMouse(x, y);
            }
        }
    }

    bool Window::ProcessEvents(double delta)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        CameraSystem *cameraSystem = GetGlobalSystem<CameraSystem>();
        Camera *camera = cameraSystem->GetCamera(0);

        ImGuiIO &io = ImGui::GetIO();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                EventSystem::PushEvent(EventQuit);

            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                EventSystem::PushEvent(EventResize);
        }

        if (EventSystem::PollEvent(EventQuit))
            return false;

        // Handle mouse rotation
        SmoothMouseRotation(camera, SDL_BUTTON(SDL_BUTTON_RIGHT));

        if (ImGui::IsKeyDown(ImGuiKey_Escape))
        {
            const SDL_MessageBoxButtonData buttons[] = {
                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "cancel"},
                {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "yes"}};
            const SDL_MessageBoxColorScheme colorScheme = {
                {{255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {0, 0, 255}, {255, 0, 255}}};
            const SDL_MessageBoxData messageboxdata = {
                SDL_MESSAGEBOX_INFORMATION, nullptr, "Exit", "Are you sure you want to exit?",
                SDL_arraysize(buttons), buttons, &colorScheme};
            int buttonid;
            SDL_ShowMessageBox(&messageboxdata, &buttonid);
            if (buttonid == 1)
                return false;
        }

        float speed = Settings::Get<GlobalSettings>().camera_speed * static_cast<float>(delta);
        if ((ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_S)) &&
            (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_D)))
            speed *= 0.707f;

        if (ImGui::IsKeyDown(ImGuiKey_W))
            camera->Move(CameraDirection::FORWARD, speed);
        if (ImGui::IsKeyDown(ImGuiKey_S))
            camera->Move(CameraDirection::BACKWARD, speed);
        if (ImGui::IsKeyDown(ImGuiKey_A))
            camera->Move(CameraDirection::LEFT, speed);
        if (ImGui::IsKeyDown(ImGuiKey_D))
            camera->Move(CameraDirection::RIGHT, speed);

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_T, false))
            Debug::TriggerCapture();

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_G, false))
            renderer->ToggleGUI();

        if (EventSystem::PollEvent(EventCompileShaders))
            renderer->PollShaders();

        if (EventSystem::PollEvent(EventResize))
        {
            if (!isMinimized())
            {
                int w, h;
                SDL_Vulkan_GetDrawableSize(m_handle, &w, &h);
                renderer->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
        }

        EventSystem::ClearPushedEvents();

        return true;
    }

    void Window::WrapMouse(int &x, int &y)
    {
        const Rect2D &rect = GetGlobalSystem<RendererSystem>()->GetRenderArea().scissor;

        if (x < rect.x + 15)
        {
            x = rect.x + 15;
        }
        else if (x > rect.x + rect.width - 15)
        {
            x = rect.x + rect.width - 15;
        }

        if (y < rect.y + 15)
        {
            y = rect.y + 15;
        }
        else if (y > rect.y + rect.height - 15)
        {
            y = rect.y + rect.height - 15;
        }

        SDL_WarpMouseInWindow(m_handle, x, y);
    }

    bool Window::IsInsideRenderWindow(int x, int y)
    {
        const Rect2D &rect = GetGlobalSystem<RendererSystem>()->GetRenderArea().scissor;

        return x > rect.x && y > rect.y && x < rect.x + rect.width &&
               y < rect.y + rect.height;
    }

    bool Window::isMinimized()
    {
        return (SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MINIMIZED) != 0;
    }

    void Window::GetDrawableSize(int &width, int &height)
    {
        SDL_Vulkan_GetDrawableSize(m_handle, &width, &height);
    }

    void Window::Show()
    {
        SDL_ShowWindow(m_handle);
    }

    void Window::Hide()
    {
        SDL_HideWindow(m_handle);
    }

    void Window::Minimize()
    {
        SDL_MinimizeWindow(m_handle);
    }

    void Window::Maximize()
    {
        SDL_MaximizeWindow(m_handle);
    }
}
