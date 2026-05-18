#include "UI/RuntimeUi.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Base/Timer.h"

namespace pe
{
    namespace
    {
        RuntimeUiSystem *s_activeRuntimeUi = nullptr;
        constexpr const char *kSampleScreenId = "runtime.debug";

        std::string MakeDefaultTitle(const std::string &screenId)
        {
            return screenId.empty() ? "Runtime UI" : screenId;
        }
    } // namespace

    RuntimeUiSystem::RuntimeUiSystem() = default;

    RuntimeUiSystem::~RuntimeUiSystem()
    {
        Shutdown();
    }

    bool RuntimeUiSystem::Init(std::unique_ptr<IRuntimeUiBackend> backend, Image *renderTarget)
    {
        Shutdown();

        if (!backend)
            return false;

        m_backendName = backend->GetName() ? backend->GetName() : "unknown";
        if (!backend->IsSupported())
        {
            PE_WARN("[RuntimeUI] Backend is not supported: %s", m_backendName.c_str());
            return false;
        }

        RuntimeUiBackendInitInfo initInfo{};
        initInfo.renderTarget = renderTarget;
        if (!backend->Init(initInfo))
        {
            PE_WARN("[RuntimeUI] Backend failed to initialize: %s", m_backendName.c_str());
            return false;
        }

        m_backend = std::move(backend);
        m_initialized = true;
        return true;
    }

    void RuntimeUiSystem::Shutdown()
    {
        if (s_activeRuntimeUi == this)
            s_activeRuntimeUi = nullptr;

        if (m_backend)
            m_backend->Shutdown();

        m_backend.reset();
        m_backendName = "none";
        m_initialized = false;
        m_frameOpen = false;
        m_frameSurfaceWidth = 0;
        m_frameSurfaceHeight = 0;
        m_frameInputEnabled = true;
        m_frameInputRectValid = false;
        m_frameInputRectMinX = 0.0f;
        m_frameInputRectMinY = 0.0f;
        m_frameInputRectWidth = 0.0f;
        m_frameInputRectHeight = 0.0f;
    }

    bool RuntimeUiSystem::ProcessEvent(const SDL_Event &event)
    {
        return m_initialized && m_backend && m_backend->ProcessEvent(event);
    }

    void RuntimeUiSystem::BeginFrame()
    {
        if (!m_initialized || !m_backend || m_frameOpen)
            return;

        const double delta = FrameTimer::Instance().GetDelta();
        RuntimeUiFrameInfo frameInfo{};
        frameInfo.deltaSeconds = static_cast<float>(delta);
        frameInfo.width = m_frameSurfaceWidth > 0 ? m_frameSurfaceWidth : RHII.GetWidth();
        frameInfo.height = m_frameSurfaceHeight > 0 ? m_frameSurfaceHeight : RHII.GetHeight();
        frameInfo.inputEnabled = m_frameInputEnabled;
        frameInfo.inputRectValid = m_frameInputRectValid;
        frameInfo.inputRectMinX = m_frameInputRectMinX;
        frameInfo.inputRectMinY = m_frameInputRectMinY;
        frameInfo.inputRectWidth = m_frameInputRectWidth;
        frameInfo.inputRectHeight = m_frameInputRectHeight;
        m_backend->BeginFrame(frameInfo);
        m_frameOpen = true;
    }

    void RuntimeUiSystem::EndFrame()
    {
        if (!m_initialized || !m_backend || !m_frameOpen)
            return;

        BuildFrame();
        m_backend->EndFrame();
        m_frameOpen = false;
    }

    void RuntimeUiSystem::Render(CommandBuffer *cmd, Image *renderTarget)
    {
        if (!m_initialized || !m_backend || !cmd || !renderTarget || !m_backend->HasDrawData())
            return;

        RuntimeUiRenderContext context{};
        context.cmd = cmd;
        context.renderTarget = renderTarget;
        m_backend->Render(context);
    }

    void RuntimeUiSystem::SetFrameSurfaceSize(uint32_t width, uint32_t height)
    {
        m_frameSurfaceWidth = width;
        m_frameSurfaceHeight = height;
        m_frameInputEnabled = true;
        m_frameInputRectValid = false;
    }

    void RuntimeUiSystem::SetFrameInputRect(float minX, float minY, float width, float height)
    {
        m_frameInputRectValid = width > 0.0f && height > 0.0f;
        m_frameInputRectMinX = minX;
        m_frameInputRectMinY = minY;
        m_frameInputRectWidth = width;
        m_frameInputRectHeight = height;
    }

    void RuntimeUiSystem::DisableFrameInput()
    {
        m_frameInputEnabled = false;
        m_frameInputRectValid = false;
    }

    bool RuntimeUiSystem::WantsMouseCapture() const
    {
        return m_initialized && m_backend && m_backend->WantsMouseCapture();
    }

    bool RuntimeUiSystem::WantsKeyboardCapture() const
    {
        return m_initialized && m_backend && m_backend->WantsKeyboardCapture();
    }

    RuntimeUiSystem::Screen &RuntimeUiSystem::GetOrCreateScreen(const std::string &screenId)
    {
        if (Screen *screen = FindScreen(screenId))
            return *screen;

        Screen screen{};
        screen.id = screenId;
        screen.title = MakeDefaultTitle(screenId);
        m_screens.push_back(std::move(screen));
        return m_screens.back();
    }

    const RuntimeUiSystem::Screen *RuntimeUiSystem::FindScreen(const std::string &screenId) const
    {
        for (const Screen &screen : m_screens)
            if (screen.id == screenId)
                return &screen;
        return nullptr;
    }

    RuntimeUiSystem::Screen *RuntimeUiSystem::FindScreen(const std::string &screenId)
    {
        for (Screen &screen : m_screens)
            if (screen.id == screenId)
                return &screen;
        return nullptr;
    }

