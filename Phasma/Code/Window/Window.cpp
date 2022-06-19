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

#include "Window.h"
#include "Console/Console.h"
#include "Renderer/RHI.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

namespace pe
{
    Window::Window(int x, int y, int w, int h, uint32_t flags)
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        {
            std::cout << SDL_GetError();
            return;
        }

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

            if (event.type == SDL_MOUSEWHEEL)
            {
                if (event.wheel.x > 0)
                    io.MouseWheelH += 1;
                if (event.wheel.x < 0)
                    io.MouseWheelH -= 1;
                if (event.wheel.y > 0)
                    io.MouseWheel += 1;
                if (event.wheel.y < 0)
                    io.MouseWheel -= 1;
            }

            if (event.type == SDL_KEYDOWN)
            {
                const int key = event.key.keysym.scancode;
                io.KeysDown[key] = true;
            }
            else if (event.type == SDL_KEYUP)
            {
                const int key = event.key.keysym.scancode;
                io.KeysDown[key] = false;
                io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
                io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
                io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
                io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
            }

            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                EventSystem::PushEvent(EventResize);
        }

        if (EventSystem::PollEvent(EventQuit))
            return false;

        int x, y;
        if (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT))
        {
            bool skipFrame = false;
            if (!SDL_GetRelativeMouseMode())
            {
                SDL_SetRelativeMouseMode(SDL_TRUE);
                skipFrame = true;
            }
            SDL_GetRelativeMouseState(&x, &y);

            if (!skipFrame)
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

        if (io.KeysDown[SDL_SCANCODE_ESCAPE])
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
        if ((io.KeysDown[SDL_SCANCODE_W] || io.KeysDown[SDL_SCANCODE_S]) &&
            (io.KeysDown[SDL_SCANCODE_A] || io.KeysDown[SDL_SCANCODE_D]))
            velocity *= 0.707f;

        if (io.KeysDown[SDL_SCANCODE_W])
            camera_main->Move(CameraDirection::FORWARD, velocity);
        if (io.KeysDown[SDL_SCANCODE_S])
            camera_main->Move(CameraDirection::BACKWARD, velocity);
        if (io.KeysDown[SDL_SCANCODE_A])
            camera_main->Move(CameraDirection::LEFT, velocity);
        if (io.KeysDown[SDL_SCANCODE_D])
            camera_main->Move(CameraDirection::RIGHT, velocity);

        if (EventSystem::PollEvent(EventCompileShaders))
            renderer->RecreatePipelines();

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
}
