#include "UI/RuntimeUi.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/ScriptSystem.h"

namespace pe
{
    namespace
    {
        RuntimeUiSystem *s_activeRuntimeUi = nullptr;
#if defined(PE_ANDROID)
        constexpr float kTextReadabilityScale = 1.5f;
#else
        constexpr float kTextReadabilityScale = 1.0f;
#endif

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
                if (AssetFileExists(candidate))
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

        RuntimeUiColor ToRuntimeUiColor(const vec4 &value)
        {
            return RuntimeUiColor{value.r, value.g, value.b, value.a};
        }

        RuntimeUiQuadVisualStyle ToRuntimeUiVisualStyle(NodeRuntimeUiWidgetType type)
        {
            switch (type)
            {
            case NodeRuntimeUiWidgetType::Text:
                return RuntimeUiQuadVisualStyle::Text;
            case NodeRuntimeUiWidgetType::Button:
                return RuntimeUiQuadVisualStyle::Button;
            case NodeRuntimeUiWidgetType::Image:
                return RuntimeUiQuadVisualStyle::Image;
            case NodeRuntimeUiWidgetType::Panel:
            default:
                return RuntimeUiQuadVisualStyle::Panel;
            }
        }

        bool GetRuntimeUiNodeRect(const Scene &scene, const NodeId *node, const NodeRuntimeUiTag &ui,
                                  float surfaceW, float surfaceH, float &x, float &y, float &z, float &w, float &h)
        {
            if (!node)
                return false;

            const mat4 &world = scene.GetWorldMatrix(node);
            const float offsetX = world[3].x;
            const float offsetY = world[3].y;
            z = world[3].z;
            w = glm::length(vec3(world[0]));
            h = glm::length(vec3(world[1]));

            if (!std::isfinite(offsetX) || !std::isfinite(offsetY) || !std::isfinite(z) || !std::isfinite(w) || !std::isfinite(h))
                return false;

            w = std::max(w, 1.0f);
            h = std::max(h, 1.0f);

            // RectTransform layout: the node translation is the offset from the screen
            // anchor (anchor*surface) to the element's pivot; the top-left is then the
            // pivot point minus pivot*size. anchor=(0,0)+pivot=(0,0) reproduces the
            // legacy "translation = top-left" behaviour.
            const float anchorX = ui.anchor.x * surfaceW;
            const float anchorY = ui.anchor.y * surfaceH;
            x = anchorX + offsetX - ui.pivot.x * w;
            y = anchorY + offsetY - ui.pivot.y * h;
            return true;
        }

        std::string MakeSceneWidgetId(const NodeRuntimeUiTag &ui, const NodeId *node)
        {
            if (!ui.widgetId.empty())
                return ui.widgetId;
            if (!node)
                return "node";
            return "node_" + std::to_string(node->index) + "_" + std::to_string(node->revision);
        }

        bool ShouldDispatchRuntimeUiAction(const std::string &actionName,
                                           const RuntimeUiWidgetState &state,
                                           const RuntimeUiWidgetState &previousState)
        {
            const std::string action = actionName.empty() ? "click" : actionName;
            if (action == "click")
                return state.clicked;
            if (action == "hover_enter" || action == "hover")
                return state.hovered && !previousState.hovered;
            if (action == "press")
                return state.down && !previousState.down;
            if (action == "release")
                return !state.down && previousState.down;
            if (action == "drag_start")
                return state.dragStarted;
            if (action == "dragging" || action == "drag")
                return state.dragging;
            if (action == "drag_release")
                return state.dragReleased;

            return state.clicked;
        }