    RuntimeUiSystem::Widget &RuntimeUiSystem::GetOrCreateWidget(Screen &screen,
                                                                const std::string &widgetId,
                                                                WidgetType type)
    {
        for (Widget &widget : screen.widgets)
        {
            if (widget.id == widgetId)
            {
                widget.type = type;
                return widget;
            }
        }

        Widget widget{};
        widget.id = widgetId;
        widget.label = widgetId;
        widget.type = type;
        screen.widgets.push_back(std::move(widget));
        return screen.widgets.back();
    }

    void RuntimeUiSystem::SetScreenVisible(const std::string &screenId, bool visible)
    {
        GetOrCreateScreen(screenId).visible = visible;
    }

    bool RuntimeUiSystem::IsScreenVisible(const std::string &screenId) const
    {
        const Screen *screen = FindScreen(screenId);
        return screen && screen->visible;
    }

    void RuntimeUiSystem::SetScreenTitle(const std::string &screenId, const std::string &title)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        screen.title = title.empty() ? MakeDefaultTitle(screenId) : title;
    }

    void RuntimeUiSystem::ClearScreen(const std::string &screenId)
    {
        GetOrCreateScreen(screenId).widgets.clear();
    }

    void RuntimeUiSystem::RemoveWidget(const std::string &screenId, const std::string &widgetId)
    {
        Screen *screen = FindScreen(screenId);
        if (!screen)
            return;

        std::erase_if(screen->widgets,
                      [&](const Widget &widget)
                      {
                          return widget.id == widgetId;
                      });
    }

    void RuntimeUiSystem::SetText(const std::string &screenId,
                                  const std::string &widgetId,
                                  const std::string &label,
                                  const std::string &value)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Text);
        widget.label = label.empty() ? widgetId : label;
        widget.textValue = value;
    }

    void RuntimeUiSystem::SetNumber(const std::string &screenId,
                                    const std::string &widgetId,
                                    const std::string &label,
                                    double value)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Number);
        widget.label = label.empty() ? widgetId : label;
        widget.numberValue = value;
    }

    void RuntimeUiSystem::SetBool(const std::string &screenId,
                                  const std::string &widgetId,
                                  const std::string &label,
                                  bool value)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Bool);
        widget.label = label.empty() ? widgetId : label;
        widget.boolValue = value;
    }

    void RuntimeUiSystem::SetButton(const std::string &screenId,
                                    const std::string &widgetId,
                                    const std::string &label)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Button);
        widget.label = label.empty() ? widgetId : label;
    }

    bool RuntimeUiSystem::GetBool(const std::string &screenId,
                                  const std::string &widgetId,
                                  bool fallback) const
    {
        const Screen *screen = FindScreen(screenId);
        if (!screen)
            return fallback;

        for (const Widget &widget : screen->widgets)
            if (widget.id == widgetId && widget.type == WidgetType::Bool)
                return widget.boolValue;

        return fallback;
    }

    bool RuntimeUiSystem::ConsumeButtonClick(const std::string &screenId, const std::string &widgetId)
    {
        Screen *screen = FindScreen(screenId);
        if (!screen)
            return false;

        for (Widget &widget : screen->widgets)
        {
            if (widget.id == widgetId && widget.type == WidgetType::Button)
            {
                const bool clicked = widget.clicked;
                widget.clicked = false;
                return clicked;
            }
        }

        return false;
    }

    void RuntimeUiSystem::EnableSampleOverlay(bool enabled)
    {
        m_sampleOverlayEnabled = enabled;
        SetScreenTitle(kSampleScreenId, "Runtime Debug");
        SetScreenVisible(kSampleScreenId, enabled);
    }

    void RuntimeUiSystem::UpdateSampleOverlay()
    {
        if (!m_sampleOverlayEnabled)
            return;

        const double delta = FrameTimer::Instance().GetDelta();
        SetText(kSampleScreenId, "backend", "Backend", m_backendName);
        SetNumber(kSampleScreenId, "fps", "FPS", delta > 0.0 ? 1.0 / delta : 0.0);
        SetNumber(kSampleScreenId, "frame_ms", "Frame ms", delta * 1000.0);
        SetNumber(kSampleScreenId, "frame", "Frame", static_cast<double>(RHII.GetFrameCounter()));
    }

    void RuntimeUiSystem::BuildFrame()
    {
        for (Screen &screen : m_screens)
        {
            if (!screen.visible)
                continue;

            RuntimeUiScreenDesc desc{};
            desc.id = screen.id;
            desc.title = screen.title.empty() ? MakeDefaultTitle(screen.id) : screen.title;

            const bool open = m_backend->BeginScreen(desc);
            if (open)
            {
                for (Widget &widget : screen.widgets)
                {
                    switch (widget.type)
                    {
                    case WidgetType::Text:
                        m_backend->Text(widget.label.c_str(), widget.textValue.c_str());
                        break;
                    case WidgetType::Number:
                        m_backend->Number(widget.label.c_str(), widget.numberValue);
                        break;
                    case WidgetType::Bool:
                        m_backend->Checkbox(widget.label.c_str(), widget.boolValue);
                        break;
                    case WidgetType::Button:
                        if (m_backend->Button(widget.label.c_str()))
                            widget.clicked = true;
                        break;
                    }
                }
            }
            m_backend->EndScreen();
        }
    }

    void SetActiveRuntimeUi(RuntimeUiSystem *runtimeUi)
    {
        s_activeRuntimeUi = runtimeUi;
    }

    RuntimeUiSystem *GetActiveRuntimeUi()
    {
        return s_activeRuntimeUi;
    }
} // namespace pe
