#include "Window.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "Camera/Camera.h"
#include "GUI/GUIState.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
#include "Runtime/RenderDocCaptureShortcut.h"
#include "Script/Bindings/Input/InputState.h"
#include "Systems/RendererSystem.h"
#include "UI/RuntimeUi.h"
#include "Window/WindowEvents.h"
#include "imgui/imgui_impl_sdl2.h"

#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"

namespace pe
{
    Window::Window(int x, int y, int w, int h, uint32_t flags) : m_owned(true)
    {
        m_apiHandle = SDL_CreateWindow("", x, y, w, h, flags);
        if (!m_apiHandle)
        {
            PE_ERROR("[SDL] %s", SDL_GetError());
            return;
        }

        auto setTitle = [this](const std::any &title)
        {
            SDL_SetWindowTitle(m_apiHandle, std::any_cast<std::string>(title).c_str());
        };

        m_setTitleToken = EventSystem::RegisterCallbackWithToken(EventType::SetWindowTitle, setTitle);
    }

    Window::Window(SDL_Window *existing) : m_owned(false)
    {
        m_apiHandle = existing;

        auto setTitle = [this](const std::any &title)
        {
            SDL_SetWindowTitle(m_apiHandle, std::any_cast<std::string>(title).c_str());
        };

        m_setTitleToken = EventSystem::RegisterCallbackWithToken(EventType::SetWindowTitle, setTitle);
    }

    Window::~Window()
    {
        EventSystem::UnregisterCallback(EventType::SetWindowTitle, m_setTitleToken);
        if (m_owned)
        {
            SDL_DestroyWindow(m_apiHandle);
            SDL_Quit();
        }
    }

    inline bool IsButtonDown(int *x, int *y, uint32_t button)
    {
        return SDL_GetMouseState(x, y) & button;
    }

