#pragma once

#include "Base/Base.h"
#include "SDL.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pe
{
    class CommandBuffer;
    class Image;

    struct RuntimeUiFrameInfo
    {
        float deltaSeconds = 0.0f;
        uint32_t width = 0;
        uint32_t height = 0;
        bool inputEnabled = true;
        bool inputRectValid = false;
        float inputRectMinX = 0.0f;
        float inputRectMinY = 0.0f;
        float inputRectWidth = 0.0f;
        float inputRectHeight = 0.0f;
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
        virtual void EndScreen() = 0;
        virtual void EndFrame() = 0;
        virtual bool HasDrawData() const = 0;
        virtual bool WantsMouseCapture() const { return false; }
        virtual bool WantsKeyboardCapture() const { return false; }
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
        void SetFrameInputRect(float minX, float minY, float width, float height);
        void DisableFrameInput();
        bool WantsMouseCapture() const;
        bool WantsKeyboardCapture() const;

        void SetScreenVisible(const std::string &screenId, bool visible);
        bool IsScreenVisible(const std::string &screenId) const;
        void SetScreenTitle(const std::string &screenId, const std::string &title);
        void ClearScreen(const std::string &screenId);
        void RemoveWidget(const std::string &screenId, const std::string &widgetId);

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

        bool GetBool(const std::string &screenId, const std::string &widgetId, bool fallback = false) const;
        bool ConsumeButtonClick(const std::string &screenId, const std::string &widgetId);
        void EnableSampleOverlay(bool enabled);
        bool IsSampleOverlayEnabled() const { return m_sampleOverlayEnabled; }
        void UpdateSampleOverlay();

    private:
        enum class WidgetType
        {
            Text,
            Number,
            Bool,
            Button
        };

        struct Widget
        {
            WidgetType type = WidgetType::Text;
            std::string id;
            std::string label;
            std::string textValue;
            double numberValue = 0.0;
            bool boolValue = false;
            bool clicked = false;
        };

        struct Screen
        {
            std::string id;
            std::string title;
            bool visible = false;
            std::vector<Widget> widgets;
        };

        Screen &GetOrCreateScreen(const std::string &screenId);
        const Screen *FindScreen(const std::string &screenId) const;
        Screen *FindScreen(const std::string &screenId);
        Widget &GetOrCreateWidget(Screen &screen, const std::string &widgetId, WidgetType type);
        void BuildFrame();

        std::unique_ptr<IRuntimeUiBackend> m_backend;
        std::vector<Screen> m_screens;
        std::string m_backendName = "none";
        bool m_initialized = false;
        bool m_frameOpen = false;
        bool m_sampleOverlayEnabled = false;
        uint32_t m_frameSurfaceWidth = 0;
        uint32_t m_frameSurfaceHeight = 0;
        bool m_frameInputEnabled = true;
        bool m_frameInputRectValid = false;
        float m_frameInputRectMinX = 0.0f;
        float m_frameInputRectMinY = 0.0f;
        float m_frameInputRectWidth = 0.0f;
        float m_frameInputRectHeight = 0.0f;
    };

    void SetActiveRuntimeUi(RuntimeUiSystem *runtimeUi);
    RuntimeUiSystem *GetActiveRuntimeUi();
} // namespace pe
