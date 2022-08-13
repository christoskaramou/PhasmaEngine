#include "Window.h"
#include "Console/Console.h"
#include "Renderer/RHI.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Systems/CameraSystem.h"
#include "imgui/imgui_impl_sdl.h"

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
            SetTitle(std::any_cast<std::string>(title));
        };

        EventSystem::RegisterCallback(EventSetWindowTitle, setTitle);
    }

    Window::~Window()
    {
        SDL_DestroyWindow(m_handle);
        SDL_Quit();
    }

    void Window::SetTitle(const std::string &title)
    {
        SDL_SetWindowTitle(m_handle, title.c_str());
    }

    bool Window::ProcessEvents(double delta)
    {
        if (Console::close_app)
            EventSystem::PushEvent(EventQuit);

        RendererSystem *renderer = CONTEXT->GetSystem<RendererSystem>();
        CameraSystem *cameraSystem = CONTEXT->GetSystem<CameraSystem>();
        Camera *camera_main = cameraSystem->GetCamera(0);

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

        int x, y;
        if (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT))
        {
            bool skip = false;
            if (!SDL_GetRelativeMouseMode())
            {
                SDL_SetRelativeMouseMode(SDL_TRUE);
                skip = true;
            }
            SDL_GetRelativeMouseState(&x, &y);
            if (!skip)
                camera_main->Rotate(static_cast<float>(x), static_cast<float>(y));
        }
        else
        {
            if (SDL_GetRelativeMouseMode())
            {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                WrapMouse(x, y);
            }
        }

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

        float velocity = GUI::cameraSpeed * static_cast<float>(delta);
        if ((ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_S)) &&
            (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_D)))
            velocity *= 0.707f;

        if (ImGui::IsKeyDown(ImGuiKey_W))
            camera_main->Move(CameraDirection::FORWARD, velocity);
        if (ImGui::IsKeyDown(ImGuiKey_S))
            camera_main->Move(CameraDirection::BACKWARD, velocity);
        if (ImGui::IsKeyDown(ImGuiKey_A))
            camera_main->Move(CameraDirection::LEFT, velocity);
        if (ImGui::IsKeyDown(ImGuiKey_D))
            camera_main->Move(CameraDirection::RIGHT, velocity);

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_T))
            Debug::TriggerCapture();

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
        const Rect2D &rect2D = CONTEXT->GetSystem<RendererSystem>()->GetRenderArea().scissor;

        if (x < rect2D.x + 15)
        {
            x = rect2D.x + 15;
        }
        else if (x > rect2D.x + rect2D.width - 15)
        {
            x = rect2D.x + rect2D.width - 15;
        }

        if (y < rect2D.y + 15)
        {
            y = rect2D.y + 15;
        }
        else if (y > rect2D.y + rect2D.height - 15)
        {
            y = rect2D.y + rect2D.height - 15;
        }

        SDL_WarpMouseInWindow(m_handle, x, y);
    }

    bool Window::IsInsideRenderWindow(int x, int y)
    {
        const Rect2D &rect2D = CONTEXT->GetSystem<RendererSystem>()->GetRenderArea().scissor;

        return x > rect2D.x && y > rect2D.y && x < rect2D.x + rect2D.width &&
               y < rect2D.y + rect2D.height;
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
