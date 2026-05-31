#include "UI/RuntimeUi.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        RuntimeUiSystem *s_activeRuntimeUi = nullptr;

        std::string MakeDefaultTitle(const std::string &screenId)
        {
            return screenId.empty() ? "Runtime UI" : screenId;
        }

        std::filesystem::path U8Path(const std::string &utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string PathToUtf8(const std::filesystem::path &path)
        {
            const auto utf8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(utf8.c_str()));
        }

        std::filesystem::path ResolveImagePath(const std::string &path)
        {
            std::filesystem::path imagePath = U8Path(path);
            if (imagePath.is_absolute())
                return imagePath;

            std::string relative = path;
            if (relative.rfind("Assets/", 0) == 0 || relative.rfind("Assets\\", 0) == 0)
                relative = relative.substr(7);

            imagePath = U8Path(relative);
            const std::filesystem::path assets = U8Path(Path::Assets);
            std::array<std::filesystem::path, 5> candidates = {
                assets / imagePath,
                assets / "Textures" / imagePath,
                assets / "Images" / imagePath,
                assets / "Objects" / imagePath,
                imagePath};

            for (const std::filesystem::path &candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                    return candidate;
            }

            return candidates[0];
        }

        std::string NormalizeImagePathKey(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
                normalized = path.lexically_normal();
            return PathToUtf8(normalized);
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

        if (!m_imageCache.empty())
        {
            if (Queue *queue = RHII.GetMainQueue())
                queue->WaitIdle();
        }

        if (m_backend)
            m_backend->Shutdown();

        m_backend.reset();
        m_imageCache.clear();
        m_backendName = "none";
        m_initialized = false;
        m_frameOpen = false;
        m_frameSurfaceWidth = 0;
        m_frameSurfaceHeight = 0;
        m_frameUiScale = 1.0f;
        m_frameInputEnabled = true;
        m_frameInputRectValid = false;
        m_frameInputRectMinX = 0.0f;
        m_frameInputRectMinY = 0.0f;
        m_frameInputRectWidth = 0.0f;
        m_frameInputRectHeight = 0.0f;
        m_frameSafeAreaValid = false;
        m_frameSafeAreaMinX = 0.0f;
        m_frameSafeAreaMinY = 0.0f;
        m_frameSafeAreaWidth = 0.0f;
        m_frameSafeAreaHeight = 0.0f;
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
        frameInfo.uiScale = m_frameUiScale > 0.0f ? m_frameUiScale : 1.0f;
        frameInfo.width = m_frameSurfaceWidth > 0 ? m_frameSurfaceWidth : RHII.GetWidth();
        frameInfo.height = m_frameSurfaceHeight > 0 ? m_frameSurfaceHeight : RHII.GetHeight();
        frameInfo.inputEnabled = m_frameInputEnabled;
        frameInfo.inputRectValid = m_frameInputRectValid;
        frameInfo.inputRectMinX = m_frameInputRectMinX;
        frameInfo.inputRectMinY = m_frameInputRectMinY;
        frameInfo.inputRectWidth = m_frameInputRectWidth;
        frameInfo.inputRectHeight = m_frameInputRectHeight;
        frameInfo.safeAreaValid = m_frameSafeAreaValid;
        frameInfo.safeAreaMinX = m_frameSafeAreaMinX;
        frameInfo.safeAreaMinY = m_frameSafeAreaMinY;
        frameInfo.safeAreaWidth = m_frameSafeAreaWidth;
        frameInfo.safeAreaHeight = m_frameSafeAreaHeight;
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

    void RuntimeUiSystem::SetFrameUiScale(float scale)
    {
        m_frameUiScale = scale > 0.0f ? scale : 1.0f;
    }

    void RuntimeUiSystem::SetFrameSafeArea(float minX, float minY, float width, float height)
    {
        m_frameSafeAreaValid = width > 0.0f && height > 0.0f;
        m_frameSafeAreaMinX = minX;
        m_frameSafeAreaMinY = minY;
        m_frameSafeAreaWidth = width;
        m_frameSafeAreaHeight = height;
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

    void RuntimeUiSystem::GetFrameSurfaceSize(uint32_t &width, uint32_t &height) const
    {
        width = m_frameSurfaceWidth > 0 ? m_frameSurfaceWidth : RHII.GetWidth();
        height = m_frameSurfaceHeight > 0 ? m_frameSurfaceHeight : RHII.GetHeight();
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

    void RuntimeUiSystem::SetScreenOverlay(const std::string &screenId, bool overlay)
    {
        GetOrCreateScreen(screenId).overlay = overlay;
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

    void RuntimeUiSystem::SetImage(const std::string &screenId,
                                   const std::string &widgetId,
                                   const std::string &label,
                                   const std::string &path,
                                   float width,
                                   float height)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Image);
        widget.label = label;
        widget.imagePath = path;
        widget.imageWidth = width;
        widget.imageHeight = height;
        widget.image = path.empty() ? nullptr : LoadImageResource(path);
    }

    void RuntimeUiSystem::SetQuad(const std::string &screenId,
                                  const std::string &widgetId,
                                  const RuntimeUiQuadDesc &desc,
                                  const std::string &path)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Quad);
        widget.label = desc.label ? desc.label : "";
        widget.title = desc.title ? desc.title : "";
        widget.subtitle = desc.subtitle ? desc.subtitle : "";
        widget.textValue = desc.body ? desc.body : "";
        widget.footer = desc.footer ? desc.footer : "";
        widget.x = desc.x;
        widget.y = desc.y;
        widget.width = desc.width;
        widget.height = desc.height;
        widget.fillColor = desc.fillColor;
        widget.borderColor = desc.borderColor;
        widget.accentColor = desc.accentColor;
        widget.textColor = desc.textColor;
        widget.imageTint = desc.imageTint;
        widget.node = desc.node;
        widget.draggable = desc.draggable;
        widget.selected = desc.selected;
        widget.visible = desc.visible;
        widget.bringToFront = desc.bringToFront;
        widget.fontScale = desc.fontScale > 0.0f ? desc.fontScale : 1.0f;
        widget.imagePath = path;
        widget.image = path.empty() ? desc.image : LoadImageResource(path);
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

    bool RuntimeUiSystem::GetWidgetState(const std::string &screenId,
                                         const std::string &widgetId,
                                         RuntimeUiWidgetState &state) const
    {
        const Screen *screen = FindScreen(screenId);
        if (!screen)
            return false;

        for (const Widget &widget : screen->widgets)
        {
            if (widget.id == widgetId)
            {
                state = widget.state;
                return true;
            }
        }

        return false;
    }

    bool RuntimeUiSystem::GetNodeRect(NodeId *node, float &x, float &y, float &w, float &h) const
    {
        if (!node)
            return false;

        bool found = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        for (const Screen &screen : m_screens)
        {
            if (!screen.visible || !screen.overlay)
                continue;

            for (const Widget &widget : screen.widgets)
            {
                if (widget.type != WidgetType::Quad || !widget.visible || widget.node != node)
                    continue;
                if (widget.width <= 0.0f || widget.height <= 0.0f)
                    continue;

                const float widgetMinX = widget.x;
                const float widgetMinY = widget.y;
                const float widgetMaxX = widget.x + widget.width;
                const float widgetMaxY = widget.y + widget.height;
                if (!found)
                {
                    minX = widgetMinX;
                    minY = widgetMinY;
                    maxX = widgetMaxX;
                    maxY = widgetMaxY;
                    found = true;
                }
                else
                {
                    minX = std::min(minX, widgetMinX);
                    minY = std::min(minY, widgetMinY);
                    maxX = std::max(maxX, widgetMaxX);
                    maxY = std::max(maxY, widgetMaxY);
                }
            }
        }

        if (!found)
            return false;

        x = minX;
        y = minY;
        w = maxX - minX;
        h = maxY - minY;
        return true;
    }

    NodeId *RuntimeUiSystem::PickNode(float x, float y) const
    {
        for (auto screenIt = m_screens.rbegin(); screenIt != m_screens.rend(); ++screenIt)
        {
            const Screen &screen = *screenIt;
            if (!screen.visible || !screen.overlay)
                continue;

            for (auto widgetIt = screen.widgets.rbegin(); widgetIt != screen.widgets.rend(); ++widgetIt)
            {
                const Widget &widget = *widgetIt;
                if (widget.type != WidgetType::Quad || !widget.visible || !widget.node)
                    continue;
                if (widget.width <= 0.0f || widget.height <= 0.0f)
                    continue;
                if (x >= widget.x && y >= widget.y && x <= widget.x + widget.width && y <= widget.y + widget.height)
                    return widget.node;
            }
        }
        return nullptr;
    }

    Image *RuntimeUiSystem::LoadImageResource(const std::string &path)
    {
        const std::filesystem::path resolvedPath = ResolveImagePath(path);
        const std::string cacheKey = NormalizeImagePathKey(resolvedPath);

        auto cacheIt = m_imageCache.find(cacheKey);
        if (cacheIt != m_imageCache.end())
            return cacheIt->second.get();

        if (!std::filesystem::exists(resolvedPath))
        {
            PE_WARN("[RuntimeUI] image file not found: %s", path.c_str());
            m_imageCache.emplace(cacheKey, nullptr);
            return nullptr;
        }

        if (ResourceHandle<Image> cached = ResourceManager::Get().Find<Image>(cacheKey))
        {
            m_imageCache.emplace(cacheKey, cached.GetShared());
            return cached.get();
        }

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            PE_WARN("[RuntimeUI] cannot load image without a main queue: %s", path.c_str());
            m_imageCache.emplace(cacheKey, nullptr);
            return nullptr;
        }

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        Image *rawImage = Image::LoadRGBA8(cmd, cacheKey);
        cmd->End();

        if (!rawImage)
        {
            cmd->Return();
            m_imageCache.emplace(cacheKey, nullptr);
            return nullptr;
        }

        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        std::shared_ptr<Image> image(rawImage, [](Image *p)
                                     { Image::Destroy(p); });
        ResourceManager::Get().Register<Image>(cacheKey, image);
        m_imageCache.emplace(cacheKey, image);
        return image.get();
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
            desc.overlay = screen.overlay;

            const bool open = m_backend->BeginScreen(desc);
            if (open)
            {
                for (Widget &widget : screen.widgets)
                {
                    if (!widget.visible)
                    {
                        widget.state = {};
                        continue;
                    }

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
                    case WidgetType::Image:
                        if (widget.image)
                        {
                            RuntimeUiImageDesc imageDesc{};
                            imageDesc.image = widget.image;
                            imageDesc.label = widget.label.c_str();
                            imageDesc.width = widget.imageWidth;
                            imageDesc.height = widget.imageHeight;

                            const float nativeWidth = widget.image->GetWidth_f();
                            const float nativeHeight = widget.image->GetHeight_f();
                            if (imageDesc.width <= 0.0f && imageDesc.height <= 0.0f)
                            {
                                imageDesc.width = nativeWidth;
                                imageDesc.height = nativeHeight;
                            }
                            else if (imageDesc.width <= 0.0f && imageDesc.height > 0.0f && nativeHeight > 0.0f)
                            {
                                imageDesc.width = imageDesc.height * nativeWidth / nativeHeight;
                            }
                            else if (imageDesc.height <= 0.0f && imageDesc.width > 0.0f && nativeWidth > 0.0f)
                            {
                                imageDesc.height = imageDesc.width * nativeHeight / nativeWidth;
                            }

                            m_backend->DrawImage(imageDesc);
                        }
                        break;
                    case WidgetType::Quad:
                    {
                        RuntimeUiQuadDesc quadDesc{};
                        quadDesc.id = widget.id.c_str();
                        quadDesc.label = widget.label.c_str();
                        quadDesc.title = widget.title.c_str();
                        quadDesc.subtitle = widget.subtitle.c_str();
                        quadDesc.body = widget.textValue.c_str();
                        quadDesc.footer = widget.footer.c_str();
                        quadDesc.image = widget.image;
                        quadDesc.x = widget.x;
                        quadDesc.y = widget.y;
                        quadDesc.width = widget.width;
                        quadDesc.height = widget.height;
                        quadDesc.fillColor = widget.fillColor;
                        quadDesc.borderColor = widget.borderColor;
                        quadDesc.accentColor = widget.accentColor;
                        quadDesc.textColor = widget.textColor;
                        quadDesc.imageTint = widget.imageTint;
                        quadDesc.node = widget.node;
                        quadDesc.draggable = widget.draggable;
                        quadDesc.selected = widget.selected;
                        quadDesc.visible = widget.visible;
                        quadDesc.bringToFront = widget.bringToFront;
                        quadDesc.fontScale = widget.fontScale;
                        widget.state = m_backend->Quad(quadDesc);
                        break;
                    }
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
