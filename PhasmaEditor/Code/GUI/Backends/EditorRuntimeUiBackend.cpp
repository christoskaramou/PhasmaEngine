#include "GUI/Backends/EditorRuntimeUiBackend.h"
#include "GUI/GUIState.h"
#include "UI/Backends/ImGuiRuntimeUiStyle.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <vector>

namespace pe
{
    namespace
    {
        struct RuntimeUiRect
        {
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;

            bool Contains(float x, float y) const
            {
                return x >= minX && x <= maxX && y >= minY && y <= maxY;
            }
        };

        bool IsMouseInputEvent(const SDL_Event &event)
        {
            return event.type == SDL_MOUSEMOTION ||
                   event.type == SDL_MOUSEBUTTONDOWN ||
                   event.type == SDL_MOUSEBUTTONUP ||
                   event.type == SDL_MOUSEWHEEL;
        }

        bool IsKeyboardInputEvent(const SDL_Event &event)
        {
            return event.type == SDL_KEYDOWN ||
                   event.type == SDL_KEYUP ||
                   event.type == SDL_TEXTINPUT ||
                   event.type == SDL_TEXTEDITING;
        }

        class EditorRuntimeUiBackend final : public IRuntimeUiBackend
        {
        public:
            const char *GetName() const override { return "Dear ImGui"; }

            bool IsSupported() const override
            {
                return ImGui::GetCurrentContext() != nullptr;
            }

            bool Init(const RuntimeUiBackendInitInfo &) override
            {
                m_initialized = IsSupported();
                return m_initialized;
            }

            void Shutdown() override
            {
                m_initialized = false;
                m_frameOpen = false;
                m_screenBegan = false;
                m_mouseCaptureActive = false;
                m_keyboardCapture = false;
                m_captureRects.clear();
                m_nextCaptureRects.clear();
            }

            bool ProcessEvent(const SDL_Event &event) override
            {
                if (!m_initialized)
                    return false;

                if (IsKeyboardInputEvent(event))
                    return m_keyboardCapture;

                if (!IsMouseInputEvent(event))
                    return false;

                bool captured = false;
                switch (event.type)
                {
                case SDL_MOUSEMOTION:
                    captured = m_mouseCaptureActive ||
                               IsPointCaptured(static_cast<float>(event.motion.x),
                                               static_cast<float>(event.motion.y));
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    captured = IsPointCaptured(static_cast<float>(event.button.x),
                                               static_cast<float>(event.button.y));
                    m_mouseCaptureActive = captured;
                    break;
                case SDL_MOUSEBUTTONUP:
                    captured = m_mouseCaptureActive ||
                               IsPointCaptured(static_cast<float>(event.button.x),
                                               static_cast<float>(event.button.y));
                    m_mouseCaptureActive = false;
                    break;
                case SDL_MOUSEWHEEL:
                {
                    int x = 0;
                    int y = 0;
                    SDL_GetMouseState(&x, &y);
                    captured = IsPointCaptured(static_cast<float>(x), static_cast<float>(y));
                    break;
                }
                default:
                    break;
                }

                return captured;
            }

            void BeginFrame(const RuntimeUiFrameInfo &frameInfo) override
            {
                if (!m_initialized || m_frameOpen)
                    return;

                m_frameOpen = true;
                m_surfaceWidth = frameInfo.width;
                m_surfaceHeight = frameInfo.height;
                m_screenOffsetY = 0.0f;
                m_nextCaptureRects.clear();
                m_nextKeyboardCapture = false;
                m_nextMouseCaptureActive = false;
            }