        void DispatchRuntimeUiNodeAction(const std::string &screenId,
                                         const std::string &widgetId,
                                         NodeId *node,
                                         uint32_t nodeIndex,
                                         uint32_t nodeRevision,
                                         const RuntimeUiWidgetState &state,
                                         const RuntimeUiWidgetState &previousState)
        {
            if (!node)
                return;

            Scene *scene = GetActiveScene();
            if (!scene)
                return;
            // The retained widget's node pointer dangles once the node is destroyed (deferred
            // destroy after SyncSceneWidgets, or a whole-scene swap) — IsNodeAlive would read
            // freed memory. Validate through the pool by captured index+revision instead.
            if (nodeIndex >= scene->GetNodeCount())
                return;
            const NodeId *current = scene->GetNodeId(nodeIndex);
            if (current != node || current->revision != nodeRevision)
                return;

            const NodeRuntimeUiTag *ui = scene->GetRuntimeUiComponent(node);
            if (!ui || !ui->authored || ui->actionFunction.empty())
                return;
            if (!ShouldDispatchRuntimeUiAction(ui->actionName, state, previousState))
                return;

            ScriptSystem *scripts = GetGlobalSystem<ScriptSystem>();
            if (!scripts)
                return;

            scripts->InvokeNodeRuntimeUiAction(node,
                                               ui->actionFunction,
                                               ui->actionName,
                                               state,
                                               screenId,
                                               widgetId);
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

        if (m_initialized || m_backend || !m_imageCache.empty())
            RHII.WaitDeviceIdle();

        if (m_backend)
            m_backend->Shutdown();

        m_backend.reset();
        ClearAllScreens();
        m_imageCache.clear();
        m_styleBackgroundPaths = {};
        m_styleBackgroundImages = {};
        m_backendName = "none";
        m_initialized = false;
        m_frameOpen = false;
        m_frameSurfaceWidth = 0;
        m_frameSurfaceHeight = 0;
        m_frameUiScale = 1.0f;
        m_textScale = 1.0f;
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

    float RuntimeUiSystem::GetFrameUiScale() const
    {
        return m_frameUiScale;
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
        screen.needsSort = true; // new widget appended out of z-order; sort in BuildFrame
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

    void RuntimeUiSystem::SetScreenScrollable(const std::string &screenId, bool scrollable)
    {
        GetOrCreateScreen(screenId).scrollable = scrollable;
    }

    void RuntimeUiSystem::SetScreenMaxHeight(const std::string &screenId, float maxHeight)
    {
        GetOrCreateScreen(screenId).maxHeight = std::max(0.0f, maxHeight);
    }

    void RuntimeUiSystem::ClearScreen(const std::string &screenId)
    {
        GetOrCreateScreen(screenId).widgets.clear();
        if (m_backend)
            m_backend->ResetInputState();
    }

    void RuntimeUiSystem::ClearAllScreens()
    {
        m_screens.clear();
        m_sceneAuthoredWidgetIds.clear();
        if (m_backend)
            m_backend->ResetInputState();
    }

    void RuntimeUiSystem::ClearScriptScreens()
    {
        // Drop every screen that isn't scene-authored. Scene-authored screens (tracked in
        // m_sceneAuthoredWidgetIds) are left alone — they are re-synced from the scene each
        // frame anyway. This is the runtime-UI counterpart to restoring scene nodes on play
        // stop: scene nodes (e.g. an authored FPS readout) revert via the snapshot, but a
        // script-built overlay (minimap dots, portrait, command card) has no such restore and
        // would otherwise stay frozen on screen after Stop.
        m_screens.erase(std::remove_if(m_screens.begin(), m_screens.end(),
                                       [this](const Screen &screen)
                                       {
                                           return m_sceneAuthoredWidgetIds.find(screen.id) ==
                                                  m_sceneAuthoredWidgetIds.end();
                                       }),
                        m_screens.end());
        if (m_backend)
            m_backend->ResetInputState();
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

    void RuntimeUiSystem::SetStyleBackground(RuntimeUiQuadVisualStyle style, const std::string &path)
    {
        const size_t index = static_cast<size_t>(style);
        if (index >= m_styleBackgroundImages.size() || m_styleBackgroundPaths[index] == path)
            return;

        m_styleBackgroundPaths[index] = path;
        m_styleBackgroundImages[index] = path.empty() ? nullptr : LoadImageResource(path);
    }

    void RuntimeUiSystem::SetTextScale(float scale)
    {
        if (std::isfinite(scale))
            m_textScale = std::clamp(scale, 0.5f, 3.0f);
    }

    void RuntimeUiSystem::SetQuad(const std::string &screenId,
                                  const std::string &widgetId,
                                  const RuntimeUiQuadDesc &desc,
                                  const std::string &path)
    {
        Screen &screen = GetOrCreateScreen(screenId);
        Widget &widget = GetOrCreateWidget(screen, widgetId, WidgetType::Quad);
        // Sort only when order keys change. Animated HUD (damage floats, etc.) updates x/y/alpha
        // every step with fixed z — re-flagging needsSort each call forced a full screen sort
        // every frame even though order was unchanged. New widgets still dirty via GetOrCreateWidget.
        const bool orderDirty = widget.z != desc.z || widget.bringToFront != desc.bringToFront;
        widget.label = desc.label ? desc.label : "";
        widget.title = desc.title ? desc.title : "";
        widget.subtitle = desc.subtitle ? desc.subtitle : "";
        widget.textValue = desc.body ? desc.body : "";
        widget.footer = desc.footer ? desc.footer : "";
        widget.x = desc.x;
        widget.y = desc.y;
        widget.z = desc.z;
        widget.width = desc.width;
        widget.height = desc.height;
        widget.fillColor = desc.fillColor;
        widget.borderColor = desc.borderColor;
        widget.accentColor = desc.accentColor;
        widget.textColor = desc.textColor;
        widget.imageTint = desc.imageTint;
        widget.backgroundImageTint = desc.backgroundImageTint;
        widget.node = desc.node;
        widget.nodeIndex = desc.node ? desc.node->index : 0;
        widget.nodeRevision = desc.node ? desc.node->revision : 0;
        widget.draggable = desc.draggable;
        widget.selected = desc.selected;
        widget.visible = desc.visible;
        widget.bringToFront = desc.bringToFront;
        widget.noInput = desc.noInput;
        widget.fontScale = desc.fontScale > 0.0f ? desc.fontScale : 1.0f;
        widget.textAlignH = desc.textAlignH;
        widget.textAlignV = desc.textAlignV;
        widget.textOffsetX = desc.textOffsetX;
        widget.textOffsetY = desc.textOffsetY;
        widget.textInsetRight = std::max(0.0f, desc.textInsetRight);
        widget.visualStyle = desc.visualStyle;
        widget.fit = desc.fit;
        // Resolve the image only when the path actually changes. LoadImageResource stats the
        // filesystem (ResolveImagePath candidates + weakly_canonical, ~10 syscalls) before its
        // cache lookup — ~150us per call, paid per animated quad per frame by set_quad-driven UIs.
        if (path.empty())
            widget.image = desc.image;
        else if (path != widget.imagePath)
            widget.image = LoadImageResource(path);
        widget.imagePath = path;
        const auto hasText = [](const char *text)
        { return text && text[0] != '\0'; };
        const bool hasContent = hasText(desc.label) || hasText(desc.title) || hasText(desc.subtitle) ||
                                hasText(desc.body) || hasText(desc.footer);
        const bool wantsBackground = desc.fillColor.a > 0.0f ||
                                     (desc.visualStyle == RuntimeUiQuadVisualStyle::Button && hasContent);
        const size_t styleIndex = static_cast<size_t>(desc.visualStyle);
        widget.backgroundImage = wantsBackground && styleIndex < m_styleBackgroundImages.size()
                                     ? m_styleBackgroundImages[styleIndex]
                                     : nullptr;
        if (orderDirty)
            screen.needsSort = true;
    }

    void RuntimeUiSystem::SyncSceneWidgets(Scene &scene)
    {
        uint32_t surfW = 0, surfH = 0;
        GetFrameSurfaceSize(surfW, surfH);
        const float fsw = static_cast<float>(surfW);
        const float fsh = static_cast<float>(surfH);
        const float layoutX = m_frameSafeAreaValid ? m_frameSafeAreaMinX : 0.0f;
        const float layoutY = m_frameSafeAreaValid ? m_frameSafeAreaMinY : 0.0f;
        const float layoutW = m_frameSafeAreaValid ? m_frameSafeAreaWidth : fsw;
        const float layoutH = m_frameSafeAreaValid ? m_frameSafeAreaHeight : fsh;

        // Update authored widgets in place this frame (SetQuad -> GetOrCreateWidget reuses the
        // existing widget), then remove only the ones that vanished. The old path removed and
        // reconstructed every authored widget every frame — M string-heavy reallocations plus a
        // full re-sort per node — which is what made per-frame-driven scene UI (HUD pools, any
        // moving authored node) far heavier than the in-place script set_quad path.
        std::unordered_map<std::string, std::unordered_set<std::string>> current;

        for (uint32_t i = 0; i < scene.GetNodeCount(); ++i)
        {
            NodeId *node = scene.GetNodeId(i);
            const NodeRuntimeUiTag *ui = scene.GetRuntimeUiComponent(node);
            if (!ui || !ui->authored || !ui->visible || !scene.IsNodeHierarchyEnabled(node))
                continue;

            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            if (!GetRuntimeUiNodeRect(scene, node, *ui, layoutW, layoutH, x, y, z, w, h))
                continue;
            x += layoutX;
            y += layoutY;

            const std::string screenId = ui->screenId.empty() ? "__scene_ui" : ui->screenId;
            const std::string widgetId = MakeSceneWidgetId(*ui, node);
            current[screenId].insert(widgetId);

            SetScreenVisible(screenId, true);
            SetScreenOverlay(screenId, true);
            SetScreenTitle(screenId, "Scene UI");

            RuntimeUiQuadDesc desc{};
            desc.id = widgetId.c_str();
            desc.label = ui->label.c_str();
            desc.title = ui->title.c_str();
            desc.subtitle = ui->subtitle.c_str();
            desc.body = ui->body.c_str();
            desc.footer = ui->footer.c_str();
            desc.x = x;
            desc.y = y;
            desc.z = z;
            desc.width = w;
            desc.height = h;
            desc.fillColor = ToRuntimeUiColor(ui->fillColor);
            desc.borderColor = ToRuntimeUiColor(ui->borderColor);
            desc.accentColor = ToRuntimeUiColor(ui->accentColor);
            desc.textColor = ToRuntimeUiColor(ui->textColor);
            desc.imageTint = ToRuntimeUiColor(ui->imageTint);
            desc.node = node;
            desc.draggable = ui->draggable;
            desc.visible = ui->visible;
            desc.bringToFront = ui->bringToFront;
            desc.noInput = ui->noInput;
            desc.fontScale = ui->fontScale;
            desc.textAlignH = static_cast<RuntimeUiTextAlignH>(ui->textAlignH);
            desc.textAlignV = static_cast<RuntimeUiTextAlignV>(ui->textAlignV);
            desc.textOffsetX = ui->textOffset.x;
            desc.textOffsetY = ui->textOffset.y;
            desc.visualStyle = ToRuntimeUiVisualStyle(ui->widgetType);
            SetQuad(screenId, widgetId, desc, ui->imagePath);
        }

        // Remove widgets authored last frame but absent this frame (node disabled / hidden /
        // destroyed); everything still present was updated in place above. Hide any screen that
        // ends up empty, matching the previous semantics.
        for (const auto &[screenId, widgetIds] : m_sceneAuthoredWidgetIds)
        {
            const auto curIt = current.find(screenId);
            for (const std::string &widgetId : widgetIds)
            {
                if (curIt == current.end() || curIt->second.find(widgetId) == curIt->second.end())
                    RemoveWidget(screenId, widgetId);
            }

            if (Screen *screen = FindScreen(screenId); screen && screen->widgets.empty())
            {
                screen->visible = false;
                screen->overlay = true;
            }
        }

        m_sceneAuthoredWidgetIds = std::move(current);
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

    void RuntimeUiSystem::SortQuadWidgets(Screen &screen)
    {
        std::stable_sort(screen.widgets.begin(),
                         screen.widgets.end(),
                         [](const Widget &a, const Widget &b)
                         {
                             const bool aQuad = a.type == WidgetType::Quad;
                             const bool bQuad = b.type == WidgetType::Quad;
                             if (aQuad != bQuad)
                                 return !aQuad && bQuad;
                             if (!aQuad)
                                 return false;
                             if (a.bringToFront != b.bringToFront)
                                 return !a.bringToFront && b.bringToFront;
                             return a.z < b.z;
                         });
    }

    Image *RuntimeUiSystem::LoadImageResource(const std::string &path)
    {
        const std::filesystem::path resolvedPath = ResolveImagePath(path);
        const std::string cacheKey = NormalizeImagePathKey(resolvedPath);

        auto cacheIt = m_imageCache.find(cacheKey);
        if (cacheIt != m_imageCache.end())
            return cacheIt->second.get();

        if (!AssetFileExists(resolvedPath))
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
        PE_PROFILE_SCOPE("RuntimeUI BuildFrame");
        uint32_t quadCount = 0;
        uint32_t overlayQuadCount = 0;
        uint32_t textQuadCount = 0;

        // The authored scene-UI screen is base HUD chrome; draw it first so script-created
        // overlay screens (dynamic HUD content) always render on top. Overlay quads are all
        // brought to the display front in draw order, so the last screen drawn wins z-order.
        // Screen creation order differs between the editor and player frame pumps (the player
        // updates scripts before SyncSceneWidgets, the editor after), which otherwise lets
        // authored panels occlude script overlays in the player only. Pinning the scene-UI
        // screen to the back makes the layering host-independent.
        std::stable_partition(m_screens.begin(), m_screens.end(),
                              [](const Screen &screen)
                              { return screen.id == "__scene_ui"; });

        // Node actions run Lua that may mutate screens/widgets (set_ui, set_quad, remove), which
        // would invalidate the containers being iterated below — collect now, dispatch after.
        struct PendingAction
        {
            std::string screenId;
            std::string widgetId;
            NodeId *node;
            uint32_t nodeIndex;
            uint32_t nodeRevision;
            RuntimeUiWidgetState state;
            RuntimeUiWidgetState previousState;
        };
        std::vector<PendingAction> pendingActions;

        for (Screen &screen : m_screens)
        {
            if (!screen.visible)
                continue;

            // Resolve quad z-order once per frame (SetQuad / new-widget creation only flag it).
            if (screen.needsSort)
            {
                SortQuadWidgets(screen);
                screen.needsSort = false;
            }

            RuntimeUiScreenDesc desc{};
            desc.id = screen.id;
            desc.title = screen.title.empty() ? MakeDefaultTitle(screen.id) : screen.title;
            desc.overlay = screen.overlay;
            desc.scrollable = screen.scrollable;
            desc.maxHeight = screen.maxHeight;

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
                        ++quadCount;
                        if (screen.overlay)
                            ++overlayQuadCount;
                        if (!widget.label.empty() || !widget.title.empty() || !widget.subtitle.empty() ||
                            !widget.textValue.empty() || !widget.footer.empty())
                            ++textQuadCount;
                        RuntimeUiQuadDesc quadDesc{};
                        quadDesc.id = widget.id.c_str();
                        quadDesc.label = widget.label.c_str();
                        quadDesc.title = widget.title.c_str();
                        quadDesc.subtitle = widget.subtitle.c_str();
                        quadDesc.body = widget.textValue.c_str();
                        quadDesc.footer = widget.footer.c_str();
                        quadDesc.image = widget.image;
                        quadDesc.backgroundImage = widget.backgroundImage;
                        quadDesc.x = widget.x;
                        quadDesc.y = widget.y;
                        quadDesc.z = widget.z;
                        quadDesc.width = widget.width;
                        quadDesc.height = widget.height;
                        quadDesc.fillColor = widget.fillColor;
                        quadDesc.borderColor = widget.borderColor;
                        quadDesc.accentColor = widget.accentColor;
                        quadDesc.textColor = widget.textColor;
                        quadDesc.imageTint = widget.imageTint;
                        quadDesc.backgroundImageTint = widget.backgroundImageTint;
                        quadDesc.node = widget.node;
                        quadDesc.draggable = widget.draggable;
                        quadDesc.selected = widget.selected;
                        quadDesc.visible = widget.visible;
                        quadDesc.bringToFront = widget.bringToFront;
                        quadDesc.noInput = widget.noInput;
                        quadDesc.fontScale = widget.fontScale / m_frameUiScale;
                        quadDesc.textScale = kTextReadabilityScale * m_textScale;
                        quadDesc.textAlignH = widget.textAlignH;
                        quadDesc.textAlignV = widget.textAlignV;
                        quadDesc.textOffsetX = widget.textOffsetX;
                        quadDesc.textOffsetY = widget.textOffsetY;
                        quadDesc.textInsetRight = widget.textInsetRight;
                        quadDesc.visualStyle = widget.visualStyle;
                        quadDesc.fit = widget.fit;
                        const RuntimeUiWidgetState previousState = widget.state;
                        widget.state = m_backend->Quad(quadDesc);
                        const RuntimeUiWidgetState &s = widget.state;
                        const bool actionable = s.clicked || s.dragStarted || s.dragging || s.dragReleased ||
                                                s.hovered != previousState.hovered || s.down != previousState.down;
                        if (widget.node && actionable)
                        {
                            pendingActions.push_back({screen.id, widget.id, widget.node,
                                                      widget.nodeIndex, widget.nodeRevision,
                                                      widget.state, previousState});
                        }
                        break;
                    }
                    }
                }
            }
            m_backend->EndScreen();
        }

        PE_PROFILE_COUNTER("RuntimeUI.Quads", quadCount);
        PE_PROFILE_COUNTER("RuntimeUI.OverlayQuads", overlayQuadCount);
        PE_PROFILE_COUNTER("RuntimeUI.TextQuads", textQuadCount);

        for (const PendingAction &action : pendingActions)
        {
            DispatchRuntimeUiNodeAction(action.screenId, action.widgetId, action.node,
                                        action.nodeIndex, action.nodeRevision,
                                        action.state, action.previousState);
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
