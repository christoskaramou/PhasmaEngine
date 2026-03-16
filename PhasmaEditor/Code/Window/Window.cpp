#include "Window.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "GUI/GUIState.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui_impl_sdl2.h"

#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#endif

namespace pe
{
    Window::Window(int x, int y, int w, int h, uint32_t flags)
    {
        m_apiHandle = SDL_CreateWindow("", x, y, w, h, flags);
        if (!m_apiHandle)
        {
            PE_ERROR("SDL_GetError: %s", SDL_GetError());
            return;
        }

        auto setTitle = [this](const std::any &title)
        {
            SDL_SetWindowTitle(m_apiHandle, std::any_cast<std::string>(title).c_str());
        };

        EventSystem::RegisterCallback(EventType::SetWindowTitle, setTitle);
    }

    Window::~Window()
    {
        SDL_DestroyWindow(m_apiHandle);
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

        if (GUIState::s_sceneViewFocused && IsButtonDown(&x, &y, triggerButton))
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

    bool Window::ProcessEvents()
    {
        RendererSystem *rendererSystem = GetGlobalSystem<RendererSystem>();
        PostProcessSystem *postProcessSystem = GetGlobalSystem<PostProcessSystem>();
        Camera *camera = rendererSystem->GetScene().GetActiveCamera();

        ImGuiIO &io = ImGui::GetIO();

        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
            if (sdlEvent.type == SDL_QUIT)
                EventSystem::PushEvent(EventType::Quit);

            ImGui_ImplSDL2_ProcessEvent(&sdlEvent);

            if (sdlEvent.type == SDL_WINDOWEVENT && sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                EventSystem::PushEvent(EventType::Resize);
        }

        EventSystem::QueuedEvent event;
        while (EventSystem::PollEvent(event))
        {
            if (auto eventType = std::get_if<pe::EventType>(&event.key))
            {
                switch (*eventType)
                {
                case EventType::Quit:
                {
                    return false;
                }
                case EventType::CompileShaders:
                {
                    rendererSystem->PollShaders();
                    postProcessSystem->PollShaders();
                    break;
                }
#if defined(PE_SCRIPTS)
                case EventType::CompileScripts:
                {
                    if (auto *ss = GetGlobalSystem<ScriptSystem>())
                        ss->Reload();
                    break;
                }
                case EventType::RunCommand:
                {
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (!ss || !ss->IsInitialized())
                        break;

                    std::string code;
                    {
                        std::ifstream f(Path::Assets + "Agent/command.lua");
                        if (f.is_open())
                            code.assign(std::istreambuf_iterator<char>(f), {});
                    }

                    if (!code.empty())
                    {
                        std::string result = ss->ExecuteLua(code);
                        std::ofstream out(Path::Assets + "Agent/result.txt", std::ios::trunc);
                        out << result;
                    }
                    break;
                }
#endif
                case EventType::PresentMode:
                {
                    GlobalSettings &gSettings = Settings::Get<GlobalSettings>();
                    RHII.ChangePresentMode(gSettings.preferred_present_mode);
                    break;
                }
                case EventType::Resize:
                {
                    if (!isMinimized())
                    {
                        int w, h;
                        SDL_Vulkan_GetDrawableSize(m_apiHandle, &w, &h);
                        rendererSystem->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                        postProcessSystem->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                    }
                    break;
                }
                case EventType::DynamicRendering:
                {
                    bool enable = std::any_cast<bool>(event.payload);
                    rendererSystem->WaitAllFramesCommands();
                    Settings::Get<GlobalSettings>().dynamic_rendering = enable;
                    break;
                }
                case EventType::ModelLoaded:
                {
                    Model *model = std::any_cast<Model *>(event.payload);
                    if (!model)
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().AddModel(model);
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    model->SetRenderReady(true);
                    break;
                }
                case EventType::ModelRemoved:
                {
                    Model *model = std::any_cast<Model *>(event.payload);
                    if (!model)
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().RemoveModel(model);
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    break;
                }
                case EventType::ModelsRemoved:
                {
                    auto models = std::any_cast<std::vector<Model *>>(event.payload);
                    if (models.empty())
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().RemoveModels(std::move(models));
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    break;
                }
                case EventType::SetRenderMode:
                {
                    rendererSystem->WaitAllFramesCommands();
                    auto mode = std::any_cast<RenderMode>(event.payload);
                    Settings::Get<GlobalSettings>().render_mode = mode;
                    break;
                }
                default:
                    break;
                }
            }
            else if (auto id = std::get_if<size_t>(&event.key))
            {
                // dynamic file event *id
                // event.payload could carry info from FileWatcher if you want
            }
        }

        EventSystem::ClearPushedEvents();

        return true;
    }

    void Window::WrapMouse(int &x, int &y)
    {
        Image *displayRT = GetGlobalSystem<RendererSystem>()->GetDisplayRT();
        Rect2Di rect = Rect2Di(0, 0, displayRT->GetWidth(), displayRT->GetHeight());

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

        SDL_WarpMouseInWindow(m_apiHandle, x, y);
    }

    bool Window::IsInsideRenderWindow(int x, int y)
    {
        Image *displayRT = GetGlobalSystem<RendererSystem>()->GetDisplayRT();
        Rect2Di rect = Rect2Di(0, 0, displayRT->GetWidth(), displayRT->GetHeight());

        return x > rect.x && y > rect.y && x < rect.x + rect.width &&
               y < rect.y + rect.height;
    }

    bool Window::isMinimized()
    {
        return (SDL_GetWindowFlags(m_apiHandle) & SDL_WINDOW_MINIMIZED) != 0;
    }

    void Window::GetDrawableSize(int &width, int &height)
    {
        SDL_Vulkan_GetDrawableSize(m_apiHandle, &width, &height);
    }

    void Window::Show()
    {
        SDL_ShowWindow(m_apiHandle);
    }

    void Window::Hide()
    {
        SDL_HideWindow(m_apiHandle);
    }

    void Window::Minimize()
    {
        SDL_MinimizeWindow(m_apiHandle);
    }

    void Window::Maximize()
    {
        SDL_MaximizeWindow(m_apiHandle);
    }
} // namespace pe