    inline bool IsMouseOverSceneViewImage()
    {
        if (!GUIState::s_sceneViewImageRectValid ||
            GUIState::s_sceneViewImageWidth <= 0.0f ||
            GUIState::s_sceneViewImageHeight <= 0.0f)
            return false;

        int x = 0;
        int y = 0;
        SDL_GetMouseState(&x, &y);

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        return fx >= GUIState::s_sceneViewImageMinX &&
               fy >= GUIState::s_sceneViewImageMinY &&
               fx < GUIState::s_sceneViewImageMinX + GUIState::s_sceneViewImageWidth &&
               fy < GUIState::s_sceneViewImageMinY + GUIState::s_sceneViewImageHeight;
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

        SDL_Event sdlEvent;
        std::vector<std::string> dropAccum;
        InputState::BeginFrame();
        RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
        while (SDL_PollEvent(&sdlEvent))
        {
            const bool fullWindowViewport = rendererSystem && !rendererSystem->GetGUI().Render();
            if ((IsScriptPlayMode() || fullWindowViewport) && IsRenderDocCaptureShortcut(sdlEvent))
                TriggerRenderDocCaptureShortcut();

            if (sdlEvent.type == SDL_QUIT)
                EventSystem::PushEvent(EventType::RequestExit);

            ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
            const bool runtimeUiCaptured = runtimeUi && runtimeUi->ProcessEvent(sdlEvent);

            if (!runtimeUiCaptured && sdlEvent.type == SDL_MOUSEMOTION)
                InputState::AddMouseMotion(sdlEvent.motion.xrel, sdlEvent.motion.yrel);

            if (!runtimeUiCaptured && sdlEvent.type == SDL_MOUSEWHEEL &&
                (fullWindowViewport || IsMouseOverSceneViewImage()))
                InputState::AddMouseWheel(sdlEvent.wheel.x, sdlEvent.wheel.y);

            if (IsRuntimeWindowResizeEvent(sdlEvent))
            {
                const WindowDrawableExtent extent = GetWindowDrawableExtent(m_apiHandle);
                if (extent.IsValid() &&
                    (RHII.GetSwapchain() == nullptr ||
                     RHII.GetWidth() != static_cast<uint32_t>(extent.width) ||
                     RHII.GetHeight() != static_cast<uint32_t>(extent.height)))
                {
                    EventSystem::PushEvent(EventType::Resize);
                }
            }

            if (sdlEvent.type == SDL_DROPBEGIN)
                dropAccum.clear();
            else if (sdlEvent.type == SDL_DROPFILE)
            {
                if (sdlEvent.drop.file)
                {
                    dropAccum.emplace_back(sdlEvent.drop.file);
                    SDL_free(sdlEvent.drop.file);
                }
            }
            else if (sdlEvent.type == SDL_DROPCOMPLETE)
            {
                if (!dropAccum.empty())
                    EventSystem::DispatchEvent(EventType::FileDrop, dropAccum);
                dropAccum.clear();
            }
        }

        if (runtimeUi)
        {
            InputState::SetMouseCapturedByUi(runtimeUi->WantsMouseCapture());
            InputState::SetKeyboardCapturedByUi(runtimeUi->WantsKeyboardCapture());
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
                case EventType::RequestExit:
                {
                    if (rendererSystem->GetGUI().IsInitialized())
                        rendererSystem->GetGUI().TriggerExitConfirmation();
                    else
                        return false;
                    break;
                }
                case EventType::CompileShaders:
                {
                    std::optional<size_t> hash = std::nullopt;
                    if (event.payload.has_value() && event.payload.type() == typeid(size_t))
                        hash = std::any_cast<size_t>(event.payload);

                    rendererSystem->PollShaders(hash);
                    CommandBuffer::ClearCache(); // force fresh pipeline rebuild with new shaders
                    break;
                }
                case EventType::CompileScripts:
                {
                    if (HasGlobalSystem<ScriptSystem>())
                        if (auto *ss = GetGlobalSystem<ScriptSystem>())
                            ss->Reload();
                    break;
                }
                case EventType::RunCommand:
                {
                    ScriptSystem *ss = HasGlobalSystem<ScriptSystem>() ? GetGlobalSystem<ScriptSystem>() : nullptr;
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
                case EventType::ReloadModule:
                {
                    // Host-owned event: preserve it for Phasma/Editor/main.cpp, which unloads
                    // the module only after Frame() returns to the safe reload point.
                    EventSystem::PushEvent(EventType::ReloadModule, std::move(event.payload));
                    return true;
                }
                case EventType::PresentMode:
                {
                    SceneSettings &gSettings = Settings::Get<SceneSettings>();
                    const PePresentMode previousMode = RHII.GetSurface()->GetPresentMode();
                    RHII.GetSurface()->SetPresentMode(gSettings.preferred_present_mode);
                    const PePresentMode effectiveMode = RHII.GetSurface()->GetPresentMode();
                    gSettings.preferred_present_mode = effectiveMode;
                    if (effectiveMode != previousMode)
                        EventSystem::PushEvent(EventType::Resize);
                    break;
                }
                case EventType::Resize:
                {
                    if (!isMinimized())
                    {
                        const WindowDrawableExtent extent = GetWindowDrawableExtent(m_apiHandle);
                        if (extent.IsValid())
                        {
                            rendererSystem->Resize(static_cast<uint32_t>(extent.width),
                                                   static_cast<uint32_t>(extent.height));
                        }
                    }
                    break;
                }
                case EventType::DynamicRendering:
                {
                    bool enable = std::any_cast<bool>(event.payload);
                    rendererSystem->WaitAllFramesCommands();
                    Settings::Get<SceneSettings>().dynamic_rendering = enable;
                    break;
                }
                case EventType::ModelLoaded:
                {
                    ModelAsset *model = std::any_cast<ModelAsset *>(event.payload);
                    if (!model)
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().AddModel(model);
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    break;
                }
                case EventType::ModelLoadedForNode:
                {
                    auto req = std::any_cast<Scene::ModelLoadForNodeRequest>(event.payload);
                    if (!req.model)
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    Scene &scene = rendererSystem->GetScene();
                    scene.AddModel(req.model);
                    // Reparent all model root nodes under the target node
                    if (req.parentNode)
                    {
                        for (NodeId *root : scene.GetModelRootNodes(req.model))
                            scene.ReparentNode(root, req.parentNode);
                    }
                    scene.UpdateGeometryBuffers();
                    break;
                }
                case EventType::ModelRemoved:
                {
                    ModelAsset *model = std::any_cast<ModelAsset *>(event.payload);
                    if (!model)
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().RemoveModel(model);
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    rendererSystem->ResetTAAHistory();
                    break;
                }
                case EventType::ModelsRemoved:
                {
                    auto models = std::any_cast<std::vector<ModelAsset *>>(event.payload);
                    if (models.empty())
                        break;
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().RemoveModels(std::move(models));
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    rendererSystem->ResetTAAHistory();
                    break;
                }
                case EventType::NodeRemoved:
                {
                    rendererSystem->WaitAllFramesCommands();
                    rendererSystem->GetScene().UpdateGeometryBuffers();
                    rendererSystem->ResetTAAHistory();
                    break;
                }
                case EventType::PrimitiveAttachedToNode:
                {
                    auto req = std::any_cast<Scene::PrimitiveAttachRequest>(event.payload);
                    if (req.node && req.model)
                    {
                        rendererSystem->WaitAllFramesCommands();
                        rendererSystem->GetScene().AttachPrimitiveToNode(req.node, req.model);
                        rendererSystem->GetScene().UpdateGeometryBuffers();
                    }
                    break;
                }
                case EventType::SetRenderMode:
                {
                    rendererSystem->WaitAllFramesCommands();
                    auto mode = std::any_cast<RenderMode>(event.payload);
                    Settings::Get<SceneSettings>().render_mode =
                        ClampRenderModeToRayTracingSupport(mode, RHII.GetCaps().rayTracing);
                    rendererSystem->ResetTAAHistory();
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
        return IsWindowMinimized(m_apiHandle);
    }

    void Window::GetDrawableSize(int &width, int &height)
    {
        const WindowDrawableExtent extent = GetWindowDrawableExtent(m_apiHandle);
        width = extent.width;
        height = extent.height;
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
