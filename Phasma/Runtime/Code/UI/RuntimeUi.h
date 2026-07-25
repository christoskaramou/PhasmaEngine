#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class Scene;
    struct NodeId;

    struct RuntimeUiFrameInfo
    {
        float deltaSeconds = 0.0f;
        float uiScale = 1.0f;
        uint32_t width = 0;
        uint32_t height = 0;
        bool inputEnabled = true;
        bool inputRectValid = false;
        float inputRectMinX = 0.0f;
        float inputRectMinY = 0.0f;
        float inputRectWidth = 0.0f;
        float inputRectHeight = 0.0f;
        bool safeAreaValid = false;
        float safeAreaMinX = 0.0f;
        float safeAreaMinY = 0.0f;
        float safeAreaWidth = 0.0f;
        float safeAreaHeight = 0.0f;
    };

    struct RuntimeUiBackendInitInfo
    {
        Image *renderTarget = nullptr;
    };

    struct RuntimeUiRenderContext
    {
        CommandBuffer *cmd = nullptr;
        Image *renderTarget = nullptr;
    };

    struct RuntimeUiScreenDesc
    {
        std::string id;
        std::string title;
        bool overlay = false;
        bool scrollable = false;
        float maxHeight = 0.0f;
    };

    struct RuntimeUiImageDesc
    {
        Image *image = nullptr;
        const char *label = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct RuntimeUiColor
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    enum class RuntimeUiQuadVisualStyle
    {
        Card,
        Panel,
        Text,
        Button,
        Image,
        Count
    };

    // Text alignment inside a widget rect. Default defers to each widget's natural alignment.
    enum class RuntimeUiTextAlignH : uint8_t
    {
        Default = 0,
        Left,
        Center,
        Right
    };

    enum class RuntimeUiTextAlignV : uint8_t
    {
        Default = 0,
        Top,
        Middle,
        Bottom
    };

    struct RuntimeUiQuadDesc
    {
        const char *id = nullptr;
        const char *label = nullptr;
        const char *title = nullptr;
        const char *subtitle = nullptr;
        const char *body = nullptr;
        const char *footer = nullptr;
        Image *image = nullptr;
        Image *backgroundImage = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        RuntimeUiColor fillColor{0.07f, 0.08f, 0.10f, 0.94f};
        RuntimeUiColor borderColor{0.45f, 0.48f, 0.54f, 0.95f};
        RuntimeUiColor accentColor{0.96f, 0.74f, 0.22f, 1.0f};
        RuntimeUiColor textColor{0.92f, 0.93f, 0.94f, 1.0f};
        RuntimeUiColor imageTint{1.0f, 1.0f, 1.0f, 1.0f};
        RuntimeUiColor backgroundImageTint{1.0f, 1.0f, 1.0f, 1.0f};
        // When true, the image is treated as a white+alpha mask so image_tint sets
        // a flat color (multiply with white) instead of blending into the art.
        bool imageColorize = false;
        NodeId *node = nullptr;
        bool draggable = false;
        bool selected = false;
        bool visible = true;
        bool bringToFront = false;
        bool noInput = false;
        float fontScale = 1.0f;
        float textScale = 1.0f;
        RuntimeUiTextAlignH textAlignH = RuntimeUiTextAlignH::Default;
        RuntimeUiTextAlignV textAlignV = RuntimeUiTextAlignV::Default;
        float textOffsetX = 0.0f;
        float textOffsetY = 0.0f;
        float textInsetRight = 0.0f;
        RuntimeUiQuadVisualStyle visualStyle = RuntimeUiQuadVisualStyle::Card;
        // Auto-size the quad to tightly fit its text (Text style). When set, the
        // backend ignores width/height and measures the content instead.
        bool fit = false;
    };

    struct RuntimeUiWidgetState
    {
        bool hovered = false;
        bool active = false;
        bool clicked = false;
        bool rightClicked = false;
        bool down = false;
        bool dragging = false;
        bool dragStarted = false;
        bool dragReleased = false;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        float dragDeltaX = 0.0f;
        float dragDeltaY = 0.0f;
    };

    class IRuntimeUiBackend
    {
    public:
        virtual ~IRuntimeUiBackend() = default;

        virtual const char *GetName() const = 0;
        virtual bool IsSupported() const = 0;
        virtual bool Init(const RuntimeUiBackendInitInfo &initInfo) = 0;
        virtual void Shutdown() = 0;
        virtual bool ProcessEvent(const SDL_Event &event) = 0;
        virtual void BeginFrame(const RuntimeUiFrameInfo &frameInfo) = 0;
        virtual bool BeginScreen(const RuntimeUiScreenDesc &screen) = 0;
        virtual void Text(const char *label, const char *value) = 0;
        virtual void Number(const char *label, double value) = 0;
        virtual bool Checkbox(const char *label, bool &value) = 0;
        virtual bool Button(const char *label) = 0;
        virtual void DrawImage(const RuntimeUiImageDesc &image) = 0;
        virtual RuntimeUiWidgetState Quad(const RuntimeUiQuadDesc &quad) = 0;
        virtual void EndScreen() = 0;
        virtual void EndFrame() = 0;
        virtual bool HasDrawData() const = 0;
        virtual bool WantsMouseCapture() const { return false; }
        virtual bool WantsKeyboardCapture() const { return false; }
        // Vertical wheel for this frame. Read from the UI layer, not raw input:
        // once the pointer is over an interactive quad the UI captures the event
        // and raw input never sees it.
        virtual float MouseWheel() const { return 0.0f; }
        virtual void ResetInputState() {}
        virtual void Render(const RuntimeUiRenderContext &context) = 0;
    };

    class RuntimeUiSystem : public NoCopy, public NoMove
    {
    public:
        RuntimeUiSystem();
        ~RuntimeUiSystem();

        bool Init(std::unique_ptr<IRuntimeUiBackend> backend, Image *renderTarget);
        void Shutdown();
        bool IsInitialized() const { return m_initialized; }
        const std::string &GetBackendName() const { return m_backendName; }

        bool ProcessEvent(const SDL_Event &event);
        void BeginFrame();
        void EndFrame();
        void Render(CommandBuffer *cmd, Image *renderTarget);
        void SetFrameSurfaceSize(uint32_t width, uint32_t height);
        void SetFrameUiScale(float scale);
        void SetFrameSafeArea(float minX, float minY, float width, float height);
        void SetFrameInputRect(float minX, float minY, float width, float height);
        void DisableFrameInput();
        void GetFrameSurfaceSize(uint32_t &width, uint32_t &height) const;
        // The DPI/density font scale the backend applies via io.FontGlobalScale (ddpi/96, clamped
        // [1,4]). Exposed so resolution-relative UIs (which size their own geometry in raw surface
        // pixels) can divide it out of their font_scale and stay WYSIWYG across DPIs.
        float GetFrameUiScale() const;
        bool WantsMouseCapture() const;
        bool WantsKeyboardCapture() const;
        // Force keyboard capture on for script-drawn text entry (dev console) that
        // is not an ImGui input widget. Mutes every Lua input.is_key_* reader.
        void SetKeyboardCaptureOverride(bool captured);
        float MouseWheel() const;

        void SetScreenVisible(const std::string &screenId, bool visible);
        bool IsScreenVisible(const std::string &screenId) const;
        void SetScreenTitle(const std::string &screenId, const std::string &title);
        void SetScreenOverlay(const std::string &screenId, bool overlay);
        void SetScreenScrollable(const std::string &screenId, bool scrollable);
        void SetScreenMaxHeight(const std::string &screenId, float maxHeight);
        void SetTextScale(float scale);
        void ClearScreen(const std::string &screenId);
        void ClearAllScreens();
        // Remove screens created by scripts (everything except scene-authored UI). Called on
        // play stop so a script-built HUD overlay does not linger after the scene is restored.
        void ClearScriptScreens();
        void RemoveWidget(const std::string &screenId, const std::string &widgetId);
        size_t RemoveWidgets(const std::string &screenId, const std::vector<std::string> &widgetIds);
        size_t SetWidgetsVisible(const std::string &screenId,
                                 const std::vector<std::string> &widgetIds,
                                 bool visible);

        void SetText(const std::string &screenId,
                     const std::string &widgetId,
                     const std::string &label,
                     const std::string &value);
        void SetNumber(const std::string &screenId,
                       const std::string &widgetId,
                       const std::string &label,
                       double value);
        void SetBool(const std::string &screenId,
                     const std::string &widgetId,
                     const std::string &label,
                     bool value);
        void SetButton(const std::string &screenId,
                       const std::string &widgetId,
                       const std::string &label);
        void SetImage(const std::string &screenId,
                      const std::string &widgetId,
                      const std::string &label,
                      const std::string &path,
                      float width = 0.0f,
                      float height = 0.0f);
        void SetStyleBackground(RuntimeUiQuadVisualStyle style, const std::string &path);
        int PreloadImages(const std::vector<std::string> &paths, bool colorize);
        void SetQuad(const std::string &screenId,
                     const std::string &widgetId,
                     const RuntimeUiQuadDesc &desc,
                     const std::string &path = {});
        void SyncSceneWidgets(Scene &scene);

        bool GetBool(const std::string &screenId, const std::string &widgetId, bool fallback = false) const;
        bool ConsumeButtonClick(const std::string &screenId, const std::string &widgetId);
        bool GetWidgetState(const std::string &screenId,
                            const std::string &widgetId,
                            RuntimeUiWidgetState &state) const;
        bool GetNodeRect(NodeId *node, float &x, float &y, float &w, float &h) const;
        NodeId *PickNode(float x, float y) const;

    private:
        enum class WidgetType
        {
            Text,
            Number,
            Bool,
            Button,
            Image,
            Quad
        };

        struct Widget
        {
            WidgetType type = WidgetType::Text;
            std::string id;
            std::string label;
            std::string title;
            std::string subtitle;
            std::string textValue;
            std::string footer;
            double numberValue = 0.0;
            bool boolValue = false;
            bool clicked = false;
            bool draggable = false;
            bool selected = false;
            bool visible = true;
            std::string imagePath;
            Image *image = nullptr;
            Image *backgroundImage = nullptr;
            float imageWidth = 0.0f;
            float imageHeight = 0.0f;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            RuntimeUiColor fillColor{0.07f, 0.08f, 0.10f, 0.94f};
            RuntimeUiColor borderColor{0.45f, 0.48f, 0.54f, 0.95f};
            RuntimeUiColor accentColor{0.96f, 0.74f, 0.22f, 1.0f};
            RuntimeUiColor textColor{0.92f, 0.93f, 0.94f, 1.0f};
            RuntimeUiColor imageTint{1.0f, 1.0f, 1.0f, 1.0f};
            RuntimeUiColor backgroundImageTint{1.0f, 1.0f, 1.0f, 1.0f};
            bool imageColorize = false;
            NodeId *node = nullptr;
            uint32_t nodeIndex = 0;    // captured at bind time; node itself may dangle
            uint32_t nodeRevision = 0; // once the node is destroyed, so validate via these
            RuntimeUiWidgetState state{};
            bool bringToFront = false;
            bool noInput = false;
            float fontScale = 1.0f;
            RuntimeUiTextAlignH textAlignH = RuntimeUiTextAlignH::Default;
            RuntimeUiTextAlignV textAlignV = RuntimeUiTextAlignV::Default;
            float textOffsetX = 0.0f;
            float textOffsetY = 0.0f;
            float textInsetRight = 0.0f;
            RuntimeUiQuadVisualStyle visualStyle = RuntimeUiQuadVisualStyle::Card;
            bool fit = false;
        };

        struct Screen
        {
            std::string id;
            std::string title;
            bool visible = false;
            bool overlay = false;
            bool scrollable = false;
            float maxHeight = 0.0f;
            std::vector<Widget> widgets;
            bool needsSort = false; // quad z-order dirty; re-sorted once per frame in BuildFrame
        };

        Screen &GetOrCreateScreen(const std::string &screenId);
        const Screen *FindScreen(const std::string &screenId) const;
        Screen *FindScreen(const std::string &screenId);
        Widget &GetOrCreateWidget(Screen &screen, const std::string &widgetId, WidgetType type);
        Image *LoadImageResource(const std::string &path);
        Image *LoadColorizeMask(const std::string &path);
        void SortQuadWidgets(Screen &screen);
        void BuildFrame();

        std::unique_ptr<IRuntimeUiBackend> m_backend;
        std::vector<Screen> m_screens;
        std::unordered_map<std::string, std::unordered_set<std::string>> m_sceneAuthoredWidgetIds;
        std::unordered_map<std::string, std::shared_ptr<Image>> m_imageCache;
        std::unordered_map<std::string, std::shared_ptr<Image>> m_colorizeMaskCache;
        std::array<std::string, static_cast<size_t>(RuntimeUiQuadVisualStyle::Count)> m_styleBackgroundPaths{};
        std::array<Image *, static_cast<size_t>(RuntimeUiQuadVisualStyle::Count)> m_styleBackgroundImages{};
        std::string m_backendName = "none";
        bool m_initialized = false;
        bool m_keyboardCaptureOverride = false;
        bool m_frameOpen = false;
        uint32_t m_frameSurfaceWidth = 0;
        uint32_t m_frameSurfaceHeight = 0;
        float m_frameUiScale = 1.0f;
        float m_textScale = 1.0f;
        bool m_frameInputEnabled = true;
        bool m_frameInputRectValid = false;
        float m_frameInputRectMinX = 0.0f;
        float m_frameInputRectMinY = 0.0f;
        float m_frameInputRectWidth = 0.0f;
        float m_frameInputRectHeight = 0.0f;
        bool m_frameSafeAreaValid = false;
        float m_frameSafeAreaMinX = 0.0f;
        float m_frameSafeAreaMinY = 0.0f;
        float m_frameSafeAreaWidth = 0.0f;
        float m_frameSafeAreaHeight = 0.0f;
    };

    void SetActiveRuntimeUi(RuntimeUiSystem *runtimeUi);
    RuntimeUiSystem *GetActiveRuntimeUi();
} // namespace pe