            bool BeginScreen(const RuntimeUiScreenDesc &screen) override
            {
                if (!m_frameOpen || !GUIState::s_sceneViewImageRectValid)
                    return false;

                m_runtimeScale = ComputeRuntimeScale();
                const float pad = runtime_ui_imgui::kViewportPadding * m_runtimeScale;
                const float gap = runtime_ui_imgui::kScreenGap * m_runtimeScale;
                const float viewportX = GUIState::s_sceneViewImageMinX;
                const float viewportY = GUIState::s_sceneViewImageMinY;
                const float viewportW = GUIState::s_sceneViewImageWidth;
                const float viewportH = GUIState::s_sceneViewImageHeight;
                if (viewportW <= pad * 2.0f || viewportH <= pad * 2.0f)
                    return false;

                ImGui::SetNextWindowPos(ImVec2(viewportX + pad, viewportY + pad + m_screenOffsetY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(runtime_ui_imgui::kWindowWidth * m_runtimeScale, 0.0f), ImGuiCond_Always);

                const std::string title = screen.title.empty() ? screen.id : screen.title;
                const std::string windowId = title + "##RuntimeUiEditor_" + screen.id;

                m_screenBegan = true;
                m_screenGap = gap;
                PushRuntimeAppearance();
                return ImGui::Begin(windowId.c_str(), nullptr, runtime_ui_imgui::kEditorWindowFlags);
            }

            void Text(const char *label, const char *value) override
            {
                if (!m_frameOpen || !m_screenBegan)
                    return;
                ImGui::Text("%s: %s", label ? label : "", value ? value : "");
            }

            void Number(const char *label, double value) override
            {
                if (!m_frameOpen || !m_screenBegan)
                    return;
                ImGui::Text("%s: %.3f", label ? label : "", value);
            }

            bool Checkbox(const char *label, bool &value) override
            {
                if (!m_frameOpen || !m_screenBegan)
                    return false;
                return ImGui::Checkbox(label ? label : "", &value);
            }

            bool Button(const char *label) override
            {
                if (!m_frameOpen || !m_screenBegan)
                    return false;
                return ImGui::Button(label ? label : "");
            }

            void EndScreen() override
            {
                if (!m_frameOpen || !m_screenBegan)
                    return;

                const ImVec2 pos = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

                RuntimeUiRect rect{};
                rect.minX = pos.x;
                rect.minY = pos.y;
                rect.maxX = pos.x + size.x;
                rect.maxY = pos.y + size.y;
                m_nextCaptureRects.push_back(rect);
                m_nextKeyboardCapture = m_nextKeyboardCapture || windowFocused;
                m_nextMouseCaptureActive = m_nextMouseCaptureActive || (windowHovered && ImGui::IsAnyMouseDown());
                m_screenOffsetY += size.y + m_screenGap;

                ImGui::End();
                PopRuntimeAppearance();
                m_screenBegan = false;
            }

            void EndFrame() override
            {
                if (!m_frameOpen)
                    return;

                m_captureRects = m_nextCaptureRects;
                m_keyboardCapture = m_nextKeyboardCapture;
                m_mouseCaptureActive = ImGui::IsAnyMouseDown() && (m_mouseCaptureActive || m_nextMouseCaptureActive);
                m_frameOpen = false;
            }

            bool HasDrawData() const override
            {
                return false;
            }

            bool WantsMouseCapture() const override
            {
                if (!m_initialized)
                    return false;

                if (m_mouseCaptureActive)
                    return true;

                int x = 0;
                int y = 0;
                SDL_GetMouseState(&x, &y);
                return IsPointCaptured(static_cast<float>(x), static_cast<float>(y));
            }

            bool WantsKeyboardCapture() const override
            {
                return m_initialized && m_keyboardCapture;
            }

            void Render(const RuntimeUiRenderContext &) override {}

        private:
            bool IsPointCaptured(float x, float y) const
            {
                if (!GUIState::s_sceneViewImageRectValid)
                    return false;

                for (const RuntimeUiRect &rect : m_captureRects)
                {
                    if (rect.Contains(x, y))
                        return true;
                }
                return false;
            }

            float ComputeRuntimeScale() const
            {
                if (!GUIState::s_sceneViewImageRectValid || m_surfaceWidth == 0 || m_surfaceHeight == 0)
                    return 1.0f;

                const float scaleX = GUIState::s_sceneViewImageWidth / static_cast<float>(m_surfaceWidth);
                const float scaleY = GUIState::s_sceneViewImageHeight / static_cast<float>(m_surfaceHeight);
                return std::clamp(std::min(scaleX, scaleY), 0.1f, 4.0f);
            }

            ImFont *GetRuntimeFont() const
            {
                if (GUIState::s_fontClassic)
                    return GUIState::s_fontClassic;

                ImFontAtlas *fonts = ImGui::GetIO().Fonts;
                return fonts && fonts->Fonts.Size > 0 ? fonts->Fonts[0] : nullptr;
            }

            void PushRuntimeAppearance()
            {
                if (m_appearancePushed)
                    return;

                m_savedStyle = ImGui::GetStyle();
                m_scaledRuntimeStyle = runtime_ui_imgui::BuildStyle(m_runtimeScale);
                ImGui::GetStyle() = m_scaledRuntimeStyle;

                ImGuiIO &io = ImGui::GetIO();
                m_savedFontGlobalScale = io.FontGlobalScale;
                io.FontGlobalScale = 1.0f;

                m_runtimeFont = GetRuntimeFont();
                if (m_runtimeFont)
                {
                    m_savedFontScale = m_runtimeFont->Scale;
                    m_runtimeFont->Scale = m_savedFontScale * m_runtimeScale;
                    ImGui::PushFont(m_runtimeFont);
                    m_fontPushed = true;
                }

                m_appearancePushed = true;
            }

            void PopRuntimeAppearance()
            {
                if (!m_appearancePushed)
                    return;

                ImGui::GetStyle() = m_savedStyle;
                ImGui::GetIO().FontGlobalScale = m_savedFontGlobalScale;

                if (m_runtimeFont)
                    m_runtimeFont->Scale = m_savedFontScale;

                if (m_fontPushed)
                {
                    ImGui::PopFont();
                    m_fontPushed = false;
                }

                m_runtimeFont = nullptr;
                m_appearancePushed = false;
            }

            std::vector<RuntimeUiRect> m_captureRects;
            std::vector<RuntimeUiRect> m_nextCaptureRects;
            ImGuiStyle m_scaledRuntimeStyle{};
            ImGuiStyle m_savedStyle{};
            ImFont *m_runtimeFont = nullptr;
            uint32_t m_surfaceWidth = 0;
            uint32_t m_surfaceHeight = 0;
            float m_runtimeScale = 1.0f;
            float m_screenOffsetY = 0.0f;
            float m_screenGap = 8.0f;
            float m_savedFontScale = 1.0f;
            float m_savedFontGlobalScale = 1.0f;
            bool m_initialized = false;
            bool m_frameOpen = false;
            bool m_screenBegan = false;
            bool m_appearancePushed = false;
            bool m_fontPushed = false;
            bool m_mouseCaptureActive = false;
            bool m_nextMouseCaptureActive = false;
            bool m_keyboardCapture = false;
            bool m_nextKeyboardCapture = false;
        };
    } // namespace

    std::unique_ptr<IRuntimeUiBackend> CreateEditorRuntimeUiBackend()
    {
        return std::make_unique<EditorRuntimeUiBackend>();
    }
} // namespace pe
